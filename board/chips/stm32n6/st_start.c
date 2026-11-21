/****************************************************************************
 * vendor/st/chips/stm32n6/st_start.c
 *
 * STM32N6 chip startup code for ATK-DNN647 board (zero-copy serial boot).
 *
 * The .data VMA = LMA (both in FLEXRAM), so no runtime .data copy is
 * needed. Only .bss zeroing is required. The heap and all stacks live
 * within the FLEXRAM writable window.
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

#include <nuttx/arch.h>
#include <nuttx/init.h>

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern uint32_t _ebss[];
extern uint32_t _estack[];

/* vector[0] (initial MSP, per arm_m/arm_vectors.c IDLE_STACK macro) =
 * _ebss + CONFIG_IDLETHREAD_STACKSIZE.  The boot ROM sizes its writable
 * RAM window by this address, so CONFIG_IDLETHREAD_STACKSIZE must be
 * large enough to cover all .data + .bss + heap + stacks. */

const uintptr_t g_idle_topstack =
  (uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE;

/* Vector table address (provided by the ld.script). */
extern uint32_t _vectors[];

/* Linker-provided symbols for memory initialization (see ld.script).
 * With zero-copy .data (VMA == LMA), _eronly == _sdata and the copy
 * below is a no-op. */
extern uint32_t _eronly[];
extern uint32_t _sdata[];
extern uint32_t _edata[];
extern uint32_t _sbss[];
extern uint32_t _ebss[];

/* Provided by arch/arm/src/armv8-m/arm_fpuconfig.c. */
void arm_fpuconfig(void);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __n6_boot
 *
 * Description:
 *   Entry point branched to by the STM32N6 boot ROM (serial boot): the ROM
 *   copies the image into FLEXRAM and branches to the header entry point
 *   without loading SP from the vector table, so set up the initial stack
 *   here before entering C code.
 *
 ****************************************************************************/

void __n6_boot(void) __attribute__((naked, noinline, used, section(".bootentry")));
void __n6_boot(void)
{
  __asm__ __volatile__
  (
    "ldr    r0,  =_vectors \n" /* vector table address */
    "ldr    sp,  [r0]      \n" /* initial MSP from vector[0] */
    "b      __start        \n"
  );
}

/* Forward declaration (defined below). */

void __start(void)
{
  uint32_t *dst;
  int k;

  /* One-time USART1 configuration: 115200 8E1 at the empirically
   * determined kernel clock (BRR=833 -> 96 MHz).  Done ONCE here; every
   * later output path writes TDR only - reconfiguring per byte would
   * drop UE and create spurious break/start bits on the line. */

  *(volatile uint32_t *)(0x46028000UL + 0x25cUL) |= 0x10UL;
  *(volatile uint32_t *)(0x46028000UL + 0x26cUL) |= 0x10UL;
  /* Keep the boot ROM's own BRR (it is proven correct for 115200 by the
   * download itself) and match its 8E1 format exactly.  Writing BRR=833
   * assumed a 96 MHz kernel clock - if the clock differs, the baud is
   * wrong and every trace byte is lost. */

  /* ===== b288 rollback: ROM already configured PE5/PE6 as AF7 ===== */
#if 0
  /* USART1 pins: PE5 = TX, PE6 = RX, AF7.
   *
   * Two reasons this has to happen here:
   *   1. The boot ROM leaves PE6 in analog mode after the serial download
   *      (TX keeps working because the ROM did configure PE5).
   *   2. arm_earlyserialinit() is never called in this port, so the GPIO
   *      setup in st_uart_setup() never runs, and the console path skips
   *      uart_setup() anyway.
   * The GPIOE peripheral clock must be enabled first, otherwise every
   * GPIO register write is ignored while the peripheral is in reset. */
  {
    volatile uint32_t *ahb4enr = (volatile uint32_t *)(0x46028000UL + 0x25cUL);
    volatile uint32_t *moder   = (volatile uint32_t *)0x46021000UL;
    volatile uint32_t *ospeedr = (volatile uint32_t *)(0x46021000UL + 0x08UL);
    volatile uint32_t *pupdr   = (volatile uint32_t *)(0x46021000UL + 0x0cUL);
    volatile uint32_t *afrl    = (volatile uint32_t *)(0x46021000UL + 0x20UL);
    uint32_t m;
    int n;

    *ahb4enr |= (1UL << 4);      /* GPIOE clock enable (AHB4ENR bit 4) */

    for (n = 0; n < 1000; n++)   /* let the clock settle */
      {
        __asm__ __volatile__ ("nop");
      }

    m = *moder;
    m &= ~(3UL << 10); m |= (2UL << 10);   /* PE5 = alternate function */
    m &= ~(3UL << 12); m |= (2UL << 12);   /* PE6 = alternate function */
    *moder = m;

    /* Official ATK/STM32 demo settings: GPIO_MODE_AF_PP, GPIO_NOPULL,
     * GPIO_SPEED_FREQ_LOW.  The pull-up + very-high-speed combination we
     * used first distorted the RX line. */
    m = *ospeedr;
    m &= ~(3UL << 10) & ~(3UL << 12);      /* SPEED_FREQ_LOW */
    *ospeedr = m;

    m = *pupdr;
    m &= ~(3UL << 10) & ~(3UL << 12);      /* GPIO_NOPULL */
    *pupdr = m;

    m = *afrl;
    m &= ~(0xfUL << 20); m |= (7UL << 20);  /* PE5 = AF7 (USART1_TX) */
    m &= ~(0xfUL << 24); m |= (7UL << 24);  /* PE6 = AF7 (USART1_RX) */
    *afrl = m;
  }

#endif
  /* ===== end rollback ===== */

  *(volatile uint32_t *)0x42011000UL = 0;                   /* CR1 off */
  *(volatile uint32_t *)0x42011000UL = 0x140dUL;             /* UE|RE|TE|PCE|M0 (8E1) - b288 */

  /* Bounded wait for TEACK: an unbounded wait can hang the boot (the
   * ack may never arrive if the peripheral clock was not settled yet). */

  {
    uint32_t guard = 2000000;

    while (((*(volatile uint32_t *)(0x42011000UL + 0x1cUL) & 0x200000UL) == 0) &&
           guard-- > 0)
      {
      }
  }

  /* ===== PE6 = USART1_RX(AF7) — RX fix (reply7, verified RCV 1->24116) ===== */
  {
    volatile uint32_t *ahb4enr = (volatile uint32_t *)(0x46028000UL + 0x25CUL);
    volatile uint32_t *moder   = (volatile uint32_t *)0x46021000UL;
    volatile uint32_t *pupdr   = (volatile uint32_t *)(0x46021000UL + 0x0CUL);
    volatile uint32_t *afrl    = (volatile uint32_t *)(0x46021000UL + 0x20UL);
    uint32_t m;
    int i;

    if (!(*ahb4enr & (1UL << 4)))        /* GPIOE clock if not already on */
      {
        *ahb4enr |= (1UL << 4);
        for (i = 0; i < 1000; i++) { __asm__ __volatile__ ("nop"); }
      }
    m = *afrl;  m &= ~(0xFUL << 24); m |= (7UL << 24); *afrl = m;  /* PE6 AF7 */
    m = *pupdr; m &= ~(3UL << 12); *pupdr = m;                     /* no pull */
    m = *moder; m &= ~(3UL << 12); m |= (2UL << 12); *moder = m;   /* PE6 AF */
  }
  /* ===== end PE6 fix ===== */

  /* Zero out BSS. */
  for (dst = _sbss; dst < _ebss; )
    {
      *dst++ = 0;
    }

  /* Scrub what the boot ROM left behind: disable SysTick, disable and
   * clear every NVIC interrupt line, point VTOR at our own vector table
   * so any stray exception is handled by NuttX. */
  *(volatile uint32_t *)0xe000e010UL = 0;   /* SysTick CSR: disable */
  for (k = 0; k < 8; k++)
    {
      *(volatile uint32_t *)(0xe000e180UL + (k * 4)) = 0xffffffffUL; /* ICER */
      *(volatile uint32_t *)(0xe000e200UL + (k * 4)) = 0xffffffffUL; /* ICPR */
    }
  *(volatile uint32_t *)0xe000ed08UL = (uint32_t)_vectors; /* VTOR */
  __asm__ __volatile__ ("dsb" ::: "memory");
  __asm__ __volatile__ ("isb" ::: "memory");

#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

  /* Enable the FPU context support BEFORE the first context switch. */
  arm_fpuconfig();

  nx_start();

  for (; ; );
}
