/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright 2014 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * Header file for tmu support
 *
 */

#ifndef __ASM_ARCH_TMU_H
#define __ASM_ARCH_TMU_H

#define EXYNOS_MAX_TEMP		125
#define EXYNOS_MIN_TEMP		10
#define EXYNOS_COLD_TEMP	15

#define THERMAL_CFREQ_INVALID -1

enum tmu_noti_state_t {
	TMU_NORMAL,
	TMU_COLD,
	TMU_HOT,
	TMU_CRITICAL,
};

enum gpu_noti_state_t {
	GPU_NORMAL,
	GPU_COLD,
	GPU_THROTTLING1,
	GPU_THROTTLING2,
	GPU_THROTTLING3,
	GPU_THROTTLING4,
	GPU_TRIPPING,
	GPU_THROTTLING,
};

enum isp_noti_state_t {
	ISP_NORMAL = 0,
	ISP_COLD,
	ISP_THROTTLING,
	ISP_THROTTLING1,
	ISP_THROTTLING2,
	ISP_THROTTLING3,
	ISP_THROTTLING4,
	ISP_TRIPPING,
};

#if IS_ENABLED(CONFIG_EXYNOS_THERMAL_V2)
extern int exynos_tmu_add_notifier(struct notifier_block *n);
#else
static inline int exynos_tmu_add_notifier(struct notifier_block *n)
{
	return 0;
}
#endif
#if IS_ENABLED(CONFIG_GPU_THERMAL)
extern int gpufreq_cooling_add_notifier(struct notifier_block *n);
extern int gpufreq_cooling_remove_notifier(struct notifier_block *n);
#else
static inline int gpufreq_cooling_add_notifier(struct notifier_block *n)
{
	return 0;
}
static inline int gpufreq_cooling_remove_notifier(struct notifier_block *n)
{
	return 0;
}
#endif
#if IS_ENABLED(CONFIG_ISP_THERMAL)
extern int exynos_tmu_isp_add_notifier(struct notifier_block *n);
#else
static inline int exynos_tmu_isp_add_notifier(struct notifier_block *n)
{
	return 0;
}
#endif
#endif /* __ASM_ARCH_TMU_H */
