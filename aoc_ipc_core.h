// SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
/*
 * Copyright 2020 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AOC_IPC_CORE_H
#define AOC_IPC_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#endif

typedef void aoc_service;

typedef enum {
	AOC_UP = 0,
	AOC_DOWN = 1,
} aoc_direction;

/* Primitive functions */
bool aoc_service_is_ring(aoc_service *service);

const char *aoc_service_name(aoc_service *service);

size_t aoc_service_message_size(aoc_service *service, aoc_direction dir);
size_t aoc_service_message_slots(aoc_service *service, aoc_direction dir);
size_t aoc_service_total_size(aoc_service *service, aoc_direction dir);

size_t aoc_service_slots_available_to_read(aoc_service *service,
					   aoc_direction dir);
size_t aoc_service_slots_available_to_write(aoc_service *service,
					    aoc_direction dir);

/* High level functions */
bool aoc_service_can_read_message(aoc_service *service, aoc_direction dir);
bool aoc_service_can_write_message(aoc_service *service, aoc_direction dir);

bool aoc_service_read_message(aoc_service *service, void *base,
			      aoc_direction dir, void *dst, size_t *size);
bool aoc_service_write_message(aoc_service *service, void *base,
			       aoc_direction dir, const void *dst, size_t size);

size_t aoc_ring_bytes_read(aoc_service *service, aoc_direction dir);
size_t aoc_ring_bytes_written(aoc_service *service, aoc_direction dir);

#ifdef __cplusplus
}
#endif

#endif /* AOC_IPC_CORE_H */
