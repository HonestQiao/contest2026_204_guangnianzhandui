/****************************************************************************
 * boards/stm32n6/atk-dnn647/include/board_lcd.h
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

#ifndef __BOARDS_STM32N6_ATK_DNN647_INCLUDE_BOARD_LCD_H
#define __BOARDS_STM32N6_ATK_DNN647_INCLUDE_BOARD_LCD_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Panel: Alientek 4.3" RGB LCD, 800x480, driven by 24-bit parallel RGB.
 *
 * Timing (ATK 800x480 typical @ ~65 Hz with 33.3 MHz pixel clock):
 *   HSW = 48, HBP = 88, HFP = 40
 *   VSW = 3,  VBP = 32, VFP = 13
 *
 * NOTE: polarity/porch values follow the Alientek examples; verify
 * against the panel datasheet on first hardware bring-up.
 */

#define BOARD_LCD_WIDTH        800
#define BOARD_LCD_HEIGHT       480

#define BOARD_LCD_HSW          48
#define BOARD_LCD_HBP          88
#define BOARD_LCD_HFP          40
#define BOARD_LCD_VSW          3
#define BOARD_LCD_VBP          32
#define BOARD_LCD_VFP          13

/* Pixel format: RGB565 (2 bytes/pixel, 768 KiB frame buffer) */

#define BOARD_LCD_BPP          16
#define BOARD_LCD_LTDC_PIXFMT  2  /* LTDC_LxPFCR: 010 = RGB565 */

/* Frame buffer lives in SRAM2_AXI (1 MB, dedicated to display) */

#define BOARD_LCD_FB_BASE      0x24100000
#define BOARD_LCD_FB_SIZE      (BOARD_LCD_WIDTH * BOARD_LCD_HEIGHT * \
                                BOARD_LCD_BPP / 8)

/* GPIO port base addresses (non-secure, AHB4) */

#define BOARD_GPIOA_BASE       0x42020000
#define BOARD_GPIOB_BASE       0x42020400
#define BOARD_GPIOF_BASE       0x42021400
#define BOARD_GPIOG_BASE       0x42021800
#define BOARD_GPIOH_BASE       0x42021c00

/* LTDC alternate function.
 * ST convention across STM32 families (F4/F7/H7) is AF14 for LTDC; the
 * STM32N647 datasheet must confirm this on first bring-up.  Centralized
 * here so a correction is a one-line change.
 */

#define BOARD_LCD_GPIO_AF      14

/* Backlight: PA3 (TIM16_CH1, PWM capable).  Simple GPIO high = full on
 * for bring-up; PWM dimming can be added later.
 */

#define BOARD_LCD_BL_PORT      BOARD_GPIOA_BASE
#define BOARD_LCD_BL_PIN       3
#define BOARD_LCD_BL_ACTIVE_HIGH 1

#endif /* __BOARDS_STM32N6_ATK_DNN647_INCLUDE_BOARD_LCD_H */
