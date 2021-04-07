// SPDX-License-Identifier: GPL-2.0
/*
 * Common support functions for Edge TPU ML accelerator host-side ops.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <asm/current.h>
#include <asm/page.h>
#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uidgid.h>

#include "edgetpu-config.h"
#include "edgetpu-debug-dump.h"
#include "edgetpu-device-group.h"
#include "edgetpu-dram.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mcp.h"
#include "edgetpu-mmu.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-usage-stats.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"

static atomic_t single_dev_count = ATOMIC_INIT(-1);

static int edgetpu_mmap_full_csr(struct edgetpu_client *client,
				 struct vm_area_struct *vma)
{
	int ret;
	ulong phys_base, vma_size, map_size;

	if (!uid_eq(current_euid(), GLOBAL_ROOT_UID))
		return -EPERM;
	vma_size = vma->vm_end - vma->vm_start;
	map_size = min(vma_size, client->reg_window.size);
	phys_base = client->etdev->regs.phys +
		client->reg_window.start_reg_offset;
	ret = io_remap_pfn_range(vma, vma->vm_start, phys_base >> PAGE_SHIFT,
				 map_size, vma->vm_page_prot);
	if (ret)
		etdev_dbg(client->etdev,
			  "Error remapping PFN range: %d\n", ret);
	return ret;
}

/*
 * Returns the wakelock event by mmap offset. Returns EDGETPU_WAKELOCK_EVENT_END
 * if the offset does not correspond to a wakelock event.
 */
static enum edgetpu_wakelock_event mmap_wakelock_event(unsigned long pgoff)
{
	switch (pgoff) {
	case 0:
		return EDGETPU_WAKELOCK_EVENT_FULL_CSR;
	case EDGETPU_MMAP_CSR_OFFSET >> PAGE_SHIFT:
		return EDGETPU_WAKELOCK_EVENT_MBOX_CSR;
	case EDGETPU_MMAP_CMD_QUEUE_OFFSET >> PAGE_SHIFT:
		return EDGETPU_WAKELOCK_EVENT_CMD_QUEUE;
	case EDGETPU_MMAP_RESP_QUEUE_OFFSET >> PAGE_SHIFT:
		return EDGETPU_WAKELOCK_EVENT_RESP_QUEUE;
	default:
		return EDGETPU_WAKELOCK_EVENT_END;
	}
}

static void edgetpu_vma_open(struct vm_area_struct *vma)
{
	struct edgetpu_client *client = vma->vm_private_data;
	enum edgetpu_wakelock_event evt = mmap_wakelock_event(vma->vm_pgoff);

	if (evt != EDGETPU_WAKELOCK_EVENT_END)
		edgetpu_wakelock_inc_event(client->wakelock, evt);
}

static void edgetpu_vma_close(struct vm_area_struct *vma)
{
	struct edgetpu_client *client = vma->vm_private_data;
	enum edgetpu_wakelock_event evt = mmap_wakelock_event(vma->vm_pgoff);
	struct edgetpu_dev *etdev = client->etdev;

	if (evt != EDGETPU_WAKELOCK_EVENT_END)
		edgetpu_wakelock_dec_event(client->wakelock, evt);

	/* TODO(b/184613387): check whole VMA range instead of the start only */
	if (vma->vm_pgoff == EDGETPU_MMAP_LOG_BUFFER_OFFSET >> PAGE_SHIFT)
		edgetpu_munmap_telemetry_buffer(etdev, EDGETPU_TELEMETRY_LOG,
						vma);
	else if (vma->vm_pgoff ==
		 EDGETPU_MMAP_TRACE_BUFFER_OFFSET >> PAGE_SHIFT)
		edgetpu_munmap_telemetry_buffer(etdev, EDGETPU_TELEMETRY_TRACE,
						vma);
}

static const struct vm_operations_struct edgetpu_vma_ops = {
	.open = edgetpu_vma_open,
	.close = edgetpu_vma_close,
};

/* Map exported device CSRs or queue into user space. */
int edgetpu_mmap(struct edgetpu_client *client, struct vm_area_struct *vma)
{
	int ret = 0;
	enum edgetpu_wakelock_event evt;

	if (vma->vm_start & ~PAGE_MASK) {
		etdev_dbg(client->etdev,
			  "Base address not page-aligned: 0x%lx\n",
			  vma->vm_start);
		return -EINVAL;
	}

	etdev_dbg(client->etdev, "%s: mmap pgoff = %lX\n", __func__,
		  vma->vm_pgoff);

	vma->vm_private_data = client;
	vma->vm_ops = &edgetpu_vma_ops;

	/* Mark the VMA's pages as uncacheable. */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* map all CSRs for debug purpose */
	if (!vma->vm_pgoff) {
		evt = EDGETPU_WAKELOCK_EVENT_FULL_CSR;
		if (edgetpu_wakelock_inc_event(client->wakelock, evt)) {
			ret = edgetpu_mmap_full_csr(client, vma);
			if (ret)
				edgetpu_wakelock_dec_event(client->wakelock,
							   evt);
		} else {
			ret = -EAGAIN;
		}
		return ret;
	}

	/* Allow mapping log and telemetry buffers without creating a group */
	if (vma->vm_pgoff == EDGETPU_MMAP_LOG_BUFFER_OFFSET >> PAGE_SHIFT)
		return edgetpu_mmap_telemetry_buffer(
			client->etdev, EDGETPU_TELEMETRY_LOG, vma);
	if (vma->vm_pgoff == EDGETPU_MMAP_TRACE_BUFFER_OFFSET >> PAGE_SHIFT)
		return edgetpu_mmap_telemetry_buffer(
			client->etdev, EDGETPU_TELEMETRY_TRACE, vma);

	evt = mmap_wakelock_event(vma->vm_pgoff);
	if (evt == EDGETPU_WAKELOCK_EVENT_END)
		return -EINVAL;
	if (!edgetpu_wakelock_inc_event(client->wakelock, evt))
		return -EAGAIN;

	mutex_lock(&client->group_lock);
	if (!client->group) {
		ret = -EINVAL;
		goto out_unlock;
	}
	switch (vma->vm_pgoff) {
	case EDGETPU_MMAP_CSR_OFFSET >> PAGE_SHIFT:
		ret = edgetpu_mmap_csr(client->group, vma);
		break;
	case EDGETPU_MMAP_CMD_QUEUE_OFFSET >> PAGE_SHIFT:
		ret = edgetpu_mmap_queue(client->group, MAILBOX_CMD_QUEUE, vma);
		break;
	case EDGETPU_MMAP_RESP_QUEUE_OFFSET >> PAGE_SHIFT:
		ret = edgetpu_mmap_queue(client->group, MAILBOX_RESP_QUEUE,
					 vma);
		break;
	}
out_unlock:
	mutex_unlock(&client->group_lock);
	if (ret)
		edgetpu_wakelock_dec_event(client->wakelock, evt);
	return ret;
}

static struct edgetpu_mailbox_manager_desc mailbox_manager_desc = {
	.num_mailbox = EDGETPU_NUM_MAILBOXES,
	.num_vii_mailbox = EDGETPU_NUM_VII_MAILBOXES,
	.num_p2p_mailbox = EDGETPU_NUM_P2P_MAILBOXES,
	.get_context_csr_base = edgetpu_mailbox_get_context_csr_base,
	.get_cmd_queue_csr_base = edgetpu_mailbox_get_cmd_queue_csr_base,
	.get_resp_queue_csr_base = edgetpu_mailbox_get_resp_queue_csr_base,
};

int edgetpu_get_state_errno_locked(struct edgetpu_dev *etdev)
{
	switch (etdev->state) {
	case ETDEV_STATE_BAD:
		return -ENODEV;
	case ETDEV_STATE_FWLOADING:
		return -EAGAIN;
	case ETDEV_STATE_NOFW:
		return -EINVAL;
	default:
		break;
	}
	return 0;
}

int edgetpu_device_add(struct edgetpu_dev *etdev,
		       const struct edgetpu_mapped_resource *regs)
{
	int ret;

	etdev->regs = *regs;

	/* mcp_id and mcp_die_index fields set by caller */
	if (etdev->mcp_id < 0) {
		uint ordinal_id = atomic_add_return(1, &single_dev_count);

		if (!ordinal_id)
			snprintf(etdev->dev_name, EDGETPU_DEVICE_NAME_MAX, "%s",
				 DRIVER_NAME);
		else
			snprintf(etdev->dev_name, EDGETPU_DEVICE_NAME_MAX,
				 "%s.%u", DRIVER_NAME, ordinal_id);
	} else {
		snprintf(etdev->dev_name, EDGETPU_DEVICE_NAME_MAX,
			 "%s.%u.%u", DRIVER_NAME, etdev->mcp_id,
			 etdev->mcp_die_index);
	}

	mutex_init(&etdev->open.lock);
	mutex_init(&etdev->groups_lock);
	INIT_LIST_HEAD(&etdev->groups);
	etdev->n_groups = 0;
	etdev->group_join_lockout = false;
	mutex_init(&etdev->state_lock);
	etdev->state = ETDEV_STATE_NOFW;

	ret = edgetpu_fs_add(etdev);
	if (ret) {
		dev_err(etdev->dev, "%s: edgetpu_fs_add returns %d\n",
			etdev->dev_name, ret);
		return ret;
	}

	etdev->mailbox_manager =
		edgetpu_mailbox_create_mgr(etdev, &mailbox_manager_desc);
	if (IS_ERR(etdev->mailbox_manager)) {
		ret = PTR_ERR(etdev->mailbox_manager);
		dev_err(etdev->dev,
			"%s: edgetpu_mailbox_create_mgr returns %d\n",
			etdev->dev_name, ret);
		goto remove_dev;
	}
	edgetpu_setup_mmu(etdev);

	edgetpu_usage_stats_init(etdev);

	etdev->kci = devm_kzalloc(etdev->dev, sizeof(*etdev->kci), GFP_KERNEL);
	if (!etdev->kci) {
		ret = -ENOMEM;
		goto detach_mmu;
	}

	etdev->telemetry =
		devm_kzalloc(etdev->dev, sizeof(*etdev->telemetry), GFP_KERNEL);
	if (!etdev->telemetry) {
		ret = -ENOMEM;
		goto detach_mmu;
	}

	ret = edgetpu_kci_init(etdev->mailbox_manager, etdev->kci);
	if (ret) {
		etdev_err(etdev, "edgetpu_kci_init returns %d\n", ret);
		goto detach_mmu;
	}

	ret = edgetpu_device_dram_init(etdev);
	if (ret) {
		etdev_err(etdev,
			  "failed to init on-device DRAM management: %d\n",
			  ret);
		goto remove_kci;
	}

	ret = edgetpu_debug_dump_init(etdev);
	if (ret)
		etdev_warn(etdev, "debug dump init fail: %d", ret);

	edgetpu_chip_init(etdev);
	return 0;

remove_kci:
	/* releases the resources of KCI */
	edgetpu_mailbox_remove_all(etdev->mailbox_manager);
detach_mmu:
	edgetpu_usage_stats_exit(etdev);
	edgetpu_mmu_detach(etdev);
remove_dev:
	edgetpu_mark_probe_fail(etdev);
	edgetpu_fs_remove(etdev);
	return ret;
}

void edgetpu_device_remove(struct edgetpu_dev *etdev)
{
	edgetpu_chip_exit(etdev);
	edgetpu_debug_dump_exit(etdev);
	edgetpu_device_dram_exit(etdev);
	edgetpu_mailbox_remove_all(etdev->mailbox_manager);
	edgetpu_usage_stats_exit(etdev);
	edgetpu_mmu_detach(etdev);
	edgetpu_fs_remove(etdev);
}

struct edgetpu_client *edgetpu_client_add(struct edgetpu_dev *etdev)
{
	struct edgetpu_client *client;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return ERR_PTR(-ENOMEM);
	client->wakelock = edgetpu_wakelock_alloc(etdev);
	if (!client->wakelock) {
		kfree(client);
		return ERR_PTR(-ENOMEM);
	}

	/* Allow entire CSR space to be mmap()'ed using 1.0 interface */
	client->reg_window.start_reg_offset = 0;
	client->reg_window.size = etdev->regs.size;
	client->pid = current->pid;
	client->tgid = current->tgid;
	client->etdev = etdev;
	mutex_init(&client->group_lock);
	/* equivalent to edgetpu_client_get() */
	refcount_set(&client->count, 1);
	client->perdie_events = 0;
	return client;
}

struct edgetpu_client *edgetpu_client_get(struct edgetpu_client *client)
{
	WARN_ON_ONCE(!refcount_inc_not_zero(&client->count));
	return client;
}

void edgetpu_client_put(struct edgetpu_client *client)
{
	if (!client)
		return;
	if (refcount_dec_and_test(&client->count))
		kfree(client);
}

void edgetpu_client_remove(struct edgetpu_client *client)
{
	struct edgetpu_dev *etdev;

	if (IS_ERR_OR_NULL(client))
		return;
	etdev = client->etdev;
	/*
	 * A quick check without holding client->group_lock.
	 *
	 * If client doesn't belong to a group then we are fine to not proceed.
	 * If there is a race that the client belongs to a group but is removing
	 * by another process - this will be detected by the check with holding
	 * client->group_lock later.
	 */
	if (client->group)
		edgetpu_device_group_leave(client);
	edgetpu_wakelock_free(client->wakelock);
	/*
	 * It should be impossible to access client->wakelock after this cleanup
	 * procedure. Set to NULL to cause kernel panic if use-after-free does
	 * happen.
	 */
	client->wakelock = NULL;

	/* Clean up all the per die event fds registered by the client */
	if (client->perdie_events &
	    1 << perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_LOGS_AVAILABLE))
		edgetpu_telemetry_unset_event(etdev, EDGETPU_TELEMETRY_LOG);
	if (client->perdie_events &
	    1 << perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_TRACES_AVAILABLE))
		edgetpu_telemetry_unset_event(etdev, EDGETPU_TELEMETRY_TRACE);

	edgetpu_client_put(client);
}

int edgetpu_register_irq(struct edgetpu_dev *etdev, int irq)
{
	int ret;

	ret = devm_request_irq(etdev->dev, irq, edgetpu_chip_irq_handler,
			       IRQF_ONESHOT, etdev->dev_name, etdev);
	if (ret)
		dev_err(etdev->dev, "%s: failed to request irq %d: %d\n",
			etdev->dev_name, irq, ret);
	return ret;
}

void edgetpu_unregister_irq(struct edgetpu_dev *etdev, int irq)
{
	devm_free_irq(etdev->dev, irq, etdev);
}

int edgetpu_alloc_coherent(struct edgetpu_dev *etdev, size_t size,
			   struct edgetpu_coherent_mem *mem,
			   enum edgetpu_context_id context_id)
{
	const u32 flags = EDGETPU_MMU_DIE | EDGETPU_MMU_32 | EDGETPU_MMU_HOST;

	mem->vaddr = dma_alloc_coherent(etdev->dev, size, &mem->dma_addr,
					GFP_KERNEL);
	if (!mem->vaddr)
		return -ENOMEM;
	edgetpu_x86_coherent_mem_init(mem);
	mem->tpu_addr =
		edgetpu_mmu_tpu_map(etdev, mem->dma_addr, size,
				    DMA_BIDIRECTIONAL, context_id, flags);
	if (!mem->tpu_addr) {
		dma_free_coherent(etdev->dev, size, mem->vaddr, mem->dma_addr);
		mem->vaddr = NULL;
		return -EINVAL;
	}
	mem->size = size;
	return 0;
}

void edgetpu_free_coherent(struct edgetpu_dev *etdev,
			   struct edgetpu_coherent_mem *mem,
			   enum edgetpu_context_id context_id)
{
	edgetpu_mmu_tpu_unmap(etdev, mem->tpu_addr, mem->size, context_id);
	edgetpu_x86_coherent_mem_set_wb(mem);
	dma_free_coherent(etdev->dev, mem->size, mem->vaddr, mem->dma_addr);
	mem->vaddr = NULL;
}

void edgetpu_handle_firmware_crash(struct edgetpu_dev *etdev, u16 crash_type,
				   u32 extra_info)
{
	etdev_err(etdev, "firmware crashed: %u 0x%x", crash_type, extra_info);
	etdev->firmware_crash_count++;
	edgetpu_fatal_error_notify(etdev);
}

int __init edgetpu_init(void)
{
	int ret;

	ret = edgetpu_fs_init();
	if (ret)
		return ret;
	edgetpu_mcp_init();
	return 0;
}

void __exit edgetpu_exit(void)
{
	edgetpu_mcp_exit();
	edgetpu_fs_exit();
}
