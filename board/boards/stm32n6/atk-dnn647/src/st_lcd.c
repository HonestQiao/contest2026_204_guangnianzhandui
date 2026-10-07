/****************************************************************************
 * boards/stm32n6/atk-dnn647/src/st_lcd.c
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
 * ATK-DNN647 LCD bring-up: 4.3" 800x480 RGB panel on STM32N6 LTDC.
 *
 * Pin map from the Alientek ATK-CNN647B IO assignment table:
 *   CLK=PA5  DE=PG13  HSYNC=PF9  VSYNC=PG0
 *   R0=PG2  R1=PB5  R2=PB4  R3=PA0  R4=PH4  R5=PA15  R6=PF8  R7=PG9
 *   G0=PG12 G1=PG1  G2=PA1  G3=PA0* G4=PB15 G5=PB12 G6=PB11 G7=PB10
 *   B0=PG15 B1=PA7  B2=PA12 B3=PA11 B4=PA10 B5=PA9  B6=PA8  B7=PA2
 *   BL=PA3 (TIM16_CH1)
 *
 * * PA0 carries R3 per the table note (color-map order to be verified on
 *   hardware bring-up; red/blue swap is a one-line fix here if needed).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <syslog.h>

#ifdef CONFIG_VIDEO_FB
#include <nuttx/video/fb.h>
#endif

#include "chip.h"
#include "arm_internal.h"
#include "include/st_ltdc.h"

#include "../include/board_lcd.h"

#ifdef CONFIG_STM32N6_LTDC

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* { port base, pin, AF } */

static const struct st_ltdc_pins_s g_lcd_pins[] =
{
  { BOARD_GPIOA_BASE,  5, BOARD_LCD_GPIO_AF },  /* CLK   */
  { BOARD_GPIOG_BASE, 13, BOARD_LCD_GPIO_AF },  /* DE    */
  { BOARD_GPIOF_BASE,  9, BOARD_LCD_GPIO_AF },  /* HSYNC */
  { BOARD_GPIOG_BASE,  0, BOARD_LCD_GPIO_AF },  /* VSYNC */
  { BOARD_GPIOG_BASE,  2, BOARD_LCD_GPIO_AF },  /* R0    */
  { BOARD_GPIOB_BASE,  5, BOARD_LCD_GPIO_AF },  /* R1    */
  { BOARD_GPIOB_BASE,  4, BOARD_LCD_GPIO_AF },  /* R2    */
  { BOARD_GPIOA_BASE,  0, BOARD_LCD_GPIO_AF },  /* R3    */
  { BOARD_GPIOH_BASE,  4, BOARD_LCD_GPIO_AF },  /* R4    */
  { BOARD_GPIOA_BASE, 15, BOARD_LCD_GPIO_AF },  /* R5    */
  { BOARD_GPIOF_BASE,  8, BOARD_LCD_GPIO_AF },  /* R6    */
  { BOARD_GPIOG_BASE,  9, BOARD_LCD_GPIO_AF },  /* R7    */
  { BOARD_GPIOG_BASE, 12, BOARD_LCD_GPIO_AF },  /* G0    */
  { BOARD_GPIOG_BASE,  1, BOARD_LCD_GPIO_AF },  /* G1    */
  { BOARD_GPIOA_BASE,  1, BOARD_LCD_GPIO_AF },  /* G2    */
  { BOARD_GPIOB_BASE, 15, BOARD_LCD_GPIO_AF },  /* G4    */
  { BOARD_GPIOB_BASE, 12, BOARD_LCD_GPIO_AF },  /* G5    */
  { BOARD_GPIOB_BASE, 11, BOARD_LCD_GPIO_AF },  /* G6    */
  { BOARD_GPIOB_BASE, 10, BOARD_LCD_GPIO_AF },  /* G7    */
  { BOARD_GPIOG_BASE, 15, BOARD_LCD_GPIO_AF },  /* B0    */
  { BOARD_GPIOA_BASE,  7, BOARD_LCD_GPIO_AF },  /* B1    */
  { BOARD_GPIOA_BASE, 12, BOARD_LCD_GPIO_AF },  /* B2    */
  { BOARD_GPIOA_BASE, 11, BOARD_LCD_GPIO_AF },  /* B3    */
  { BOARD_GPIOA_BASE, 10, BOARD_LCD_GPIO_AF },  /* B4    */
  { BOARD_GPIOA_BASE,  9, BOARD_LCD_GPIO_AF },  /* B5    */
  { BOARD_GPIOA_BASE,  8, BOARD_LCD_GPIO_AF },  /* B6    */
  { BOARD_GPIOA_BASE,  2, BOARD_LCD_GPIO_AF },  /* B7    */
};

static const struct st_ltdc_config_s g_lcd_cfg =
{
  .timing =
  {
    .hsw = BOARD_LCD_HSW,
    .hbp = BOARD_LCD_HBP,
    .hfp = BOARD_LCD_HFP,
    .vsw = BOARD_LCD_VSW,
    .vbp = BOARD_LCD_VBP,
    .vfp = BOARD_LCD_VFP,
    .width = BOARD_LCD_WIDTH,
    .height = BOARD_LCD_HEIGHT,
  },
  .pins   = g_lcd_pins,
  .npins  = sizeof(g_lcd_pins) / sizeof(g_lcd_pins[0]),
  .pixfmt = BOARD_LCD_LTDC_PIXFMT,
  .bpp    = BOARD_LCD_BPP,
  .fb_base = BOARD_LCD_FB_BASE,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_lcd_backlight
 ****************************************************************************/

static void board_lcd_backlight(void)
{
  uint32_t reg;
  uint32_t val;

  /* PA3 as push-pull output, full brightness for bring-up */

  reg = BOARD_LCD_BL_PORT + 0x00;  /* MODER */
  val = getreg32(reg);
  val &= ~(3 << (BOARD_LCD_BL_PIN * 2));
  val |=  (1 << (BOARD_LCD_BL_PIN * 2));
  putreg32(val, reg);

  reg = BOARD_LCD_BL_PORT + 0x04;  /* OTYPER: push-pull */
  val = getreg32(reg);
  val &= ~(1 << BOARD_LCD_BL_PIN);
  putreg32(val, reg);

#if BOARD_LCD_BL_ACTIVE_HIGH
  putreg32(1 << BOARD_LCD_BL_PIN, BOARD_LCD_BL_PORT + 0x18); /* BSRR set */
#else
  putreg32(1 << (BOARD_LCD_BL_PIN + 16), BOARD_LCD_BL_PORT + 0x18);
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: atk_dnn647_lcd_init
 *
 * Description:
 *   Initialize the ATK 4.3" RGB LCD (backlight GPIO + LTDC controller +
 *   framebuffer registration).  Called from atk_dnn647_bringup().
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int atk_dnn647_lcd_init(void)
{
  int ret;

  board_lcd_backlight();

  ret = st_ltdc_init(&g_lcd_cfg);
  if (ret < 0)
    {
      return ret;
    }

#ifdef CONFIG_VIDEO_FB
  /* Register /dev/fb0; fb_register() picks up the vtable via
   * up_fbgetvplane() provided by the chip layer.
   */

  ret = fb_register(0, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: fb_register failed: %d\n", ret);
      return ret;
    }
#endif

  return OK;
}

#endif /* CONFIG_STM32N6_LTDC */
