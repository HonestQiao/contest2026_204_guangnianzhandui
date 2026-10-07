/****************************************************************************
 * vendor/st/chips/stm32n6/st_irq.c
 *
 * STM32N6 NVIC interrupt controller management.
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
#include <assert.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <arch/irq.h>

#include "arm_internal.h"
#include "nvic.h"
#include "st_irq.h"

/* Default priority for all interrupts and exceptions */
#define DEFPRIORITY32  0xf0f0f0f0

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* NVIC priority register addresses */


/* Default priority for all interrupts and exceptions */


/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 *
 * Description:
 *   Initialize the interrupt subsystem.
 *
 ****************************************************************************/

void up_irqinitialize(void)
{
  int i;

  /* Disable all interrupts */

  for (i = 0; i < NR_IRQS - NVIC_IRQ_FIRST; i += 32)
    {
      putreg32(0xffffffff, NVIC_IRQ_CLEAR(i));
    }

  /* Set all interrupts (and exceptions) to the default priority */

  putreg32(DEFPRIORITY32, NVIC_SYSH4_7_PRIORITY);
  putreg32(DEFPRIORITY32, NVIC_SYSH8_11_PRIORITY);
  putreg32(DEFPRIORITY32, NVIC_SYSH12_15_PRIORITY);

  /* Attach the SVCall and Hard Fault exception handlers */

  irq_attach(NVIC_IRQ_SVCALL, arm_svcall, NULL);
  irq_attach(NVIC_IRQ_HARDFAULT, arm_hardfault, NULL);

  /* And finally, enable interrupts */

  up_irq_enable();
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the IRQ specified by 'irq'
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  int nvic = irq - NVIC_IRQ_FIRST;
  if (nvic < 0)
    {
      /* Ignore attempts to enable exceptions */

      return;
    }

  putreg32(1 << (nvic & 0x1f), NVIC_IRQ_ENABLE(nvic));
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the IRQ specified by 'irq'
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  int nvic = irq - NVIC_IRQ_FIRST;
  if (nvic < 0)
    {
      /* Ignore attempts to disable exceptions */

      return;
    }

  putreg32(1 << (nvic & 0x1f), NVIC_IRQ_CLEAR(nvic));
}

/****************************************************************************
 * Name: arm_ack_irq
 *
 * Description:
 *   Acknowledge the IRQ
 *
 ****************************************************************************/

void arm_ack_irq(int irq)
{
  /* Nothing to do on ARMv8-M */
}
