/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Edge TPU MMU API.
 *
 * Copyright (C) 2020 Google, Inc.
 */
#ifndef __EDGETPU_MMU_H__
#define __EDGETPU_MMU_H__

#include <linux/dma-direction.h>

#include "edgetpu-internal.h"
#include "edgetpu.h"

#define IS_MIRRORED(flag) (!((flag) & EDGETPU_MAP_NONMIRRORED))

/* flags for MMU operations */

/* The target of operation is a single die or the whole virtual device group */
#define EDGETPU_MMU_VDG		(0 << 0)
#define EDGETPU_MMU_DIE		(1 << 0)
/* Whether the TPU address to be allocated can be 64-bit wide. */
#define EDGETPU_MMU_32		(0 << 1)
#define EDGETPU_MMU_64		(1 << 1)
/* The memory will be mapped to host DRAM, device DRAM, or dma-buf. */
#define EDGETPU_MMU_HOST	(0 << 2)
#define EDGETPU_MMU_DEVICE	(1 << 2)
#define EDGETPU_MMU_DMABUF	(2 << 2)

/*
 * Return the DMA direction to use for the host DMA API call to map a buffer.
 * Normally DMA buffers "only written" by the device (so far as the TPU runtime
 * is concerned) would be mapped write-only to the host IOMMU.  However, our
 * TPU CPU may perform cache line fills and possibly prefetches from the buffer
 * being written to.  Map write-only buffers bi-directional.
 */
static inline enum dma_data_direction
edgetpu_host_dma_dir(enum dma_data_direction target_dir)
{
	switch (target_dir) {
	case DMA_FROM_DEVICE:
		return DMA_BIDIRECTIONAL;
	default:
		return target_dir;
	}
}

static inline u32 map_to_mmu_flags(edgetpu_map_flag_t flags)
{
	u32 ret = 0;

	ret |= IS_MIRRORED(flags) ? EDGETPU_MMU_VDG : EDGETPU_MMU_DIE;
	ret |= (flags & EDGETPU_MAP_CPU_NONACCESSIBLE) ? EDGETPU_MMU_64 :
							 EDGETPU_MMU_32;
	return ret;
}

int edgetpu_mmu_attach(struct edgetpu_dev *dev, void *mmu_info);
void edgetpu_mmu_detach(struct edgetpu_dev *dev);
/**
 * Re-attach to previously attached MMU.
 *
 * Description: Re-attach to previously attached MMU with the same @mmu_info
 * passed in edgetpu_mmu_attach().
 *
 * This function is used when the MMU device is rebooted/reset, and the kernel
 * wants to resume the state before it being turned off.
 *
 * Returns 0 on success, or -errno on error.
 */
int edgetpu_mmu_reattach(struct edgetpu_dev *dev);
void edgetpu_mmu_reset(struct edgetpu_dev *dev);
int edgetpu_mmu_map(struct edgetpu_dev *dev, struct edgetpu_mapping *map,
		    enum edgetpu_context_id context_id, u32 mmu_flags);
void edgetpu_mmu_unmap(struct edgetpu_dev *dev, struct edgetpu_mapping *map,
		       enum edgetpu_context_id context_id);

/**
 * Maps TPU IOVA @iova to @sgt.
 * @sgt: the sg table presents the list of pages.
 *
 * Description: Request TPU to map @iova to the pages presented by @sgt.
 *
 * Returns 0 on success, -errno on error.
 */
int edgetpu_mmu_map_iova_sgt(struct edgetpu_dev *etdev, tpu_addr_t iova,
			     struct sg_table *sgt, enum dma_data_direction dir,
			     enum edgetpu_context_id context_id);
void edgetpu_mmu_unmap_iova_sgt(struct edgetpu_dev *etdev, tpu_addr_t iova,
				struct sg_table *sgt,
				enum dma_data_direction dir,
				enum edgetpu_context_id context_id);

/**
 * Allocates an IOVA in the internal MMU.
 * @size: size needed to be allocated in bytes.
 *
 * Description: Allocates a TPU address to be mapped via
 * edgetpu_mmu_add_translation().
 *
 * If the chip doesn't have an internal MMU then return zero.
 *
 * Returns zero on error.
 */
tpu_addr_t edgetpu_mmu_alloc(struct edgetpu_dev *etdev, size_t size,
			     u32 mmu_flags);
/**
 * Marks the IOVA region [@tpu_addr, @tpu_addr + @size) as reserved.
 *
 * Description: Use this function to mark the region as reserved and prevents
 * it from being allocated by edgetpu_mmu_alloc().
 *
 * Use edgetpu_mmu_free() to release the reserved area.
 */
void edgetpu_mmu_reserve(struct edgetpu_dev *etdev, tpu_addr_t tpu_addr,
			 size_t size);
/*
 * Free the IOVA allocated by edgetpu_mmu_alloc() or reserved by
 * edgetpu_mmu_reserve().
 */
void edgetpu_mmu_free(struct edgetpu_dev *etdev, tpu_addr_t tpu_addr,
		      size_t size);

/**
 * Add an IOVA translation to the chip MMU/IOMMU.
 * @iova: I/O virtual address (TPU VA) to map to paddr.
 * @paddr: Physical/next-stage target address to which iova is to be mapped.
 * @size: size of the mapping in bytes.
 * @prot: IOMMU API protections to use for the mapping.
 * @context_id: generic context ID for the mapping.
 *
 * Description: Add a mapping from iova -> paddr to the MMU for the chip.
 * paddr can be considered a physical address from the TPU's viewpoint, but
 * may actually be another IOVA for another IOMMU downstream of the chip MMU
 * (as on Hermosa, where the SMMU translates TPU VAs to IOVAs sent to the IOMMU
 * downstream of the TPU).
 */
int edgetpu_mmu_add_translation(struct edgetpu_dev *etdev, unsigned long iova,
				phys_addr_t paddr, size_t size, int prot,
				enum edgetpu_context_id context_id);

/* Remove a translation added by edgetpu_mmu_add_translation. */
void edgetpu_mmu_remove_translation(struct edgetpu_dev *etdev,
				    unsigned long iova, size_t size,
				    enum edgetpu_context_id context_id);

/**
 * Add a TPU mapping for a local DMA mapping
 * @down_addr: DMA (or physical) addr of memory downstream from TPU
 * @size: size of memory area in bytes
 * @dir: DMA direction of mapping
 * @context_id: context ID for the mapping
 * @mmu_flags: the flag or'ed with EDGETPU_MMU_* macros
 *
 * Description: For chips with internal MMUs (e.g., Hermosa SMMU), add the
 * required internal MMU mapping for the TPU to access @downstream_addr, the
 * DMA or physical address of the buffer as returned by the Linux DMA API when
 * the DMA mapping was created.  This can be used with, for example, buffers
 * allocated using dma_alloc_coherent(), which are mapped appropriately for
 * any downstream IOMMU and must be mapped to the TPU internal MMU as well.
 *
 * If the chip doesn't have an internal MMU then just return the downstream
 * DMA address.
 *
 * Returns zero on error.
 */
tpu_addr_t edgetpu_mmu_tpu_map(struct edgetpu_dev *etdev, dma_addr_t down_addr,
			       size_t size, enum dma_data_direction dir,
			       enum edgetpu_context_id context_id,
			       u32 mmu_flags);

/* Unmap a TPU mapping created by edgetpu_mmu_tpu_map */
void edgetpu_mmu_tpu_unmap(struct edgetpu_dev *etdev,
			   tpu_addr_t tpu_addr, size_t size,
			   enum edgetpu_context_id context_id);

#endif /* __EDGETPU_MMU_H__ */
