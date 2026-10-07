/****************************************************************************
 * vendor/st/boards/stm32n6/atk-dnn647/include/board.h
 *
 * Board-level definitions for ATK-DNN647 (STM32N647X0H3Q).
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

#ifndef __VENDOR_ST_BOARDS_STM32N6_ATK_DNN647_INCLUDE_BOARD_H
#define __VENDOR_ST_BOARDS_STM32N6_ATK_DNN647_INCLUDE_BOARD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clocking *****************************************************************
 *
 * ATK-DNN647 board clock sources:
 *   HSE: 48 MHz from external crystal (PH0/PH1)
 *   HSI: 64 MHz internal RC
 *   LSE: 32.768 kHz from external crystal (PC14/PC15)
 *   LSI: 32 kHz internal RC
 *
 * PLL1 Configuration (HSE -> 800 MHz):
 *   ref_clk  = HSE / PLLM = 48 / 6 = 8 MHz
 *   PLL1_VCO = ref_clk * PLLN = 8 * 100 = 800 MHz
 *   PLL1P    = PLL1_VCO / PLLP1 = 800 / 1 = 800 MHz -> SYSCLK
 *   CPUCLK   = SYSCLK = 800 MHz
 */

#define STM32N6_HSE_FREQUENCY     48000000UL
#define STM32N6_HSI_FREQUENCY     64000000UL
#define STM32N6_LSE_FREQUENCY     32768UL
#define STM32N6_LSI_FREQUENCY     32000UL

#define STM32N6_PLL1M             6
#define STM32N6_PLL1N             100
#define STM32N6_PLL1P             1

#define STM32N6_SYSCLK_FREQUENCY  800000000UL
#define STM32N6_CPUCLK_FREQUENCY  STM32N6_SYSCLK_FREQUENCY

/* LED definitions **********************************************************
 *
 * LED0: PG10 (active low, on core board CNN647B)
 * LED1: PE10 (active low, on base board DNN647)
 */

#define BOARD_LED0                0
#define BOARD_LED1                1
#define BOARD_NLEDS               2

#define BOARD_LED0_BIT            (1 << BOARD_LED0)
#define BOARD_LED1_BIT            (1 << BOARD_LED1)

#define LED_STARTED               0
#define LED_HEAPALLOCATE          1
#define LED_IRQSENABLED           2
#define LED_STACKCREATED          3
#define LED_INIRQ                 4
#define LED_SIGNAL                5
#define LED_ASSERTION             6
#define LED_PANIC                 7

/* Button definitions *******************************************************
 *
 * KEY0: PC6 (active low), KEY1: PD1, KEY2: PG11, WK_UP: PC13
 */

#define BUTTON_KEY0               0
#define BUTTON_KEY1               1
#define BUTTON_KEY2               2
#define BUTTON_WK_UP              3
#define NUM_BUTTONS               4

/* USART1 (Console) - PE5(TX), PE6(RX), AF7 */

#define GPIO_USART1_TX            (GPIO_USART1_TX_5 | GPIO_SPEED_100MHz)
#define GPIO_USART1_RX            (GPIO_USART1_RX_5 | GPIO_SPEED_100MHz)

#endif /* __VENDOR_ST_BOARDS_STM32N6_ATK_DNN647_INCLUDE_BOARD_H */

