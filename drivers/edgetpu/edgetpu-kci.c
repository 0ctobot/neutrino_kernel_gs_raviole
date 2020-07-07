// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel Control Interface, implements the protocol between AP kernel and TPU
 * firmware.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <linux/device.h>
#include <linux/dma-mapping.h> /* dmam_alloc_coherent */
#include <linux/errno.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h> /* memcpy */

#include "edgetpu-firmware.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-mmu.h"
#include "edgetpu-telemetry.h"

/* the index of mailbox for kernel should always be zero */
#define KERNEL_MAILBOX_INDEX 0

/* size of queue for KCI mailbox */
#define QUEUE_SIZE MAX_QUEUE_SIZE

/* Timeout for KCI responses from the firmware (milliseconds) */
#ifdef CONFIG_EDGETPU_FPGA
/* Set extra ludicrously high to 60 seconds for (slow) Palladium emulation. */
#define KCI_TIMEOUT	(60000)
#else
/* 5 secs.  TODO(134408592): Define a timeout for TPU CPU responses  */
#define KCI_TIMEOUT	(5000)
#endif

static inline u32 edgetpu_kci_queue_element_size(enum mailbox_queue_type type)
{
	if (type == MAILBOX_CMD_QUEUE)
		return sizeof(struct edgetpu_command_element);
	else
		return sizeof(struct edgetpu_kci_response_element);
}

static void *edgetpu_kci_alloc_queue(struct edgetpu_dev *etdev,
				     struct edgetpu_mailbox *mailbox,
				     enum mailbox_queue_type type,
				     tpu_addr_t *tpu_addr, dma_addr_t *dma_addr)
{
	void *addr;
	u32 queue_size = QUEUE_SIZE;
	u32 size = queue_size * edgetpu_kci_queue_element_size(type);
	const u32 flags = EDGETPU_MMU_DIE | EDGETPU_MMU_32 | EDGETPU_MMU_HOST;
	int ret;

	addr = dmam_alloc_coherent(etdev->dev, size, dma_addr, GFP_KERNEL);
	if (!addr)
		return ERR_PTR(-ENOMEM);
	*tpu_addr =
		edgetpu_mmu_tpu_map(etdev, *dma_addr, size, DMA_BIDIRECTIONAL,
				    EDGETPU_CONTEXT_KCI, flags);
	if (!*tpu_addr) {
		etdev_err(etdev, "failed to map KCI queue to TPU");
		return ERR_PTR(-EINVAL);
	}
	ret = edgetpu_mailbox_set_queue(mailbox, type, (u64)*tpu_addr,
					queue_size);
	if (ret) {
		etdev_err(etdev, "failed to set mailbox queue: %d", ret);
		edgetpu_mmu_tpu_unmap(etdev, *tpu_addr, size,
				      EDGETPU_CONTEXT_KCI);
		return ERR_PTR(ret);
	}

	return addr;
}

/*
 * Pops the wait_list until the sequence number of @resp is found, and copies
 * @resp to the found entry.
 *
 * Both entry in wait_list and response handling should have sequence number in
 * increasing order.
 * Compare the #seq of head of wait_list with @resp->seq, we have three cases:
 * 1. #seq > @resp->seq:
 *   - Nothing to do, @resp is not needed and we're done.
 * 2. #seq == @resp->seq:
 *   - Copy @resp, pop the head and we're done.
 * 3. #seq < @resp->seq:
 *   - Should not happen, this implies the sequence number of either entries in
 *     wait_list or responses are out-of-order, or remote didn't respond to a
 *     command. In this case, the status of response will be set to
 *     KCI_STATUS_NO_RESPONSE.
 *   - Pop until case 1. or 2.
 */
static void edgetpu_kci_consume_wait_list(
		struct edgetpu_kci *kci,
		const struct edgetpu_kci_response_element *resp)
{
	struct edgetpu_kci_wait_list *cur, *nxt;

	mutex_lock(&kci->wait_list_lock);

	list_for_each_entry_safe(cur, nxt, &kci->wait_list, list) {
		if (cur->resp->seq > resp->seq)
			break;
		if (cur->resp->seq == resp->seq) {
			memcpy(cur->resp, resp, sizeof(*resp));
			list_del(&cur->list);
			kfree(cur);
			break;
		}
		/* #seq < @resp->seq */
		cur->resp->status = KCI_STATUS_NO_RESPONSE;
		list_del(&cur->list);
		kfree(cur);
	}

	mutex_unlock(&kci->wait_list_lock);
}

/* Handler of a response. */
static void edgetpu_kci_handle_response(
		struct edgetpu_kci *kci,
		const struct edgetpu_kci_response_element *resp)
{
	edgetpu_kci_consume_wait_list(kci, resp);
}

/*
 * Fetches elements in the response queue.
 *
 * Returns the pointer of fetched response elements.
 * @total_ptr will be the number of elements fetched.
 *
 * Returns -ENOMEM if failed on memory allocation.
 * Returns NULL if the response queue is empty.
 */
static struct edgetpu_kci_response_element *edgetpu_kci_fetch_responses(
		struct edgetpu_kci *kci, u32 *total_ptr)
{
	u32 head;
	u32 tail;
	u32 count;
	u32 i;
	u32 j;
	u32 total = 0;
	const u32 size = kci->mailbox->resp_queue_size;
	const struct edgetpu_kci_response_element *queue = kci->resp_queue;
	struct edgetpu_kci_response_element *ret = NULL;
	struct edgetpu_kci_response_element *prev_ptr = NULL;

	mutex_lock(&kci->resp_queue_lock);

	head = kci->mailbox->resp_queue_head;
	/* loop until our head equals to CSR tail */
	while (1) {
		tail = EDGETPU_MAILBOX_RESP_QUEUE_READ_SYNC(kci->mailbox, tail);
		count = circular_queue_count(head, tail, size);
		if (count == 0)
			break;

		prev_ptr = ret;
		ret = krealloc(prev_ptr, (total + count) * sizeof(*queue),
			       GFP_KERNEL);
		/*
		 * Out-of-memory, we can return the previously fetched responses
		 * if any, or ENOMEM otherwise.
		 */
		if (!ret) {
			if (!prev_ptr)
				ret = ERR_PTR(-ENOMEM);
			else
				ret = prev_ptr;
			break;
		}
		/* copy responses */
		j = CIRCULAR_QUEUE_REAL_INDEX(head);
		for (i = 0; i < count; i++) {
			memcpy(&ret[total], &queue[j], sizeof(*queue));
			ret[total].status = KCI_STATUS_OK;
			j = (j + 1) % size;
			total++;
		}
		head = circular_queue_inc(head, count, size);
	}
	edgetpu_mailbox_inc_resp_queue_head(kci->mailbox, total);

	mutex_unlock(&kci->resp_queue_lock);
	/*
	 * Ring the doorbell of *cmd* queue if the firmware might be waiting us
	 * to consume the response queue.
	 */
	if (total == size)
		EDGETPU_MAILBOX_CMD_QUEUE_WRITE(kci->mailbox, doorbell_set, 1);

	*total_ptr = total;
	return ret;
}

/*
 * Fetches and handles responses, then wakes up threads that are waiting for a
 * response.
 *
 * Note: this worker is scheduled in the IRQ handler, to prevent use-after-free
 * or race-condition bugs, edgetpu_kci_release() must be called before free the
 * mailbox.
 */
static void edgetpu_kci_consume_responses_work(struct work_struct *work)
{
	struct edgetpu_kci *kci = container_of(work, struct edgetpu_kci, work);
	struct edgetpu_mailbox *mailbox = kci->mailbox;
	struct edgetpu_kci_response_element *responses;
	u32 i;
	u32 count = 0;

	/* fetch responses and bump RESP_QUEUE_HEAD */
	responses = edgetpu_kci_fetch_responses(kci, &count);
	/* Wake up threads that waiting for response doorbell to be rung. */
	wake_up(&kci->resp_doorbell_waitq);
	if (IS_ERR(responses)) {
		etdev_err(mailbox->etdev,
			  "KCI failed on fetching responses: %ld",
			  PTR_ERR(responses));
		return;
	}

	for (i = 0; i < count; i++)
		edgetpu_kci_handle_response(kci, &responses[i]);
	/*
	 * Responses handled, wake up threads that are waiting for a response.
	 */
	wake_up(&kci->wait_list_waitq);
	kfree(responses);
}

/*
 * IRQ handler of KCI mailbox.
 *
 * Puts the edgetpu_kci_consume_responses_work() into the system work queue.
 */
static void edgetpu_kci_handle_irq(struct edgetpu_mailbox *mailbox)
{
	struct edgetpu_kci *kci = mailbox->internal.kci;

	schedule_work(&kci->work);
}

int edgetpu_kci_init(struct edgetpu_mailbox_manager *mgr,
		     struct edgetpu_kci *kci)
{
	struct edgetpu_mailbox *mailbox = edgetpu_mailbox_kci(mgr);
	void *addr;

	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	addr = edgetpu_kci_alloc_queue(mgr->etdev, mailbox, MAILBOX_CMD_QUEUE,
				       &kci->cmd_queue_tpu_addr,
				       &kci->cmd_queue_dma_addr);
	if (IS_ERR(addr)) {
		edgetpu_mailbox_remove(mgr, mailbox);
		return PTR_ERR(addr);
	}
	kci->cmd_queue = addr;
	mutex_init(&kci->cmd_queue_lock);
	etdev_dbg(mgr->etdev, "%s: cmdq kva=%pK iova=0x%llx dma=%pad", __func__,
		  addr, kci->cmd_queue_tpu_addr, &kci->cmd_queue_dma_addr);

	addr = edgetpu_kci_alloc_queue(mgr->etdev, mailbox, MAILBOX_RESP_QUEUE,
				       &kci->resp_queue_tpu_addr,
				       &kci->resp_queue_dma_addr);
	if (IS_ERR(addr)) {
		edgetpu_mailbox_remove(mgr, mailbox);
		return PTR_ERR(addr);
	}
	kci->resp_queue = addr;
	mutex_init(&kci->resp_queue_lock);
	etdev_dbg(mgr->etdev, "%s: rspq kva=%pK iova=0x%llx dma=%pad", __func__,
		  addr, kci->resp_queue_tpu_addr, &kci->resp_queue_dma_addr);

	mailbox->handle_irq = edgetpu_kci_handle_irq;
	mailbox->internal.kci = kci;
	kci->mailbox = mailbox;
	kci->cur_seq = 0;
	mutex_init(&kci->mailbox_lock);
	init_waitqueue_head(&kci->resp_doorbell_waitq);
	INIT_LIST_HEAD(&kci->wait_list);
	mutex_init(&kci->wait_list_lock);
	init_waitqueue_head(&kci->wait_list_waitq);
	INIT_WORK(&kci->work, edgetpu_kci_consume_responses_work);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, context_enable, 1);
	return 0;
}

int edgetpu_kci_reinit(struct edgetpu_kci *kci)
{
	struct edgetpu_mailbox *mailbox = kci->mailbox;
	int ret;

	if (!mailbox)
		return -ENODEV;
	ret = edgetpu_mailbox_set_queue(mailbox, MAILBOX_CMD_QUEUE,
					kci->cmd_queue_tpu_addr, QUEUE_SIZE);
	if (ret)
		return ret;
	ret = edgetpu_mailbox_set_queue(mailbox, MAILBOX_RESP_QUEUE,
					kci->resp_queue_tpu_addr, QUEUE_SIZE);
	if (ret)
		return ret;
	edgetpu_mailbox_init_doorbells(mailbox);
	EDGETPU_MAILBOX_CONTEXT_WRITE(mailbox, context_enable, 1);

	return 0;
}

void edgetpu_kci_release(struct edgetpu_dev *etdev, struct edgetpu_kci *kci)
{
	if (!kci)
		return;
	/*
	 * Command/Response queues are managed (dmam_alloc_coherent()), we don't
	 * need to free them.
	 */

	/* Cancel the queue consumer worker or wait until it's done. */
	cancel_work_sync(&kci->work);

	edgetpu_mmu_tpu_unmap(etdev, kci->cmd_queue_tpu_addr,
			      QUEUE_SIZE *
			      edgetpu_kci_queue_element_size(MAILBOX_CMD_QUEUE),
			      EDGETPU_CONTEXT_KCI);
	edgetpu_mmu_tpu_unmap(etdev, kci->resp_queue_tpu_addr,
			      QUEUE_SIZE *
			      edgetpu_kci_queue_element_size(MAILBOX_RESP_QUEUE),
			      EDGETPU_CONTEXT_KCI);

	/*
	 * Non-empty @kci->wait_list means someone
	 * (edgetpu_kci_push_cmd_wait_timeout) is waiting for a response.
	 *
	 * Since this function is only called when removing a device,
	 * it should be impossible to reach here with
	 * edgetpu_kci_push_cmd_wait_timeout() is still waiting (rmmod should
	 * fail), add a simple check here so we can easier to figure it out when
	 * this happens.
	 */
	if (!list_empty(&kci->wait_list))
		etdev_warn(etdev, "KCI commands still pending");
	/* detach the mailbox */
	kci->mailbox = NULL;
}

/*
 * Adds @resp to @kci->wait_list.
 *
 * wait_list is a FIFO queue, with sequence number in increasing order.
 *
 * Returns 0 on success, or -ENOMEM if failed on allocation.
 */
static int edgetpu_kci_push_wait_resp(struct edgetpu_kci *kci,
				      struct edgetpu_kci_response_element *resp)
{
	struct edgetpu_kci_wait_list *entry = kzalloc(sizeof(*entry),
						      GFP_KERNEL);

	if (!entry)
		return -ENOMEM;
	entry->resp = resp;
	mutex_lock(&kci->wait_list_lock);
	list_add_tail(&entry->list, &kci->wait_list);
	mutex_unlock(&kci->wait_list_lock);

	return 0;
}

/*
 * Removes the response previously pushed with edgetpu_kci_push_wait_resp().
 *
 * This is used when the kernel gives up waiting for the response.
 */
static void edgetpu_kci_del_wait_resp(struct edgetpu_kci *kci,
				      struct edgetpu_kci_response_element *resp)
{
	struct edgetpu_kci_wait_list *cur;

	mutex_lock(&kci->wait_list_lock);

	list_for_each_entry(cur, &kci->wait_list, list) {
		if (cur->resp->seq > resp->seq)
			break;
		if (cur->resp->seq == resp->seq) {
			list_del(&cur->list);
			kfree(cur);
			break;
		}
	}

	mutex_unlock(&kci->wait_list_lock);
}

int edgetpu_kci_push_cmd(struct edgetpu_kci *kci,
			 struct edgetpu_command_element *cmd,
			 struct edgetpu_kci_response_element *resp)
{
	int ret;
	u32 tail;

	mutex_lock(&kci->cmd_queue_lock);

	cmd->seq = kci->cur_seq;
	/*
	 * The lock ensures mailbox->cmd_queue_tail cannot be changed by
	 * other processes (this method should be the only one to modify the
	 * value of tail), therefore we can remember its value here and use it
	 * in the condition of wait_event() call.
	 */
	tail = kci->mailbox->cmd_queue_tail;
	/*
	 * Waits until the cmd queue is not full.
	 * Response doorbell rung means remote might have consumed commands.
	 */
	ret = wait_event_timeout(
		       kci->resp_doorbell_waitq,
		       EDGETPU_MAILBOX_CMD_QUEUE_READ(kci->mailbox, head)
		       != (tail ^ CIRCULAR_QUEUE_WRAP_BIT),
		       msecs_to_jiffies(KCI_TIMEOUT));
	if (!ret) {
		ret = -ETIMEDOUT;
		goto out;
	}
	if (resp) {
		/*
		 * Add @resp to the wait_list only if the cmd can be pushed
		 * successfully.
		 */
		resp->seq = cmd->seq;
		resp->status = KCI_STATUS_WAITING_RESPONSE;
		ret = edgetpu_kci_push_wait_resp(kci, resp);
		if (ret)
			goto out;
	}
	/* size of cmd_queue is a multiple of sizeof(*cmd) */
	memcpy(kci->cmd_queue + CIRCULAR_QUEUE_REAL_INDEX(tail), cmd,
	       sizeof(*cmd));
	edgetpu_mailbox_inc_cmd_queue_tail(kci->mailbox, 1);
	/* triggers doorbell */
	EDGETPU_MAILBOX_CMD_QUEUE_WRITE_SYNC(kci->mailbox, doorbell_set, 1);
	/* bumps sequence number after the command is sent */
	kci->cur_seq++;
	ret = 0;
out:
	mutex_unlock(&kci->cmd_queue_lock);
	if (ret)
		etdev_dbg(kci->mailbox->etdev, "%s: ret=%d", __func__, ret);

	return ret;
}

int edgetpu_kci_push_cmd_wait_timeout(struct edgetpu_kci *kci,
				      struct edgetpu_command_element *cmd,
				      long timeout)
{
	int ret;
	struct edgetpu_kci_response_element resp;

	ret = edgetpu_kci_push_cmd(kci, cmd, &resp);
	if (ret)
		return ret;
	ret = wait_event_timeout(kci->wait_list_waitq,
				 resp.status != KCI_STATUS_WAITING_RESPONSE,
				 msecs_to_jiffies(timeout));
	if (!ret) {
		etdev_dbg(kci->mailbox->etdev, "%s: event wait timeout",
			  __func__);
		edgetpu_kci_del_wait_resp(kci, &resp);
		return -ETIMEDOUT;
	}
	if (resp.status != KCI_STATUS_OK) {
		etdev_dbg(kci->mailbox->etdev, "%s: resp status=%u", __func__,
			  resp.status);
		return -ENOMSG;
	}

	return resp.code;
}

int edgetpu_kci_unmap_buffer(struct edgetpu_kci *kci, tpu_addr_t tpu_addr,
			     u32 size, enum dma_data_direction dir)
{
	struct edgetpu_command_element cmd = {
		.code = KCI_CODE_UNMAP_BUFFER,
		.dma = {
			.address = tpu_addr,
			.size = size,
			.flags = dir,
		},
	};

	return edgetpu_kci_push_cmd_wait_timeout(kci, &cmd, KCI_TIMEOUT);
}

int edgetpu_kci_ack(struct edgetpu_kci *kci)
{
	struct edgetpu_command_element cmd = {
		.code = KCI_CODE_ACK,
	};

	return edgetpu_kci_push_cmd_wait_timeout(kci, &cmd, KCI_TIMEOUT);
}

int edgetpu_kci_map_log_buffer(struct edgetpu_kci *kci, tpu_addr_t tpu_addr,
			       u32 size)
{
	struct edgetpu_command_element cmd = {
		.code = KCI_CODE_MAP_LOG_BUFFER,
		.dma = {
			.address = tpu_addr,
			.size = size,
		},
	};

	return edgetpu_kci_push_cmd_wait_timeout(kci, &cmd, KCI_TIMEOUT);
}

int edgetpu_kci_map_trace_buffer(struct edgetpu_kci *kci, tpu_addr_t tpu_addr,
				 u32 size)
{
	struct edgetpu_command_element cmd = {
		.code = KCI_CODE_MAP_TRACE_BUFFER,
		.dma = {
			.address = tpu_addr,
			.size = size,
		},
	};

	return edgetpu_kci_push_cmd_wait_timeout(kci, &cmd, KCI_TIMEOUT);
}

int edgetpu_kci_join_group(struct edgetpu_kci *kci, struct edgetpu_dev *etdev,
			   u8 n_dies, u8 vid)
{
	struct edgetpu_kci_device_group_detail *detail;
	const u32 size = sizeof(*detail);
	dma_addr_t dma_addr;
	tpu_addr_t tpu_addr;
	struct edgetpu_command_element cmd = {
		.code = KCI_CODE_JOIN_GROUP,
		.dma = {
			.size = size,
		},
	};
	const u32 flags = EDGETPU_MMU_DIE | EDGETPU_MMU_32 | EDGETPU_MMU_HOST;
	int ret;

	if (!kci)
		return -ENODEV;
	detail = dma_alloc_coherent(etdev->dev, sizeof(*detail), &dma_addr,
				    GFP_KERNEL);
	if (!detail)
		return -ENOMEM;
	detail->n_dies = n_dies;
	detail->vid = vid;

	tpu_addr = edgetpu_mmu_tpu_map(etdev, dma_addr, size, DMA_TO_DEVICE,
				       EDGETPU_CONTEXT_KCI, flags);
	if (!tpu_addr) {
		etdev_err(etdev, "%s: failed to map group detail to TPU",
			  __func__);
		dma_free_coherent(etdev->dev, size, detail, dma_addr);
		return -EINVAL;
	}

	cmd.dma.address = tpu_addr;
	etdev_dbg(etdev, "%s: map kva=%pK iova=0x%llx dma=%pad", __func__,
		  detail, tpu_addr, &dma_addr);

	ret = edgetpu_kci_push_cmd_wait_timeout(kci, &cmd, KCI_TIMEOUT);
	edgetpu_mmu_tpu_unmap(etdev, tpu_addr, size, EDGETPU_CONTEXT_KCI);
	dma_free_coherent(etdev->dev, size, detail, dma_addr);
	etdev_dbg(etdev, "%s: unmap kva=%pK iova=0x%llx dma=%pad", __func__,
		  detail, tpu_addr, &dma_addr);

	return ret;
}

int edgetpu_kci_leave_group(struct edgetpu_kci *kci)
{
	struct edgetpu_command_element cmd = {
		.code = KCI_CODE_LEAVE_GROUP,
	};

	if (!kci)
		return -ENODEV;
	return edgetpu_kci_push_cmd_wait_timeout(kci, &cmd, KCI_TIMEOUT);
}

/* debugfs mappings dump */
void edgetpu_kci_mappings_show(struct edgetpu_dev *etdev, struct seq_file *s)
{
	struct edgetpu_kci *kci = etdev->kci;

	if (!kci || !kci->mailbox)
		return;

	seq_printf(s, "kci context %u:\n", EDGETPU_CONTEXT_KCI);
	seq_printf(s, "  0x%llx %lu cmdq - %pad\n",
		   kci->cmd_queue_tpu_addr,
		   QUEUE_SIZE *
		   edgetpu_kci_queue_element_size(MAILBOX_CMD_QUEUE)
		   / PAGE_SIZE, &kci->cmd_queue_dma_addr);
	seq_printf(s, "  0x%llx %lu rspq - %pad\n",
		   kci->resp_queue_tpu_addr,
		   QUEUE_SIZE *
		   edgetpu_kci_queue_element_size(MAILBOX_RESP_QUEUE)
		   / PAGE_SIZE, &kci->resp_queue_dma_addr);
	edgetpu_telemetry_mappings_show(etdev, s);
	edgetpu_firmware_mappings_show(etdev, s);
}

int edgetpu_kci_shutdown(struct edgetpu_kci *kci)
{
	struct edgetpu_command_element cmd = {
		.code = KCI_CODE_SHUTDOWN,
	};

	if (!kci)
		return -ENODEV;
	return edgetpu_kci_push_cmd_wait_timeout(kci, &cmd, KCI_TIMEOUT);
}
