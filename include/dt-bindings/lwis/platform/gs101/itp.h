/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Google LWIS GS101 ITP Interrupt And Event Defines
 *
 * Copyright (c) 2020 Google, LLC
 */

#ifndef DT_BINDINGS_LWIS_PLATFORM_GS101_ITP_H_
#define DT_BINDINGS_LWIS_PLATFORM_GS101_ITP_H_

#include <dt-bindings/lwis/platform/common.h>

/* clang-format off */
#define ITP_INT1_BASE (HW_EVENT_MASK + 0)

#define ITP_INT1_FRAME_START 0
#define ITP_INT1_FRAME_END_INTERRUPT 1
#define ITP_INT1_FRAME_INT_ON_ROW_COL_INFO 2
#define ITP_INT1_DNS_COREX_ERROR_INT 3
#define ITP_INT1_ITP_COREX_ERROR_INT 4
#define ITP_INT1_PRE_FRAME_END_INTERRUPT 5
#define ITP_INT1_CORRUPTED_IRQ 11
#define ITP_INT1_C2COM_SLOW_RING 12
#define ITP_INT1_DNS_COREX_END_INT0 13
#define ITP_INT1_DNS_COREX_END_INT1 14
#define ITP_INT1_ITP_COREX_END_INT0 15
#define ITP_INT1_ITP_COREX_END_INT1 16
#define ITP_INT1_RDMA_BAYER 17
#define ITP_INT1_WDMA_YUV 18

#define ITP_INT2_BASE (HW_EVENT_MASK + 32)

#define ITP_INT2_CINFIFO_IPP_LINES_ERROR_INT 0
#define ITP_INT2_COUTFIFO_MAIN_LINES_ERROR_INT 1
#define ITP_INT2_COUTFIFO_LINEAR_LINES_ERROR_INT 2
#define ITP_INT2_COUTFIFO_MCSC_LINES_ERROR_INT 3
#define ITP_INT2_CINFIFO_IPP_COLUMNS_ERROR_INT 4
#define ITP_INT2_COUTFIFO_MAIN_COLUMNS_ERROR_INT 5
#define ITP_INT2_COUTFIFO_LINEAR_COLUMNS_ERROR_INT 6
#define ITP_INT2_COUTFIFO_MCSC_COLUMNS_ERROR_INT 7
#define ITP_INT2_CINFIFO_IPP_MISSED_FRAME_INT 8
#define ITP_INT2_CINFIFO_IPP_OVERFLOW_INT 9
#define ITP_INT2_DMA_SBWC_ERR 12

#define ITP_SCALER_INT_BASE (HW_EVENT_MASK + 64)

#define ITP_SCALER_INT_FRAME_END_INT 0
#define ITP_SCALER_INT_FRAME_START_INT 1
#define ITP_SCALER_INT_WDMA_FINISH_INT 2
#define ITP_SCALER_INT_CORE_FINISH_INT 3
#define ITP_SCALER_INT_INPUT_HORIZONTAL_OVF_INT 7
#define ITP_SCALER_INT_INPUT_HORIZONTAL_UNF_INT 8
#define ITP_SCALER_INT_INPUT_VERTICAL_OVF_INT 9
#define ITP_SCALER_INT_INPUT_VERTICAL_UNF_INT 10
#define ITP_SCALER_INT_SCALER_OVERFLOW_INT 11
#define ITP_SCALER_INT_INPUT_FRAME_CRUSH_INT 12
#define ITP_SCALER_INT_SHADOW_COPY_FINISH_INT 16
#define ITP_SCALER_INT_SHADOW_COPY_FINISH_OVER_INT 17
#define ITP_SCALER_INT_FM_SUB_FRAME_FINISH_INT 20
#define ITP_SCALER_INT_FM_SUB_FRAME_START_INT 21
#define ITP_SCALER_INT_VOTF_WDMA_LOST_FLUSH_INT 22
#define ITP_SCALER_INT_VOTF_WDMA_SLOW_RING_STATUS_INT 23
#define ITP_SCALER_INT_STALL_TIMEOUT_INT 24
#define ITP_SCALER_INT_OTFOUT_STALL_BLOCKING_INT 25
#define ITP_SCALER_INT_LINEAR_INPUT_HORIZONTAL_OVF_INT 26
#define ITP_SCALER_INT_LINEAR_INPUT_HORIZONTAL_UNF_INT 27
#define ITP_SCALER_INT_LINEAR_INPUT_VERTICAL_OVF_INT 28
#define ITP_SCALER_INT_LINEAR_INPUT_VERTICAL_UNF_INT 29
#define ITP_SCALER_INT_LINEAR_SCALER_OVERFLOW_INT 30
#define ITP_SCALER_INT_LINEAR_STALL_TIMEOUT_INT 31

#define ITP_FRO_INT1_BASE (HW_EVENT_MASK + 96)

#define ITP_FRO_INT1_FRAME_START 0
#define ITP_FRO_INT1_FRAME_END_INTERRUPT 1
#define ITP_FRO_INT1_FRAME_INT_ON_ROW_COL_INFO 2
#define ITP_FRO_INT1_DNS_COREX_ERROR_INT 3
#define ITP_FRO_INT1_ITP_COREX_ERROR_INT 4
#define ITP_FRO_INT1_PRE_FRAME_END_INTERRUPT 5
#define ITP_FRO_INT1_CORRUPTED_IRQ 11
#define ITP_FRO_INT1_C2COM_SLOW_RING 12
#define ITP_FRO_INT1_DNS_COREX_END_INT0 13
#define ITP_FRO_INT1_DNS_COREX_END_INT1 14
#define ITP_FRO_INT1_ITP_COREX_END_INT0 15
#define ITP_FRO_INT1_ITP_COREX_END_INT1 16
#define ITP_FRO_INT1_RDMA_BAYER 17
#define ITP_FRO_INT1_WDMA_YUV 18

#define ITP_FRO_INT2_BASE (HW_EVENT_MASK + 128)

#define ITP_FRO_INT2_CINFIFO_IPP_LINES_ERROR_INT 0
#define ITP_FRO_INT2_COUTFIFO_MAIN_LINES_ERROR_INT 1
#define ITP_FRO_INT2_COUTFIFO_LINEAR_LINES_ERROR_INT 2
#define ITP_FRO_INT2_COUTFIFO_MCSC_LINES_ERROR_INT 3
#define ITP_FRO_INT2_CINFIFO_IPP_COLUMNS_ERROR_INT 4
#define ITP_FRO_INT2_COUTFIFO_MAIN_COLUMNS_ERROR_INT 5
#define ITP_FRO_INT2_COUTFIFO_LINEAR_COLUMNS_ERROR_INT 6
#define ITP_FRO_INT2_COUTFIFO_MCSC_COLUMNS_ERROR_INT 7
#define ITP_FRO_INT2_CINFIFO_IPP_MISSED_FRAME_INT 8
#define ITP_FRO_INT2_CINFIFO_IPP_OVERFLOW_INT 9
#define ITP_FRO_INT2_DMA_SBWC_ERR 12

/* clang-format on */

#define LWIS_PLATFORM_EVENT_ID_ITP_FRAME_START                                 \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_FRAME_START)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRAME_END_INTERRUPT                         \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_FRAME_END_INTERRUPT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRAME_INT_ON_ROW_COL_INFO                   \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_FRAME_INT_ON_ROW_COL_INFO)
#define LWIS_PLATFORM_EVENT_ID_ITP_DNS_COREX_ERROR_INT                         \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_DNS_COREX_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_ITP_COREX_ERROR_INT                         \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_ITP_COREX_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_PRE_FRAME_END_INTERRUPT                     \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_PRE_FRAME_END_INTERRUPT)
#define LWIS_PLATFORM_EVENT_ID_ITP_CORRUPTED_IRQ                               \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_CORRUPTED_IRQ)
#define LWIS_PLATFORM_EVENT_ID_ITP_C2COM_SLOW_RING                             \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_C2COM_SLOW_RING)
#define LWIS_PLATFORM_EVENT_ID_ITP_DNS_COREX_END_INT0                          \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_DNS_COREX_END_INT0)
#define LWIS_PLATFORM_EVENT_ID_ITP_DNS_COREX_END_INT1                          \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_DNS_COREX_END_INT1)
#define LWIS_PLATFORM_EVENT_ID_ITP_ITP_COREX_END_INT0                          \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_ITP_COREX_END_INT0)
#define LWIS_PLATFORM_EVENT_ID_ITP_ITP_COREX_END_INT1                          \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_ITP_COREX_END_INT1)
#define LWIS_PLATFORM_EVENT_ID_ITP_RDMA_BAYER                                  \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_RDMA_BAYER)
#define LWIS_PLATFORM_EVENT_ID_ITP_WDMA_YUV                                    \
	EVENT_ID(ITP_INT1_BASE, ITP_INT1_WDMA_YUV)

#define LWIS_PLATFORM_EVENT_ID_ITP_CINFIFO_IPP_LINES_ERROR_INT                 \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_CINFIFO_IPP_LINES_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_COUTFIFO_MAIN_LINES_ERROR_INT               \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_COUTFIFO_MAIN_LINES_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_COUTFIFO_LINEAR_LINES_ERROR_INT             \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_COUTFIFO_LINEAR_LINES_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_COUTFIFO_MCSC_LINES_ERROR_INT               \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_COUTFIFO_MCSC_LINES_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_CINFIFO_IPP_COLUMNS_ERROR_INT               \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_CINFIFO_IPP_COLUMNS_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_COUTFIFO_MAIN_COLUMNS_ERROR_INT             \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_COUTFIFO_MAIN_COLUMNS_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_COUTFIFO_LINEAR_COLUMNS_ERROR_INT           \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_COUTFIFO_LINEAR_COLUMNS_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_COUTFIFO_MCSC_COLUMNS_ERROR_INT             \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_COUTFIFO_MCSC_COLUMNS_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_CINFIFO_IPP_MISSED_FRAME_INT                \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_CINFIFO_IPP_MISSED_FRAME_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_CINFIFO_IPP_OVERFLOW_INT                    \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_CINFIFO_IPP_OVERFLOW_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_DMA_SBWC_ERR                                \
	EVENT_ID(ITP_INT2_BASE, ITP_INT2_DMA_SBWC_ERR)

#define LWIS_PLATFORM_EVENT_ID_ITP_FRAME_END_INT                               \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_FRAME_END_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRAME_START_INT                             \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_FRAME_START_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_WDMA_FINISH_INT                             \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_WDMA_FINISH_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_CORE_FINISH_INT                             \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_CORE_FINISH_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_INPUT_HORIZONTAL_OVF_INT                    \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_INPUT_HORIZONTAL_OVF_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_INPUT_HORIZONTAL_UNF_INT                    \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_INPUT_HORIZONTAL_UNF_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_INPUT_VERTICAL_OVF_INT                      \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_INPUT_VERTICAL_OVF_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_INPUT_VERTICAL_UNF_INT                      \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_INPUT_VERTICAL_UNF_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_SCALER_OVERFLOW_INT                         \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_SCALER_OVERFLOW_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_INPUT_FRAME_CRUSH_INT                       \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_INPUT_FRAME_CRUSH_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_SHADOW_COPY_FINISH_INT                      \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_SHADOW_COPY_FINISH_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_SHADOW_COPY_FINISH_OVER_INT                 \
	EVENT_ID(ITP_SCALER_INT_BASE,                                          \
		 ITP_SCALER_INT_SHADOW_COPY_FINISH_OVER_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FM_SUB_FRAME_FINISH_INT                     \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_FM_SUB_FRAME_FINISH_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FM_SUB_FRAME_START_INT                      \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_FM_SUB_FRAME_START_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_VOTF_WDMA_LOST_FLUSH_INT                    \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_VOTF_WDMA_LOST_FLUSH_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_VOTF_WDMA_SLOW_RING_STATUS_INT              \
	EVENT_ID(ITP_SCALER_INT_BASE,                                          \
		 ITP_SCALER_INT_VOTF_WDMA_SLOW_RING_STATUS_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_STALL_TIMEOUT_INT                           \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_STALL_TIMEOUT_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_OTFOUT_STALL_BLOCKING_INT                   \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_OTFOUT_STALL_BLOCKING_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_LINEAR_INPUT_HORIZONTAL_OVF_INT             \
	EVENT_ID(ITP_SCALER_INT_BASE,                                          \
		 ITP_SCALER_INT_LINEAR_INPUT_HORIZONTAL_OVF_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_LINEAR_INPUT_HORIZONTAL_UNF_INT             \
	EVENT_ID(ITP_SCALER_INT_BASE,                                          \
		 ITP_SCALER_INT_LINEAR_INPUT_HORIZONTAL_UNF_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_LINEAR_INPUT_VERTICAL_OVF_INT               \
	EVENT_ID(ITP_SCALER_INT_BASE,                                          \
		 ITP_SCALER_INT_LINEAR_INPUT_VERTICAL_OVF_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_LINEAR_INPUT_VERTICAL_UNF_INT               \
	EVENT_ID(ITP_SCALER_INT_BASE,                                          \
		 ITP_SCALER_INT_LINEAR_INPUT_VERTICAL_UNF_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_LINEAR_SCALER_OVERFLOW_INT                  \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_LINEAR_SCALER_OVERFLOW_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_LINEAR_STALL_TIMEOUT_INT                    \
	EVENT_ID(ITP_SCALER_INT_BASE, ITP_SCALER_INT_LINEAR_STALL_TIMEOUT_INT)

#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_FRAME_START                             \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_FRAME_START)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_FRAME_END_INTERRUPT                     \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_FRAME_END_INTERRUPT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_FRAME_INT_ON_ROW_COL_INFO               \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_FRAME_INT_ON_ROW_COL_INFO)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_DNS_COREX_ERROR_INT                     \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_DNS_COREX_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_ITP_COREX_ERROR_INT                     \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_ITP_COREX_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_PRE_FRAME_END_INTERRUPT                 \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_PRE_FRAME_END_INTERRUPT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_CORRUPTED_IRQ                           \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_CORRUPTED_IRQ)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_C2COM_SLOW_RING                         \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_C2COM_SLOW_RING)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_DNS_COREX_END_INT0                      \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_DNS_COREX_END_INT0)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_DNS_COREX_END_INT1                      \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_DNS_COREX_END_INT1)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_ITP_COREX_END_INT0                      \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_ITP_COREX_END_INT0)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_ITP_COREX_END_INT1                      \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_ITP_COREX_END_INT1)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_RDMA_BAYER                              \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_RDMA_BAYER)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_WDMA_YUV                                \
	EVENT_ID(ITP_FRO_INT1_BASE, ITP_FRO_INT1_WDMA_YUV)

#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_CINFIFO_IPP_LINES_ERROR_INT             \
	EVENT_ID(ITP_FRO_INT2_BASE, ITP_FRO_INT2_CINFIFO_IPP_LINES_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_COUTFIFO_MAIN_LINES_ERROR_INT           \
	EVENT_ID(ITP_FRO_INT2_BASE, ITP_FRO_INT2_COUTFIFO_MAIN_LINES_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_COUTFIFO_LINEAR_LINES_ERROR_INT         \
	EVENT_ID(ITP_FRO_INT2_BASE,                                            \
		 ITP_FRO_INT2_COUTFIFO_LINEAR_LINES_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_COUTFIFO_MCSC_LINES_ERROR_INT           \
	EVENT_ID(ITP_FRO_INT2_BASE, ITP_FRO_INT2_COUTFIFO_MCSC_LINES_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_CINFIFO_IPP_COLUMNS_ERROR_INT           \
	EVENT_ID(ITP_FRO_INT2_BASE, ITP_FRO_INT2_CINFIFO_IPP_COLUMNS_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_COUTFIFO_MAIN_COLUMNS_ERROR_INT         \
	EVENT_ID(ITP_FRO_INT2_BASE,                                            \
		 ITP_FRO_INT2_COUTFIFO_MAIN_COLUMNS_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_COUTFIFO_LINEAR_COLUMNS_ERROR_INT       \
	EVENT_ID(ITP_FRO_INT2_BASE,                                            \
		 ITP_FRO_INT2_COUTFIFO_LINEAR_COLUMNS_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_COUTFIFO_MCSC_COLUMNS_ERROR_INT         \
	EVENT_ID(ITP_FRO_INT2_BASE,                                            \
		 ITP_FRO_INT2_COUTFIFO_MCSC_COLUMNS_ERROR_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_CINFIFO_IPP_MISSED_FRAME_INT            \
	EVENT_ID(ITP_FRO_INT2_BASE, ITP_FRO_INT2_CINFIFO_IPP_MISSED_FRAME_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_CINFIFO_IPP_OVERFLOW_INT                \
	EVENT_ID(ITP_FRO_INT2_BASE, ITP_FRO_INT2_CINFIFO_IPP_OVERFLOW_INT)
#define LWIS_PLATFORM_EVENT_ID_ITP_FRO_DMA_SBWC_ERR                            \
	EVENT_ID(ITP_FRO_INT2_BASE, ITP_FRO_INT2_DMA_SBWC_ERR)

#endif /* DT_BINDINGS_LWIS_PLATFORM_GS101_ITP_H_ */
