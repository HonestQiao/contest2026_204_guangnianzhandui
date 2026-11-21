/****************************************************************************
 * vendor/st/chips/stm32n6/st_oneshot.c
 *
 * System tick timer for the ATK-DNN647 (STM32N647) using the Cortex-M55
 * SysTick.
 *
 * The CPU clock is 600MHz after the ATK FSBL (PLL1 1.2GHz / IC1 /2) and
 * 64MHz (HSI) when booted via the serial boot ROM.  We assume 600MHz for
 * the flash-boot case; when running from the serial boot the tick is
 * proportionally slower but still functional.
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

#include <stdint.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/clock.h>
#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SysTick registers (the secure instance; our code runs in the secure
 * state when booted by the ATK FSBL). */

#define SYSTICK_CTRL   0xe000e010UL
#define SYSTICK_LOAD   0xe000e014UL
#define SYSTICK_VAL    0xe000e018UL

#define SYSTICK_CTRL_CLKSOURCE  (1UL << 2)
#define SYSTICK_CTRL_TICKINT    (1UL << 1)
#define SYSTICK_CTRL_ENABLE     (1UL << 0)

/* CPU clock frequency in Hz.  The ATK FSBL configures PLL1 to 1.2GHz and
 * IC1 to 600MHz before jumping to the application. */

#define CPU_FREQ        600000000UL

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int n6_systick_isr(int irq, void *context, void *arg)
{
  nxsched_process_timer();
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   Initialize the SysTick as the system tick timer.  Without this the
 *   scheduler has no tick source and every blocking operation (semaphores,
 *   mutexes, sleep) hangs forever.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  uint32_t reload = CPU_FREQ / CLK_TCK - 1;

  /* Attach the tick interrupt (SysTick = exception 15). */

  irq_attach(15, n6_systick_isr, NULL);  /* SysTick = exception 15 */

  /* Program SysTick: clock = CPU clock, reload for CLK_TCK, interrupt on
   * underflow, enable. */

  *(volatile uint32_t *)SYSTICK_LOAD = reload;
  *(volatile uint32_t *)SYSTICK_VAL = 0;
  *(volatile uint32_t *)SYSTICK_CTRL = SYSTICK_CTRL_CLKSOURCE |
                                       SYSTICK_CTRL_TICKINT |
                                       SYSTICK_CTRL_ENABLE;


}
