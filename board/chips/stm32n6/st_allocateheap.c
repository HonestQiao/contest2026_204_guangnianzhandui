/****************************************************************************
 * vendor/st/chips/stm32n6/st_allocateheap.c
 *
 * Heap allocation for the zero-copy serial-boot layout.
 *
 * All memory (code, .data, .bss, heap, stacks) lives in FLEXRAM within
 * the boot ROM's writable window [ROM_start, vector[0]).
 *
 * The vector[0] = _ebss + CONFIG_IDLETHREAD_STACKSIZE (set by
 * arm_m/arm_vectors.c IDLE_STACK macro).  With
 * CONFIG_IDLETHREAD_STACKSIZE = 139000, vector[0] ≈ 0x341C1000 = the
 * end of the RAM region.
 *
 * The kernel heap = [_ebss, _ebss + CONFIG_IDLETHREAD_STACKSIZE - 4096),
 * leaving 2048 + 2048 = 4KB at the top for the idle task stack and
 * interrupt nesting.
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <nuttx/arch.h>

/* Linker-provided symbols (see ld.script). */
extern uint32_t _ebss[];
extern uint32_t _estack[];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void up_allocate_heap(void **heap_start, size_t *heap_size)
{
  /* The idle task's stack occupies [_ebss, _ebss + IDLETHREAD_STACKSIZE):
   * arm_m/arm_vectors.c puts IDLE_STACK (= _ebss + CONFIG_IDLETHREAD_STACKSIZE,
   * a full-descending stack top) in vector[0], and arm_initialstate.c derives
   * stack_alloc_ptr as g_idle_topstack - CONFIG_IDLETHREAD_STACKSIZE.  The
   * heap therefore must start ABOVE that stack and run to the end of the
   * RAM window, never inside the stack. */

  *heap_start = (void *)((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE);
  *heap_size  = (CONFIG_RAM_START + CONFIG_RAM_SIZE)
                - ((uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE);
}
