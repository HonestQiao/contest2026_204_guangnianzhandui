/****************************************************************************
 * vendors/st/chips/stm32n6/include/st_ltdc.h
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

#ifndef __VENDOR_ST_CHIPS_STM32N6_INCLUDE_ST_LTDC_H
#define __VENDOR_ST_CHIPS_STM32N6_INCLUDE_ST_LTDC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* LCD panel timing (all values in pixels) */

struct st_ltdc_timing_s
{
  uint16_t hsw;    /* Horizontal synchronization width */
  uint16_t hbp;    /* Horizontal back porch */
  uint16_t hfp;    /* Horizontal front porch */
  uint16_t vsw;    /* Vertical synchronization width */
  uint16_t vbp;    /* Vertical back porch */
  uint16_t vfp;    /* Vertical front porch */
  uint16_t width;  /* Active width */
  uint16_t height; /* Active height */
};

/* One LTDC pin: GPIO port base address, pin number, alternate function */

struct st_ltdc_pins_s
{
  uint32_t gpio_base;  /* GPIO port register base address */
  uint8_t  pin;        /* Pin number 0-15 */
  uint8_t  af;         /* Alternate function number */
};

/* Board supplied LTDC configuration */

struct st_ltdc_config_s
{
  struct st_ltdc_timing_s timing;   /* Panel timing */
  FAR const struct st_ltdc_pins_s *pins;  /* Pin table */
  int npins;                        /* Number of pins */
  uint8_t pixfmt;                   /* LTDC_LxPFCR pixel format value */
  uint8_t bpp;                      /* Bits per pixel for fb planeinfo */
  uint32_t fb_base;                 /* Frame buffer physical base address */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: st_ltdc_init
 *
 * Description:
 *   Clock, pin (via board table) and controller initialization for the
 *   STM32N6 LTDC.  Layer 1 is configured to display the frame buffer at
 *   cfg->fb_base and the NuttX framebuffer driver (/dev/fb0) is
 *   registered when CONFIG_FB is enabled.
 *
 *   Pixel clock defaults to IC16 sourced from PLL1 with the divider given
 *   by CONFIG_STM32N6_LTDC_IC16_DIV (24 => 800 MHz / 24 = 33.3 MHz).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int st_ltdc_init(FAR const struct st_ltdc_config_s *cfg);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __VENDOR_ST_CHIPS_STM32N6_INCLUDE_ST_LTDC_H */
