// SPDX-License-Identifier: GPL-2.0
/*
 * Edge TPU firmware loader.
 *
 * Copyright (C) 2019-2020 Google, Inc.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>

#include "edgetpu-device-group.h"
#include "edgetpu-firmware.h"
#include "edgetpu-firmware-util.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-pm.h"
#include "edgetpu-shared-fw.h"
#include "edgetpu-telemetry.h"

/*
 * Descriptor for loaded firmware, either in shared buffer mode or legacy mode
 * (non-shared, custom allocated memory).
 */
struct edgetpu_firmware_desc {
	/*
	 * Mode independent buffer information. This is either passed into or
	 * updated by handlers.
	 */
	struct edgetpu_firmware_buffer buf;
	/*
	 * Shared firmware buffer when we're using shared buffer mode. This
	 * pointer to keep and release the reference count on unloading this
	 * shared firmware buffer.
	 *
	 * This is NULL when firmware is loaded in legacy mode.
	 */
	struct edgetpu_shared_fw_buffer *shared_buf;
};

struct edgetpu_firmware_private {
	const struct edgetpu_firmware_handlers *handlers;
	void *data; /* for edgetpu_firmware_(set/get)_data */

	struct mutex fw_desc_lock;
	struct edgetpu_firmware_desc fw_desc;
	enum edgetpu_firmware_status status;
	enum edgetpu_fw_flavor fw_flavor;
};

void edgetpu_firmware_set_data(struct edgetpu_firmware *et_fw, void *data)
{
	et_fw->p->data = data;
}

void *edgetpu_firmware_get_data(struct edgetpu_firmware *et_fw)
{
	return et_fw->p->data;
}

static int edgetpu_firmware_legacy_load_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc, const char *name)
{
	int ret;
	struct edgetpu_dev *etdev = et_fw->etdev;
	struct device *dev = etdev->dev;
	const struct firmware *fw;
	size_t aligned_size;

	ret = request_firmware(&fw, name, dev);
	if (ret) {
		etdev_dbg(etdev,
			  "%s: request '%s' failed: %d\n", __func__, name, ret);
		return ret;
	}

	aligned_size = ALIGN(fw->size, fw_desc->buf.used_size_align);
	if (aligned_size > fw_desc->buf.alloc_size) {
		etdev_dbg(etdev,
			  "%s: firmware buffer too small: alloc size=0x%zx, required size=0x%zx\n",
			  __func__, fw_desc->buf.alloc_size, aligned_size);
		ret = -ENOSPC;
		goto out_release_firmware;
	}

	memcpy(fw_desc->buf.vaddr, fw->data, fw->size);
	fw_desc->buf.used_size = aligned_size;
	fw_desc->buf.name = kstrdup(name, GFP_KERNEL);

out_release_firmware:
	release_firmware(fw);
	return ret;
}

static void edgetpu_firmware_legacy_unload_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc)
{
	kfree(fw_desc->buf.name);
	fw_desc->buf.name = NULL;
	fw_desc->buf.used_size = 0;
}

static int edgetpu_firmware_shared_load_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc, const char *name)
{
	int ret;
	struct edgetpu_dev *etdev = et_fw->etdev;
	struct edgetpu_shared_fw_buffer *shared_buf;

	shared_buf = edgetpu_shared_fw_load(name, etdev);
	if (IS_ERR(shared_buf)) {
		ret = PTR_ERR(shared_buf);
		etdev_dbg(etdev, "shared buffer loading failed: %d\n", ret);
		return ret;
	}
	fw_desc->shared_buf = shared_buf;
	fw_desc->buf.vaddr = edgetpu_shared_fw_buffer_vaddr(shared_buf);
	fw_desc->buf.alloc_size = edgetpu_shared_fw_buffer_size(shared_buf);
	fw_desc->buf.used_size = fw_desc->buf.alloc_size;
	fw_desc->buf.name = edgetpu_shared_fw_buffer_name(shared_buf);
	return 0;
}

static void edgetpu_firmware_shared_unload_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc)
{
	fw_desc->buf.vaddr = NULL;
	fw_desc->buf.alloc_size = 0;
	fw_desc->buf.used_size = 0;
	fw_desc->buf.name = NULL;
	edgetpu_shared_fw_put(fw_desc->shared_buf);
	fw_desc->shared_buf = NULL;
}

static int edgetpu_firmware_do_load_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc, const char *name)
{
	/* Use shared firmware from host if not allocated a buffer space. */
	if (!fw_desc->buf.vaddr)
		return edgetpu_firmware_shared_load_locked(et_fw, fw_desc,
							   name);
	else
		return edgetpu_firmware_legacy_load_locked(et_fw, fw_desc,
							   name);
}

static void edgetpu_firmware_do_unload_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc)
{
	if (fw_desc->shared_buf)
		edgetpu_firmware_shared_unload_locked(et_fw, fw_desc);
	else
		edgetpu_firmware_legacy_unload_locked(et_fw, fw_desc);
}

static int edgetpu_firmware_load_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc, const char *name,
		enum edgetpu_firmware_flags flags)
{
	const struct edgetpu_firmware_handlers *handlers = et_fw->p->handlers;
	struct edgetpu_dev *etdev = et_fw->etdev;
	int ret;

	fw_desc->buf.flags = flags;

	if (handlers && handlers->alloc_buffer) {
		ret = handlers->alloc_buffer(et_fw, &fw_desc->buf);
		if (ret) {
			etdev_dbg(etdev, "handler alloc_buffer failed: %d\n",
				  ret);
			return ret;
		}
	}

	ret = edgetpu_firmware_do_load_locked(et_fw, fw_desc, name);
	if (ret) {
		etdev_dbg(etdev, "firmware request failed: %d\n", ret);
		goto out_free_buffer;
	}

	if (handlers && handlers->setup_buffer) {
		ret = handlers->setup_buffer(et_fw, &fw_desc->buf);
		if (ret) {
			etdev_dbg(etdev, "handler setup_buffer failed: %d\n",
				  ret);
			goto out_do_unload_locked;
		}
	}

	return 0;

out_do_unload_locked:
	edgetpu_firmware_do_unload_locked(et_fw, fw_desc);
out_free_buffer:
	if (handlers && handlers->free_buffer)
		handlers->free_buffer(et_fw, &fw_desc->buf);
	return ret;
}

static void edgetpu_firmware_unload_locked(
		struct edgetpu_firmware *et_fw,
		struct edgetpu_firmware_desc *fw_desc)
{
	const struct edgetpu_firmware_handlers *handlers = et_fw->p->handlers;

	/*
	 * Platform specific implementation for cleaning up allocated buffer.
	 */
	if (handlers && handlers->teardown_buffer)
		handlers->teardown_buffer(et_fw, &fw_desc->buf);
	edgetpu_firmware_do_unload_locked(et_fw, fw_desc);
	/*
	 * Platform specific implementation for freeing allocated buffer.
	 */
	if (handlers && handlers->free_buffer)
		handlers->free_buffer(et_fw, &fw_desc->buf);
}

static char *fw_flavor_str(enum edgetpu_fw_flavor fw_flavor)
{
	switch (fw_flavor) {
	case FW_FLAVOR_BL1:
		return "stage 2 bootloader";
	case FW_FLAVOR_SYSTEST:
		return "test";
	case FW_FLAVOR_PROD_DEFAULT:
		return "prod";
	case FW_FLAVOR_CUSTOM:
		return "custom";
	default:
	case FW_FLAVOR_UNKNOWN:
		return "unknown";
	}

	/* NOTREACHED */
	return "?";
}

static int edgetpu_firmware_handshake(struct edgetpu_firmware *et_fw)
{
	enum edgetpu_fw_flavor fw_flavor;
	struct edgetpu_firmware_buffer *fw_buf;

	/* Give the firmware some time to initialize */
	msleep(100);
	etdev_dbg(et_fw->etdev, "Detecting firmware flavor...");
	fw_flavor = edgetpu_kci_fw_flavor(et_fw->etdev->kci);
	if (fw_flavor < 0) {
		etdev_err(et_fw->etdev, "firmware handshake failed: %d",
			  fw_flavor);
		et_fw->p->status = FW_INVALID;
		et_fw->p->fw_flavor = FW_FLAVOR_UNKNOWN;
		return fw_flavor;
	}

	if (fw_flavor != FW_FLAVOR_BL1) {
		fw_buf = &et_fw->p->fw_desc.buf;
		etdev_info(et_fw->etdev, "loaded %s firmware%s",
			   fw_flavor_str(fw_flavor),
			   fw_buf->flags & FW_ONDEV ? " on device" : "");
	} else {
		etdev_dbg(et_fw->etdev, "loaded stage 2 bootloader");
	}
	et_fw->p->status = FW_VALID;
	et_fw->p->fw_flavor = fw_flavor;
	/* Hermosa second-stage bootloader doesn't implement log/trace */
	if (fw_flavor != FW_FLAVOR_BL1)
		edgetpu_telemetry_kci(et_fw->etdev);
	return 0;
}

int edgetpu_firmware_run_locked(struct edgetpu_firmware *et_fw,
				const char *name,
				enum edgetpu_firmware_flags flags)
{
	const struct edgetpu_firmware_handlers *handlers = et_fw->p->handlers;
	struct edgetpu_firmware_desc new_fw_desc;
	int ret;

	et_fw->p->status = FW_LOADING;

	memset(&new_fw_desc, 0, sizeof(new_fw_desc));
	ret = edgetpu_firmware_load_locked(et_fw, &new_fw_desc, name, flags);
	if (ret)
		return ret;

	etdev_dbg(et_fw->etdev, "run fw %s flags=0x%x", name, flags);
	if (handlers && handlers->prepare_run) {
		/* Note this may recursively call us to run BL1 */
		ret = handlers->prepare_run(et_fw, &new_fw_desc.buf);
		if (ret)
			goto out_unload_new_fw;
	}

	/*
	 * Previous firmware buffer is not used anymore when R52 runs on
	 * new firmware buffer. Unload this before et_fw->p->fw_buf is
	 * overwritten by new buffer information.
	 */
	edgetpu_firmware_unload_locked(et_fw, &et_fw->p->fw_desc);
	et_fw->p->fw_desc = new_fw_desc;

	return edgetpu_firmware_handshake(et_fw);

out_unload_new_fw:
	edgetpu_firmware_unload_locked(et_fw, &new_fw_desc);
	return ret;
}

int edgetpu_firmware_run(struct edgetpu_dev *etdev, const char *name,
			 enum edgetpu_firmware_flags flags)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		return -ENODEV;
	ret = edgetpu_firmware_lock(etdev);
	if (ret) {
		etdev_err(etdev, "%s: lock failed (%d)\n", __func__, ret);
		return ret;
	}
	/*
	 * Prevent platform-specific code from trying to run the previous
	 * firmware
	 */
	et_fw->p->status = FW_LOADING;
	etdev_dbg(et_fw->etdev, "Requesting power up for firmware run\n");
	ret = edgetpu_pm_get(etdev->pm);
	if (!ret)
		ret = edgetpu_firmware_run_locked(et_fw, name, flags);
	etdev->firmware = et_fw;
	edgetpu_pm_put(etdev->pm);
	edgetpu_firmware_unlock(etdev);
	return ret;
}

int edgetpu_firmware_lock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw) {
		etdev_err(
			etdev,
			"Cannot load firmware when no loader is available\n");
		return -EINVAL;
	}
	mutex_lock(&et_fw->p->fw_desc_lock);

	/* Disallow group join while loading, fail if already joined */
	if (!edgetpu_set_group_join_lockout(etdev, true)) {
		etdev_err(
			etdev,
			"Cannot load firmware because device is in use");
		mutex_unlock(&et_fw->p->fw_desc_lock);
		return -EBUSY;
	}
	return 0;
}

void edgetpu_firmware_unlock(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw) {
		etdev_dbg(etdev,
			  "Unlock firmware when no loader available\n");
		return;
	}
	edgetpu_set_group_join_lockout(etdev, false);
	mutex_unlock(&et_fw->p->fw_desc_lock);
}

enum edgetpu_firmware_status
edgetpu_firmware_status_locked(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;

	if (!et_fw)
		return FW_INVALID;
	return et_fw->p->status;
}

int edgetpu_firmware_restart_locked(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	const struct edgetpu_firmware_handlers *handlers = et_fw->p->handlers;
	int ret;

	et_fw->p->status = FW_LOADING;
	if (handlers && handlers->prepare_run) {
		ret = handlers->prepare_run(et_fw, &et_fw->p->fw_desc.buf);
		if (ret)
			return ret;
	}
	return edgetpu_firmware_handshake(et_fw);
}

static ssize_t load_firmware_show(
		struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;
	const char *fw_name;

	if (!et_fw)
		return -ENODEV;

	mutex_lock(&et_fw->p->fw_desc_lock);
	fw_name = et_fw->p->fw_desc.buf.name;
	if (fw_name)
		ret = scnprintf(buf, PAGE_SIZE, "%s\n", fw_name);
	else
		ret = -ENODATA;
	mutex_unlock(&et_fw->p->fw_desc_lock);
	return ret;
}

static ssize_t load_firmware_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;
	char *name;

	if (!et_fw)
		return -ENODEV;

	name = edgetpu_fwutil_name_from_attr_buf(buf);
	if (IS_ERR(name))
		return PTR_ERR(name);

	etdev_info(etdev, "loading firmware %s\n", name);
	ret = edgetpu_chip_firmware_run(etdev, name, 0);

	kfree(name);

	if (ret)
		return ret;
	return count;
}

static DEVICE_ATTR_RW(load_firmware);

static ssize_t firmware_type_show(
		struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct edgetpu_dev *etdev = dev_get_drvdata(dev);
	struct edgetpu_firmware *et_fw = etdev->firmware;
	int ret;

	if (!et_fw)
		return -ENODEV;
	ret = scnprintf(buf, PAGE_SIZE, "%s\n",
			fw_flavor_str(et_fw->p->fw_flavor));
	return ret;
}
static DEVICE_ATTR_RO(firmware_type);

static struct attribute *dev_attrs[] = {
	&dev_attr_load_firmware.attr,
	&dev_attr_firmware_type.attr,
	NULL,
};

static const struct attribute_group edgetpu_firmware_attr_group = {
	.attrs = dev_attrs,
};

int edgetpu_firmware_create(struct edgetpu_dev *etdev,
			    const struct edgetpu_firmware_handlers *handlers)
{
	struct edgetpu_firmware *et_fw;
	int ret;

	if (etdev->firmware)
		return -EBUSY;

	et_fw = kzalloc(sizeof(*et_fw), GFP_KERNEL);
	if (!et_fw)
		return -ENOMEM;
	et_fw->etdev = etdev;

	et_fw->p = kzalloc(sizeof(*et_fw->p), GFP_KERNEL);
	if (!et_fw->p) {
		ret = -ENOMEM;
		goto out_kfree_et_fw;
	}
	et_fw->p->handlers = handlers;

	mutex_init(&et_fw->p->fw_desc_lock);

	ret = device_add_group(etdev->dev, &edgetpu_firmware_attr_group);
	if (ret)
		goto out_kfree_et_fw_p;

	if (handlers && handlers->after_create) {
		ret = handlers->after_create(et_fw);
		if (ret) {
			etdev_dbg(etdev,
				  "%s: after create handler failed: %d\n",
				  __func__, ret);
			goto out_device_remove_group;
		}
	}

	etdev->firmware = et_fw;
	return 0;

out_device_remove_group:
	device_remove_group(etdev->dev, &edgetpu_firmware_attr_group);
out_kfree_et_fw_p:
	kfree(et_fw->p);
out_kfree_et_fw:
	kfree(et_fw);
	return ret;
}

void edgetpu_firmware_destroy(struct edgetpu_dev *etdev)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	const struct edgetpu_firmware_handlers *handlers;

	if (!et_fw)
		return;

	if (et_fw->p) {
		handlers = et_fw->p->handlers;
		/*
		 * Platform specific implementation, which includes stop
		 * running firmware.
		 */
		if (handlers && handlers->before_destroy)
			handlers->before_destroy(et_fw);
	}

	device_remove_group(etdev->dev, &edgetpu_firmware_attr_group);

	if (et_fw->p) {
		mutex_lock(&et_fw->p->fw_desc_lock);
		edgetpu_firmware_unload_locked(et_fw, &et_fw->p->fw_desc);
		mutex_unlock(&et_fw->p->fw_desc_lock);
	}

	etdev->firmware = NULL;

	kfree(et_fw->p);
	kfree(et_fw);
}

/* debugfs mappings dump */
void edgetpu_firmware_mappings_show(struct edgetpu_dev *etdev,
				    struct seq_file *s)
{
	struct edgetpu_firmware *et_fw = etdev->firmware;
	struct edgetpu_firmware_buffer *fw_buf;
	phys_addr_t fw_iova_target;
	unsigned long iova;

	if (!et_fw)
		return;
	fw_buf = &et_fw->p->fw_desc.buf;
	if (!fw_buf->vaddr)
		return;
	fw_iova_target = fw_buf->dram_tpa ? fw_buf->dram_tpa : fw_buf->dma_addr;
	iova = edgetpu_chip_firmware_iova(etdev);
	seq_printf(s, "  0x%lx %lu fw - %pad %s\n", iova,
		   fw_buf->alloc_size / PAGE_SIZE, &fw_iova_target,
		   fw_buf->flags & FW_ONDEV ? "dev" : "");
}
