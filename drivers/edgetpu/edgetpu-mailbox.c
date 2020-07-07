// SPDX-License-Identifier: GPL-2.0
/*
 * Utility functions of mailbox protocol for Edge TPU ML accelerator.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#ifdef CONFIG_X86
#include <linux/printk.h>
#include <asm/set_memory.h>
#endif
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "edgetpu-device-group.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mmu.h"
#include "edgetpu.h"

/*
 * Checks if @size is a valid circular queue size, which should be a positive
 * number and less than or equal to MAX_QUEUE_SIZE.
 */
static inline bool valid_circular_queue_size(u32 size)
{
	if (!size || size > MAX_QUEUE_SIZE)
		return false;
	return true;
}

/* Sets mailbox->cmd_queue_tail and corresponding CSR on device. */
static void edgetpu_mailbox_set_cmd_queue_tail(struct edgetpu_mailbox *mailbox,
					       u32 value)
{
	mailbox->cmd_queue_tail = value;
	EDGETPU_MAILBOX_CMD_QUEUE_WRITE_SYNC(mailbox, tail, value);
}

/* Sets mailbox->resp_queue_head and corresponding CSR on device. */
static void edgetpu_mailbox_set_resp_queue_head(struct edgetpu_mailbox *mailbox,
						u32 value)
{
	mailbox->resp_queue_head = value;
	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, head, value);
}

/*
 * Allocates and returns a mailbox given the index of this mailbox,
 * also enables the mailbox.
 *
 * Caller holds mgr->mailboxes_lock.
 */
static struct edgetpu_mailbox *edgetpu_mailbox_create_locked(
		struct edgetpu_mailbox_manager *mgr,
		uint index)
{
	struct edgetpu_mailbox *mailbox = kzalloc(sizeof(*mailbox), GFP_ATOMIC);

	if (!mailbox)
		return ERR_PTR(-ENOMEM);
	mailbox->mailbox_id = index;
	mailbox->etdev = mgr->etdev;
	mailbox->context_csr_base = mgr->get_context_csr_base(index);
	mailbox->cmd_queue_csr_base = mgr->get_cmd_queue_csr_base(index);
	mailbox->resp_queue_csr_base = mgr->get_resp_queue_csr_base(index);
	edgetpu_mailbox_init_doorbells(mailbox);

	return mailbox;
}

/* Disables a mailbox via setting CSR. */
static void edgetpu_mailbox_disable(struct edgetpu_mailbox *mailbox)
{
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, context_enable, 0);
}

/*
 * Disables the @index-th mailbox via setting CSR. Doesn't need
 * @mgr->mailboxes[index] be allocated.
 */
static void edgetpu_mailbox_disable_ith(struct edgetpu_mailbox_manager *mgr,
					uint index)
{
	struct edgetpu_mailbox mbox = {
		.etdev = mgr->etdev,
		.context_csr_base = mgr->get_context_csr_base(index),
	};

	edgetpu_mailbox_disable(&mbox);
}

static void edgetpu_vii_irq_handler(struct edgetpu_mailbox *mailbox)
{
	edgetpu_group_notify(mailbox->internal.group, EDGETPU_EVENT_RESPDATA);
}

/*
 * Increases the command queue tail by @inc.
 *
 * The queue uses the mirrored circular buffer arrangement. Each index (head and
 * tail) has a wrap bit, represented by the constant CIRCULAR_QUEUE_WRAP_BIT.
 * Whenever an index is increased and will exceed the end of the queue, the wrap
 * bit is xor-ed.
 *
 * This method will update both mailbox->cmd_queue_tail and CSR on device.
 *
 * Returns 0 on success.
 * If command queue tail will exceed command queue head after adding @inc,
 * -EBUSY is returned and all fields are remain unchanged. The caller should
 * handle this case and implement a mechanism to wait until the consumer
 * consumes commands.
 */
int edgetpu_mailbox_inc_cmd_queue_tail(struct edgetpu_mailbox *mailbox,
				       u32 inc)
{
	u32 head;
	u32 remain_size;
	u32 new_tail;

	if (inc > mailbox->cmd_queue_size)
		return -EINVAL;

	head = EDGETPU_MAILBOX_CMD_QUEUE_READ(mailbox, head);
	remain_size = mailbox->cmd_queue_size -
		      circular_queue_count(head, mailbox->cmd_queue_tail,
					   mailbox->cmd_queue_size);
	/* no enough space left */
	if (inc > remain_size)
		return -EBUSY;

	new_tail = circular_queue_inc(mailbox->cmd_queue_tail, inc,
				      mailbox->cmd_queue_size);
	edgetpu_mailbox_set_cmd_queue_tail(mailbox, new_tail);
	return 0;
}

/*
 * Increases the response queue head by @inc.
 *
 * The queue uses the mirrored circular buffer arrangement. Each index (head and
 * tail) has a wrap bit, represented by the constant CIRCULAR_QUEUE_WRAP_BIT.
 * Whenever an index is increased and will exceed the end of the queue, the wrap
 * bit is xor-ed.
 *
 * This method will update both mailbox->resp_queue_head and CSR on device.
 *
 * Returns 0 on success.
 * -EINVAL is returned if the queue head will exceed tail of queue, and no
 * fields or CSR is updated in this case.
 */
int edgetpu_mailbox_inc_resp_queue_head(struct edgetpu_mailbox *mailbox,
					u32 inc)
{
	u32 tail;
	u32 size;
	u32 new_head;

	if (inc > mailbox->resp_queue_size)
		return -EINVAL;

	tail = EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, tail);
	size = circular_queue_count(mailbox->resp_queue_head, tail,
				    mailbox->resp_queue_size);
	if (inc > size)
		return -EINVAL;
	new_head = circular_queue_inc(mailbox->resp_queue_head, inc,
				      mailbox->resp_queue_size);
	edgetpu_mailbox_set_resp_queue_head(mailbox, new_head);

	return 0;
}

/*
 * Sets address and size of queue.
 *
 * Sets the queue address with @addr, a 36-bit address, and with size @size in
 * units of number of elements.
 *
 * Returns 0 on success.
 * -EINVAL is returned if @addr or @size is invalid.
 */
int edgetpu_mailbox_set_queue(struct edgetpu_mailbox *mailbox,
			      enum mailbox_queue_type type, u64 addr, u32 size)
{
	u32 low = addr & 0xffffffff;
	u32 high = addr >> 32;

	if (!valid_circular_queue_size(size))
		return -EINVAL;
	/* addr is a 36-bit address, checks if the higher bits are clear */
	if (high & 0xfffffff0)
		return -EINVAL;

	switch (type) {
	case MAILBOX_CMD_QUEUE:
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_address_low,
					      low);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_address_high,
					      high);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_size, size);
		mailbox->cmd_queue_size = size;
		edgetpu_mailbox_set_cmd_queue_tail(mailbox, 0);
		EDGETPU_MAILBOX_CMD_QUEUE_WRITE(mailbox, head, 0);
		break;
	case MAILBOX_RESP_QUEUE:
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_address_low,
					      low);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_address_high,
					      high);
		EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_size, size);
		mailbox->resp_queue_size = size;
		edgetpu_mailbox_set_resp_queue_head(mailbox, 0);
		EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, tail, 0);
		break;
	}

	return 0;
}

/* Reset mailbox queues, clear out any commands/responses left from before. */
void edgetpu_mailbox_reset(struct edgetpu_mailbox *mailbox)
{
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, context_enable, 0);
	EDGETPU_MAILBOX_CMD_QUEUE_WRITE(mailbox, head, 0);
	edgetpu_mailbox_set_cmd_queue_tail(mailbox, 0);
	edgetpu_mailbox_set_resp_queue_head(mailbox, 0);
	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, tail, 0);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, context_enable, 1);
}

/* Sets the priority of @mailbox. */
void edgetpu_mailbox_set_priority(struct edgetpu_mailbox *mailbox, u32 priority)
{
	/* TODO(b/137343013): check whether priority is valid */
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, priority, priority);
}

/*
 * Requests an available mailbox for VII.
 *
 * -EBUSY is returned if there is no mailbox available.
 */
struct edgetpu_mailbox *edgetpu_mailbox_vii_add(
		struct edgetpu_mailbox_manager *mgr)
{
	uint i;
	struct edgetpu_mailbox *mailbox = NULL;
	unsigned long flags;

	write_lock_irqsave(&mgr->mailboxes_lock, flags);
	/* Skip the mailboxes preserved for KCI */
	for (i = mgr->vii_index_from; i < mgr->vii_index_to; i++) {
		if (!mgr->mailboxes[i]) {
			mailbox = edgetpu_mailbox_create_locked(mgr, i);
			if (IS_ERR(mailbox))
				goto out;
			mgr->mailboxes[i] = mailbox;
			break;
		}
	}

	/* no empty slot found */
	if (!mailbox)
		mailbox = ERR_PTR(-EBUSY);
out:
	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);
	return mailbox;
}

/*
 * Requests the first @n P2P mailboxes, where @n should be the number of devices
 * in a virtual device group.
 *
 * The array of requested mailboxes will be assigned to @mailboxes on success.
 * @mailboxes must have dimension @n, @mailboxes[@skip_i] will be set to NULL.
 *
 * Returns 0 on success, or a negative errno on error.
 * Returns -EBUSY if any mailbox is using.
 */
int edgetpu_mailbox_p2p_batch(struct edgetpu_mailbox_manager *mgr, uint n,
			      uint skip_i, struct edgetpu_mailbox **mailboxes)
{
	uint i, j;
	int ret;
	struct edgetpu_mailbox *mailbox;
	unsigned long flags;

	if (mgr->p2p_index_to - mgr->p2p_index_from < n)
		return -EINVAL;

	write_lock_irqsave(&mgr->mailboxes_lock, flags);

	memset(mailboxes, 0, sizeof(*mailboxes) * n);
	for (i = mgr->p2p_index_from, j = 0; j < n; i++, j++) {
		if (mgr->mailboxes[i]) {
			ret = -EBUSY;
			goto release;
		}
	}

	for (i = mgr->p2p_index_from, j = 0; j < n; i++, j++) {
		if (j == skip_i) {
			edgetpu_mailbox_disable_ith(mgr, i);
			continue;
		}

		mailbox = edgetpu_mailbox_create_locked(mgr, i);
		if (IS_ERR(mailbox)) {
			ret = PTR_ERR(mailbox);
			goto release;
		}
		mailboxes[j] = mgr->mailboxes[i] = mailbox;
	}

	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);
	return 0;

release:
	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);

	for (i = 0; i < n; i++) {
		if (mailboxes[i])
			edgetpu_mailbox_remove(mgr, mailboxes[i]);
		mailboxes[i] = NULL;
	}

	return ret;
}

/*
 * Every mailbox manager can allocate one mailbox for KCI to use.
 * -EBUSY is returned if the KCI mailbox is allocated and hasn't been removed
 * via edgetpu_mailbox_remove().
 */
struct edgetpu_mailbox *edgetpu_mailbox_kci(struct edgetpu_mailbox_manager *mgr)
{
	struct edgetpu_mailbox *mailbox;
	unsigned long flags;

	write_lock_irqsave(&mgr->mailboxes_lock, flags);
	if (mgr->mailboxes[KERNEL_MAILBOX_INDEX]) {
		mailbox = ERR_PTR(-EBUSY);
		goto out;
	}

	mailbox = edgetpu_mailbox_create_locked(mgr, KERNEL_MAILBOX_INDEX);
	if (!IS_ERR(mailbox))
		mgr->mailboxes[KERNEL_MAILBOX_INDEX] = mailbox;

out:
	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);
	return mailbox;
}

/*
 * Removes a mailbox from the manager.
 * Returns 0 on success.
 */
int edgetpu_mailbox_remove(struct edgetpu_mailbox_manager *mgr,
			   struct edgetpu_mailbox *mailbox)
{
	unsigned long flags;

	write_lock_irqsave(&mgr->mailboxes_lock, flags);
	/* simple security checks */
	if (mailbox->mailbox_id >= mgr->num_mailbox ||
	    mgr->mailboxes[mailbox->mailbox_id] != mailbox) {
		write_unlock_irqrestore(&mgr->mailboxes_lock, flags);
		return -EINVAL;
	}

	mgr->mailboxes[mailbox->mailbox_id] = NULL;
	edgetpu_mailbox_disable(mailbox);
	/* KCI mailbox is a special case */
	if (mailbox->mailbox_id == KERNEL_MAILBOX_INDEX)
		edgetpu_kci_release(mgr->etdev, mailbox->internal.kci);
	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);
	kfree(mailbox);

	return 0;
}

/*
 * The queue size of edgetpu_mailbox_attr has units in KB, convert it to use the
 * element size here.
 *
 * Returns a negative errno on error, or the converted size.
 */
static int convert_runtime_queue_size_to_fw(u32 queue_size, u32 element_size)
{
	const u32 runtime_unit = 1024;

	/* zero size is not allowed */
	if (queue_size == 0)
		return -EINVAL;
	/* prevent integer overflow */
	if (queue_size > SIZE_MAX / runtime_unit)
		return -ENOMEM;
	return queue_size * runtime_unit / element_size;
}

/*
 * Sets mailbox and allocates queues to @vii.
 *
 * @group is the device group that @vii will be associated with.
 *
 * Returns 0 on success.
 * Returns -EINVAL if any fields in @attr is invalid.
 */
int edgetpu_mailbox_init_vii(struct edgetpu_vii *vii,
			     struct edgetpu_device_group *group,
			     const struct edgetpu_mailbox_attr *attr)
{
	int cmd_queue_size, resp_queue_size;
	struct edgetpu_mailbox_manager *mgr = group->etdev->mailbox_manager;
	struct edgetpu_mailbox *mailbox = edgetpu_mailbox_vii_add(mgr);
	int ret;

	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	cmd_queue_size = convert_runtime_queue_size_to_fw(
			attr->cmd_queue_size, EDGETPU_SIZEOF_VII_CMD_ELEMENT);
	if (cmd_queue_size < 0) {
		edgetpu_mailbox_remove(mgr, mailbox);
		return cmd_queue_size;
	}
	resp_queue_size = convert_runtime_queue_size_to_fw(
			attr->resp_queue_size, EDGETPU_SIZEOF_VII_RESP_ELEMENT);
	if (resp_queue_size < 0) {
		edgetpu_mailbox_remove(mgr, mailbox);
		return resp_queue_size;
	}

	edgetpu_mailbox_set_priority(mailbox, attr->priority);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox,
				      cmd_queue_tail_doorbell_enable,
				      attr->cmdq_tail_doorbell);

	ret = edgetpu_mailbox_alloc_queue(group->etdev, mailbox,
					  cmd_queue_size,
					  EDGETPU_SIZEOF_VII_CMD_ELEMENT,
					  MAILBOX_CMD_QUEUE,
					  &vii->cmd_queue_mem);
	if (ret) {
		edgetpu_mailbox_remove(mgr, mailbox);
		return ret;
	}

	etdev_dbg(group->etdev,
		  "%s: mbox %u cmdq iova=0x%llx dma=%pad\n",
		  __func__, mailbox->mailbox_id, vii->cmd_queue_mem.tpu_addr,
		  &vii->cmd_queue_mem.dma_addr);
	ret = edgetpu_mailbox_alloc_queue(group->etdev, mailbox,
					  resp_queue_size,
					  EDGETPU_SIZEOF_VII_RESP_ELEMENT,
					  MAILBOX_RESP_QUEUE,
					  &vii->resp_queue_mem);

	if (ret) {
		edgetpu_mailbox_free_queue(group->etdev, mailbox,
					   &vii->cmd_queue_mem);
		edgetpu_mailbox_remove(mgr, mailbox);
		return ret;
	}

	etdev_dbg(group->etdev,
		  "%s: mbox %u rspq iova=0x%llx dma=%pad\n",
		  __func__, mailbox->mailbox_id, vii->resp_queue_mem.tpu_addr,
		  &vii->resp_queue_mem.dma_addr);
	mailbox->internal.group = edgetpu_device_group_get(group);
	mailbox->handle_irq = edgetpu_vii_irq_handler;
	vii->mailbox = mailbox;
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, context_enable, 1);
	return 0;
}

void edgetpu_mailbox_remove_vii(struct edgetpu_vii *vii)
{
	struct edgetpu_dev *etdev;
	struct edgetpu_mailbox_manager *mgr;

	if (!vii->mailbox)
		return;
	etdev = vii->mailbox->internal.group->etdev;
	mgr = etdev->mailbox_manager;
	edgetpu_mailbox_free_queue(etdev, vii->mailbox, &vii->cmd_queue_mem);
	edgetpu_mailbox_free_queue(etdev, vii->mailbox, &vii->resp_queue_mem);
	edgetpu_device_group_put(vii->mailbox->internal.group);
	edgetpu_mailbox_remove(mgr, vii->mailbox);
	vii->mailbox = NULL;
}

/*
 * Allocates memory for a queue.
 *
 * The total size (in bytes) of queue is @queue_size * @unit.
 * CSRs of @mailbox include queue_size and queue_address will be set on success.
 * @mem->dma_addr, @mem->vaddr, and @mem->size will be set.
 *
 * Returns 0 on success, or a negative errno on error.
 */
int edgetpu_mailbox_alloc_queue(struct edgetpu_dev *etdev,
				struct edgetpu_mailbox *mailbox, u32 queue_size,
				u32 unit, enum mailbox_queue_type type,
				edgetpu_queue_mem *mem)
{
	u32 size = unit * queue_size;
	const u32 flags = EDGETPU_MMU_DIE | EDGETPU_MMU_32 | EDGETPU_MMU_HOST;
	int ret;

	/* checks integer overflow */
	if (queue_size > SIZE_MAX / unit)
		return -ENOMEM;

	/* Align queue size to page size for TPU MMU map. */
	size = __ALIGN_KERNEL(size, PAGE_SIZE);

	/*
	 * TODO(b/136515481):
	 * Check if allocating many VII + P2P mailbox queues in coherent memory
	 * is a problem.
	 */
	mem->vaddr = dma_alloc_coherent(etdev->dev, size, &mem->dma_addr,
					GFP_KERNEL);
	if (!mem->vaddr)
		return -ENOMEM;
#ifdef CONFIG_X86
	set_memory_uc((unsigned long)mem->vaddr, size >> PAGE_SHIFT);
#endif
	mem->tpu_addr =
		edgetpu_mmu_tpu_map(etdev, mem->dma_addr, size,
				    DMA_BIDIRECTIONAL,
				    edgetpu_mailbox_context_id(mailbox), flags);
	if (!mem->tpu_addr) {
#ifdef CONFIG_X86
		set_memory_wb((unsigned long)mem->vaddr, size >> PAGE_SHIFT);
#endif
		dma_free_coherent(etdev->dev, size, mem->vaddr, mem->dma_addr);
		mem->vaddr = NULL;
		return -EINVAL;
	}

	ret = edgetpu_mailbox_set_queue(mailbox, type, mem->tpu_addr,
					queue_size);
	if (ret) {
		edgetpu_mmu_tpu_unmap(etdev, mem->tpu_addr, size, 0);
#ifdef CONFIG_X86
		set_memory_wb((unsigned long)mem->vaddr, size >> PAGE_SHIFT);
#endif
		dma_free_coherent(etdev->dev, size, mem->vaddr, mem->dma_addr);
		mem->vaddr = NULL;
		return ret;
	}

	mem->size = size;
	return 0;
}

/*
 * Releases the queue memory previously allocated with
 * edgetpu_mailbox_alloc_queue().
 *
 * Does nothing if @mem->vaddr is NULL.
 */
void edgetpu_mailbox_free_queue(struct edgetpu_dev *etdev,
				struct edgetpu_mailbox *mailbox,
				edgetpu_queue_mem *mem)
{
	if (!mem->vaddr)
		return;

	edgetpu_mmu_tpu_unmap(etdev, mem->tpu_addr, mem->size,
			      edgetpu_mailbox_context_id(mailbox));
#ifdef CONFIG_X86
	set_memory_wb((unsigned long)mem->vaddr, mem->size >> PAGE_SHIFT);
#endif
	dma_free_coherent(etdev->dev, mem->size, mem->vaddr, mem->dma_addr);
	mem->vaddr = NULL;
}

/*
 * Creates a mailbox manager, one edgetpu device has one manager.
 */
struct edgetpu_mailbox_manager *edgetpu_mailbox_create_mgr(
		struct edgetpu_dev *etdev,
		const struct edgetpu_mailbox_manager_desc *desc)
{
	struct edgetpu_mailbox_manager *mgr;
	uint total = 0;

	total += 1; /* KCI mailbox */
	total += desc->num_vii_mailbox;
	total += desc->num_p2p_mailbox;
	if (total > desc->num_mailbox)
		return ERR_PTR(-EINVAL);
	mgr = devm_kzalloc(etdev->dev, sizeof(*mgr), GFP_KERNEL);
	if (!mgr)
		return ERR_PTR(-ENOMEM);

	mgr->etdev = etdev;
	mgr->num_mailbox = desc->num_mailbox;
	/* index 0 is reserved for KCI */
	mgr->vii_index_from = 1;
	mgr->vii_index_to = mgr->vii_index_from + desc->num_vii_mailbox;
	mgr->p2p_index_from = mgr->vii_index_to;
	mgr->p2p_index_to = mgr->p2p_index_from + desc->num_p2p_mailbox;

	mgr->get_context_csr_base = desc->get_context_csr_base;
	mgr->get_cmd_queue_csr_base = desc->get_cmd_queue_csr_base;
	mgr->get_resp_queue_csr_base = desc->get_resp_queue_csr_base;

	mgr->mailboxes = devm_kcalloc(etdev->dev, mgr->num_mailbox,
				      sizeof(*mgr->mailboxes), GFP_KERNEL);
	if (!mgr->mailboxes)
		return ERR_PTR(-ENOMEM);
	rwlock_init(&mgr->mailboxes_lock);

	return mgr;
}

/* All requested mailboxes will be disabled and freed. */
void edgetpu_mailbox_remove_all(struct edgetpu_mailbox_manager *mgr)
{
	uint i;
	struct edgetpu_mailbox *kci_mailbox = NULL;
	unsigned long flags;

	if (IS_ERR_OR_NULL(mgr))
		return;
	write_lock_irqsave(&mgr->mailboxes_lock, flags);
	for (i = 0; i < mgr->num_mailbox; i++) {
		struct edgetpu_mailbox *mailbox = mgr->mailboxes[i];

		if (mailbox) {
			edgetpu_mailbox_disable(mailbox);
			/* KCI needs special handling */
			if (i == KERNEL_MAILBOX_INDEX)
				kci_mailbox = mailbox;
			else
				kfree(mailbox);
			mgr->mailboxes[i] = NULL;
		}
	}
	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);

	/* Cancel KCI worker outside the lock, then free KCI mailbox. */
	if (kci_mailbox) {
		edgetpu_kci_release(mgr->etdev,
				    kci_mailbox->internal.kci);
		kfree(kci_mailbox);
	}
}

/*
 * The interrupt handler for mailboxes.
 *
 * This handler loops through mailboxes with an interrupt pending and invokes
 * their IRQ handlers.
 */
irqreturn_t edgetpu_mailbox_handle_irq(struct edgetpu_mailbox_manager *mgr)
{
	struct edgetpu_mailbox *mailbox;
	uint i;

	if (!mgr)
		return IRQ_NONE;

	read_lock(&mgr->mailboxes_lock);
	for (i = 0; i < mgr->num_mailbox; i++) {
		mailbox = mgr->mailboxes[i];
		if (!mailbox)
			continue;
		if (!EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, doorbell_status))
			continue;
		EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, doorbell_clear, 1);
		etdev_dbg(mgr->etdev, "mbox %u resp doorbell irq tail=%u\n",
			  i, EDGETPU_MAILBOX_RESP_QUEUE_READ(mailbox, tail));
		if (mailbox->handle_irq)
			mailbox->handle_irq(mailbox);
	}
	read_unlock(&mgr->mailboxes_lock);

	return IRQ_HANDLED;
}

void edgetpu_mailbox_init_doorbells(struct edgetpu_mailbox *mailbox)
{
	/* Clear any stale doorbells requested */
	EDGETPU_MAILBOX_RESP_QUEUE_WRITE(mailbox, doorbell_clear, 1);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_doorbell_clear, 1);
	/* Enable the command and response doorbell interrupts */
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, cmd_queue_doorbell_enable, 1);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, resp_queue_doorbell_enable, 1);
}

void edgetpu_mailbox_reset_vii(struct edgetpu_mailbox_manager *mgr)
{
	uint i;
	unsigned long flags;

	write_lock_irqsave(&mgr->mailboxes_lock, flags);
	for (i = mgr->vii_index_from; i < mgr->vii_index_to; i++) {
		struct edgetpu_mailbox mbox = {
			.etdev = mgr->etdev,
			.context_csr_base = mgr->get_context_csr_base(i),
		};

		edgetpu_mailbox_reset(&mbox);
		edgetpu_mailbox_disable(&mbox);
		edgetpu_mailbox_init_doorbells(&mbox);
	}
	write_unlock_irqrestore(&mgr->mailboxes_lock, flags);
}
