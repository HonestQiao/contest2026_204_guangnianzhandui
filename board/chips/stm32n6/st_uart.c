/****************************************************************************
 * vendor/st/chips/stm32n6/st_uart.c
 *
 * STM32N6 USART1 console driver for ATK-DNN647 board.
 * USART1: PE5(TX), PE6(RX), AF7, 115200 8N1
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
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/serial/serial.h>

#include <arch/board/board.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* USART1 base address */

#define STM32N6_USART1_BASE    0x40011000UL

/* USART register offsets */

#define USART_CR1              0x00
#define USART_CR2              0x04
#define USART_CR3              0x08
#define USART_BRR              0x0C
#define USART_ISR              0x1C
#define USART_ICR              0x20
#define USART_RDR              0x24
#define USART_TDR              0x28

/* USART_CR1 bits */

#define USART_CR1_UE           (1 << 0)
#define USART_CR1_RE           (1 << 2)
#define USART_CR1_TE           (1 << 3)
#define USART_CR1_RXNEIE       (1 << 5)

/* USART_ISR bits */

#define USART_ISR_PE           (1 << 0)
#define USART_ISR_FE           (1 << 1)
#define USART_ISR_NF           (1 << 2)
#define USART_ISR_ORE          (1 << 3)
#define USART_ISR_IDLE         (1 << 4)
#define USART_ISR_RXNE         (1 << 5)
#define USART_ISR_TC           (1 << 6)
#define USART_ISR_TXE          (1 << 7)
#define USART_ISR_TEACK        (1 << 21)
#define USART_ISR_REACK        (1 << 22)

/* GPIO base addresses */

#define STM32N6_GPIOE_BASE     0x40021400UL

/* GPIO register offsets */

#define GPIO_MODER             0x00
#define GPIO_OSPEEDR           0x08
#define GPIO_PUPDR             0x0C
#define GPIO_AFRL              0x20

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  st_uart_setup(struct uart_dev_s *dev);
static void st_uart_shutdown(struct uart_dev_s *dev);
static int  st_uart_attach(struct uart_dev_s *dev);
static void st_uart_detach(struct uart_dev_s *dev);
static int  st_uart_receive(struct uart_dev_s *dev, unsigned int *status);
static void st_uart_rxint(struct uart_dev_s *dev, bool enable);
static bool st_uart_rxavailable(struct uart_dev_s *dev);
static void st_uart_send(struct uart_dev_s *dev, int ch);
static void st_uart_txint(struct uart_dev_s *dev, bool enable);
static bool st_uart_txready(struct uart_dev_s *dev);
static bool st_uart_txempty(struct uart_dev_s *dev);

/****************************************************************************
 * Private Data ****************************************************************************/

static const struct uart_ops_s g_uart_ops =
{
  .setup       = st_uart_setup,
  .shutdown    = st_uart_shutdown,
  .attach      = st_uart_attach,
  .detach      = st_uart_detach,
  .receive     = st_uart_receive,
  .rxint       = st_uart_rxint,
  .rxavailable = st_uart_rxavailable,
  .send        = st_uart_send,
  .txint       = st_uart_txint,
  .txready     = st_uart_txready,
  .txempty     = st_uart_txempty,
};

/* USART1 console */

static char g_uart1rxbuffer[256];
static char g_uart1txbuffer[256];

static struct uart_dev_s g_uart1port =
{
  .isconsole = true,
  .ops       = &g_uart_ops,
  .recv      =
  {
    .size    = 256,
    .buffer  = g_uart1rxbuffer,
  },
  .xmit      =
  {
    .size    = 256,
    .buffer  = g_uart1txbuffer,
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline void st_uart_putreg(uint32_t offset, uint32_t value)
{
  putreg32(value, STM32N6_USART1_BASE + offset);
}

static inline uint32_t st_uart_getreg(uint32_t offset)
{
  return getreg32(STM32N6_USART1_BASE + offset);
}

static int st_uart_setup(struct uart_dev_s *dev)
{
  uint32_t reg;

  /* Configure GPIO: PE5=TX, PE6=RX as AF7 */

  reg = getreg32(STM32N6_GPIOE_BASE + GPIO_MODER);
  reg &= ~(0x3 << (5 * 2)) & ~(0x3 << (6 * 2));
  reg |= (0x2 << (5 * 2)) | (0x2 << (6 * 2));  /* Alternate function */
  putreg32(reg, STM32N6_GPIOE_BASE + GPIO_MODER);

  reg = getreg32(STM32N6_GPIOE_BASE + GPIO_OSPEEDR);
  reg |= (0x3 << (5 * 2)) | (0x3 << (6 * 2));  /* Very high speed */
  putreg32(reg, STM32N6_GPIOE_BASE + GPIO_OSPEEDR);

  reg = getreg32(STM32N6_GPIOE_BASE + GPIO_PUPDR);
  reg &= ~(0x3 << (5 * 2)) & ~(0x3 << (6 * 2));
  reg |= (0x1 << (5 * 2)) | (0x1 << (6 * 2));  /* Pull-up */
  putreg32(reg, STM32N6_GPIOE_BASE + GPIO_PUPDR);

  /* Set AF7 for PE5 (AFRL[5]) and PE6 (AFRL[6]) */

  reg = getreg32(STM32N6_GPIOE_BASE + GPIO_AFRL);
  reg &= ~(0xf << (5 * 4)) & ~(0xf << (6 * 4));
  reg |= (7 << (5 * 4)) | (7 << (6 * 4));
  putreg32(reg, STM32N6_GPIOE_BASE + GPIO_AFRL);

  /* Configure USART1: 115200 8N1, using HSI (64 MHz) as kernel clock */

  st_uart_putreg(USART_CR1, 0);  /* Disable USART */

  /* BRR = fck / baud = 64000000 / 115200 = 556 (approx) */

  st_uart_putreg(USART_BRR, 556);

  /* Enable TX, RX, USART */

  st_uart_putreg(USART_CR1, USART_CR1_UE | USART_CR1_TE | USART_CR1_RE);

  /* Wait for TE and RE acknowledgment */

  while ((st_uart_getreg(USART_ISR) & (USART_ISR_TEACK | USART_ISR_REACK))
         != (USART_ISR_TEACK | USART_ISR_REACK));

  return OK;
}

static void st_uart_shutdown(struct uart_dev_s *dev)
{
  st_uart_putreg(USART_CR1, 0);
}

static int st_uart_attach(struct uart_dev_s *dev)
{
  return OK;
}

static void st_uart_detach(struct uart_dev_s *dev)
{
}

static int st_uart_receive(struct uart_dev_s *dev, unsigned int *status)
{
  uint32_t isr = st_uart_getreg(USART_ISR);
  *status = isr;
  return st_uart_getreg(USART_RDR) & 0xff;
}

static void st_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  uint32_t cr1 = st_uart_getreg(USART_CR1);
  if (enable)
    {
      cr1 |= USART_CR1_RXNEIE;
    }
  else
    {
      cr1 &= ~USART_CR1_RXNEIE;
    }
  st_uart_putreg(USART_CR1, cr1);
}

static bool st_uart_rxavailable(struct uart_dev_s *dev)
{
  return (st_uart_getreg(USART_ISR) & USART_ISR_RXNE) != 0;
}

static void st_uart_send(struct uart_dev_s *dev, int ch)
{
  while (!(st_uart_getreg(USART_ISR) & USART_ISR_TXE));
  st_uart_putreg(USART_TDR, ch);
}

static void st_uart_txint(struct uart_dev_s *dev, bool enable)
{
}

static bool st_uart_txready(struct uart_dev_s *dev)
{
  return (st_uart_getreg(USART_ISR) & USART_ISR_TXE) != 0;
}

static bool st_uart_txempty(struct uart_dev_s *dev)
{
  return (st_uart_getreg(USART_ISR) & USART_ISR_TC) != 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_earlyserialinit
 *
 * Description:
 *   Performs the low level USART initialization early in debug so that the
 *   serial console will be available during bootup.
 *
 ****************************************************************************/

void arm_earlyserialinit(void)
{
  /* Setup USART1 for early console */

  st_uart_setup(&g_uart1port);
}

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   Register serial console and serial ports.
 *
 ****************************************************************************/

void arm_serialinit(void)
{
  uart_register("/dev/console", &g_uart1port);
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Provide priority, low level access to support OS debug writes
 *
 ****************************************************************************/

void up_putc(int ch)
{
  while (!(st_uart_getreg(USART_ISR) & USART_ISR_TXE));
  st_uart_putreg(USART_TDR, ch);
}
