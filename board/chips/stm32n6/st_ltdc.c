/****************************************************************************
 * vendors/st/chips/stm32n6/st_ltdc.c
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
 * STM32N6 LTDC driver (LTDC v2, parallel RGB).
 *
 * Register base addresses (non-secure, from CMSIS stm32n647xx.h):
 *   RCC   0x42028000  (AHB4 + 0x8000)
 *   LTDC  0x48001000  (APB5 + 0x1000)
 *   LTDC layer 1 at LTDC + 0x100
 *
 * Pixel clock path: PLL1 VCO (800 MHz) -> IC16 (/24) = 33.3 MHz ->
 *   LTDCSEL = IC16.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/video/fb.h>
#include <syslog.h>

#include "include/st_ltdc.h"
#include "arm_internal.h"
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* RCC registers */

#define STM32N6_RCC_BASE       0x42028000
#define RCC_IC16CFGR           (STM32N6_RCC_BASE + 0x100)
#define RCC_CCIPR4             (STM32N6_RCC_BASE + 0x150)
#define RCC_DIVENR             (STM32N6_RCC_BASE + 0x240)
#define RCC_AHB4ENR            (STM32N6_RCC_BASE + 0x25c)
#define RCC_APB5ENR            (STM32N6_RCC_BASE + 0x27c)

#define RCC_AHB4ENR_GPIOAEN    (1 << 0)
#define RCC_AHB4ENR_GPIOBEN    (1 << 1)
#define RCC_AHB4ENR_GPIOFEN    (1 << 5)
#define RCC_AHB4ENR_GPIOGEN    (1 << 6)
#define RCC_AHB4ENR_GPIOHEN    (1 << 7)

#define RCC_APB5ENR_LTDCEN     (1 << 1)

#define RCC_CCIPR4_LTDCSEL_SH  24
#define RCC_CCIPR4_LTDCSEL_MSK (3 << RCC_CCIPR4_LTDCSEL_SH)
#define LTDC_CLKSEL_IC16       (2 << RCC_CCIPR4_LTDCSEL_SH)

#define RCC_IC16CFGR_IC16INT_SH  16
#define RCC_IC16CFGR_IC16INT_MSK (0xff << RCC_IC16CFGR_IC16INT_SH)
#define RCC_IC16CFGR_IC16SEL_SH  28
#define RCC_IC16CFGR_IC16SEL_MSK (3 << RCC_IC16CFGR_IC16SEL_SH)
#define IC16SEL_PLL1             0
#define RCC_DIVENR_IC16EN        (1 << 15)

/* LTDC registers */

#define STM32N6_LTDC_BASE      0x48001000
#define LTDC_SSCR              (STM32N6_LTDC_BASE + 0x08)
#define LTDC_BPCR              (STM32N6_LTDC_BASE + 0x0c)
#define LTDC_AWCR              (STM32N6_LTDC_BASE + 0x10)
#define LTDC_TWCR              (STM32N6_LTDC_BASE + 0x14)
#define LTDC_GCR               (STM32N6_LTDC_BASE + 0x18)
#define LTDC_SRCR              (STM32N6_LTDC_BASE + 0x24)
#define LTDC_BCCR              (STM32N6_LTDC_BASE + 0x2c)

#define LTDC_GCR_LTDCEN        (1 << 0)
#define LTDC_GCR_DEN           (1 << 16)
#define LTDC_GCR_PCPOL         (1 << 28)
#define LTDC_GCR_DEPOL         (1 << 29)
#define LTDC_GCR_VSPOL         (1 << 30)
#define LTDC_GCR_HSPOL         (1 << 31)

#define LTDC_SRCR_IMR          (1 << 0)

/* LTDC layer 1 registers (absolute offsets) */

#define LTDC_L1_CR             (STM32N6_LTDC_BASE + 0x10c)
#define LTDC_L1_WHPCR          (STM32N6_LTDC_BASE + 0x110)
#define LTDC_L1_WVPCR          (STM32N6_LTDC_BASE + 0x114)
#define LTDC_L1_PFCR           (STM32N6_LTDC_BASE + 0x11c)
#define LTDC_L1_CACR           (STM32N6_LTDC_BASE + 0x120)
#define LTDC_L1_DCCR           (STM32N6_LTDC_BASE + 0x124)
#define LTDC_L1_BFCR           (STM32N6_LTDC_BASE + 0x128)
#define LTDC_L1_CFBAR          (STM32N6_LTDC_BASE + 0x134)
#define LTDC_L1_CFBLR          (STM32N6_LTDC_BASE + 0x138)
#define LTDC_L1_CFBLNR         (STM32N6_LTDC_BASE + 0x13c)

#define LTDC_L1CR_LEN          (1 << 0)

/* GPIO registers (standard STM32 layout) */

#define GPIO_MODER(base)       ((base) + 0x00)
#define GPIO_AFR0(base)        ((base) + 0x20)
#define GPIO_AFR1(base)        ((base) + 0x24)

#ifndef CONFIG_STM32N6_LTDC_IC16_DIV
#  define CONFIG_STM32N6_LTDC_IC16_DIV 24   /* 800 MHz / 24 = 33.3 MHz */
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct st_ltdc_state_s
{
  struct fb_vtable_s vtable;
  struct fb_videoinfo_s vinfo;
  struct fb_planeinfo_s pinfo;
  struct st_ltdc_timing_s timing;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct st_ltdc_state_s g_ltdc;

/****************************************************************************
 * Name: st_ltdc_gpioconfig
 ****************************************************************************/

static void st_ltdc_gpioconfig(FAR const struct st_ltdc_pins_s *pins,
                               int npins)
{
  int i;

  /* Enable all GPIO port clocks used by LTDC (ports A,B,F,G,H) */

  modifyreg32(RCC_AHB4ENR, 0,
              RCC_AHB4ENR_GPIOAEN | RCC_AHB4ENR_GPIOBEN |
              RCC_AHB4ENR_GPIOFEN | RCC_AHB4ENR_GPIOGEN |
              RCC_AHB4ENR_GPIOHEN);

  for (i = 0; i < npins; i++)
    {
      uint32_t base = pins[i].gpio_base;
      uint32_t pin  = pins[i].pin;
      uint32_t af   = pins[i].af;
      uint32_t reg;
      uint32_t val;

      /* Alternate function mode */

      reg = GPIO_MODER(base);
      val = getreg32(reg);
      val &= ~(3 << (pin * 2));
      val |=  (2 << (pin * 2));
      putreg32(val, reg);

      /* Alternate function select */

      if (pin < 8)
        {
          reg = GPIO_AFR0(base);
          val = getreg32(reg);
          val &= ~(0xf << (pin * 4));
          val |=  (af   << (pin * 4));
          putreg32(val, reg);
        }
      else
        {
          reg = GPIO_AFR1(base);
          val = getreg32(reg);
          val &= ~(0xf << ((pin - 8) * 4));
          val |=  (af   << ((pin - 8) * 4));
          putreg32(val, reg);
        }
    }
}

/****************************************************************************
 * Name: st_ltdc_clockconfig
 ****************************************************************************/

static void st_ltdc_clockconfig(void)
{
  uint32_t regval;

  /* LTDC peripheral clock (APB5) */

  modifyreg32(RCC_APB5ENR, 0, RCC_APB5ENR_LTDCEN);

  /* IC16 <- PLL1, integer divider */

  regval  = getreg32(RCC_IC16CFGR);
  regval &= ~(RCC_IC16CFGR_IC16SEL_MSK | RCC_IC16CFGR_IC16INT_MSK);
  regval |= (IC16SEL_PLL1 << RCC_IC16CFGR_IC16SEL_SH) |
            (CONFIG_STM32N6_LTDC_IC16_DIV << RCC_IC16CFGR_IC16INT_SH);
  putreg32(regval, RCC_IC16CFGR);
  modifyreg32(RCC_DIVENR, 0, RCC_DIVENR_IC16EN);

  /* LTDC kernel clock <- IC16 */

  regval  = getreg32(RCC_CCIPR4);
  regval &= ~RCC_CCIPR4_LTDCSEL_MSK;
  regval |= LTDC_CLKSEL_IC16;
  putreg32(regval, RCC_CCIPR4);
}

/****************************************************************************
 * Name: st_ltdc_controllerconfig
 ****************************************************************************/

static void st_ltdc_controllerconfig(FAR const struct st_ltdc_config_s *cfg)
{
  FAR const struct st_ltdc_timing_s *t = &cfg->timing;
  uint32_t accumulated;
  uint32_t stride;
  uint32_t total;

  /* SSCR: synchronization size */

  putreg32((t->vsw - 1) | ((t->hsw - 1) << 16), LTDC_SSCR);

  /* BPCR: back porch position (sync + back porch - 1) */

  accumulated = t->hsw + t->hbp - 1;
  putreg32((t->vsw + t->vbp - 1) | (accumulated << 16), LTDC_BPCR);

  /* AWCR: active width end (sync + back porch + width - 1) */

  accumulated = t->hsw + t->hbp + t->width - 1;
  putreg32((t->vsw + t->vbp + t->height - 1) | (accumulated << 16),
           LTDC_AWCR);

  /* TWCR: total width (sync + back porch + width + front porch - 1) */

  total = t->hsw + t->hbp + t->width + t->hfp - 1;
  putreg32((t->vsw + t->vbp + t->height + t->vfp - 1) | (total << 16),
           LTDC_TWCR);

  /* Background color: black */

  putreg32(0, LTDC_BCCR);

  /* Layer window: starts at first active pixel (accumulated + 1),
   * ends at accumulated + width/height (register +1 encoding).
   */

  accumulated = t->hsw + t->hbp;
  putreg32(((accumulated + t->width) << 16) | (accumulated + 1),
           LTDC_L1_WHPCR);

  accumulated = t->vsw + t->vbp;
  putreg32(((accumulated + t->height) << 16) | (accumulated + 1),
           LTDC_L1_WVPCR);

  /* Pixel format, constant alpha = 255, opaque default color */

  putreg32(cfg->pixfmt, LTDC_L1_PFCR);
  putreg32(0xff, LTDC_L1_CACR);
  putreg32(0xff000000, LTDC_L1_DCCR);

  /* Blending: BF1 = constant alpha, BF2 = 1 - constant alpha */

  putreg32((4 << 8) | 5, LTDC_L1_BFCR);

  /* Frame buffer */

  putreg32(cfg->fb_base, LTDC_L1_CFBAR);

  stride = (uint32_t)t->width * cfg->bpp / 8;
  putreg32(((stride + 3) << 16) | (stride + 3), LTDC_L1_CFBLR);
  putreg32(t->height, LTDC_L1_CFBLNR);

  /* Enable layer 1, then the controller.
   * Polarity: DE active high; HSYNC/VSYNC active low; PCLK active high.
   */

  modifyreg32(LTDC_L1_CR, 0, LTDC_L1CR_LEN);
  putreg32(LTDC_GCR_DEPOL | LTDC_GCR_LTDCEN, LTDC_GCR);

  /* Immediate shadow-register reload */

  putreg32(LTDC_SRCR_IMR, LTDC_SRCR);
}

/****************************************************************************
 * Name: st_fb_getvideoinfo / st_fb_getplaneinfo / open / close
 ****************************************************************************/

static int st_fb_getvideoinfo(FAR struct fb_vtable_s *vtable,
                              FAR struct fb_videoinfo_s *vinfo)
{
  memcpy(vinfo, &g_ltdc.vinfo, sizeof(struct fb_videoinfo_s));
  return OK;
}

static int st_fb_getplaneinfo(FAR struct fb_vtable_s *vtable, int planeno,
                              FAR struct fb_planeinfo_s *pinfo)
{
  if (planeno != 0)
    {
      return -EINVAL;
    }

  memcpy(pinfo, &g_ltdc.pinfo, sizeof(struct fb_planeinfo_s));
  return OK;
}

static int st_fb_open(FAR struct fb_vtable_s *vtable)
{
  return OK;
}

static int st_fb_close(FAR struct fb_vtable_s *vtable)
{
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: st_ltdc_init
 ****************************************************************************/

int st_ltdc_init(FAR const struct st_ltdc_config_s *cfg)
{
  uint32_t stride;

  if (cfg == NULL || cfg->pins == NULL)
    {
      return -EINVAL;
    }

  stride = (uint32_t)cfg->timing.width * cfg->bpp / 8;

  /* Controller state */

  memset(&g_ltdc, 0, sizeof(g_ltdc));
  memcpy(&g_ltdc.timing, &cfg->timing, sizeof(struct st_ltdc_timing_s));

  g_ltdc.vtable.getvideoinfo = st_fb_getvideoinfo;
  g_ltdc.vtable.getplaneinfo = st_fb_getplaneinfo;
  g_ltdc.vtable.open         = st_fb_open;
  g_ltdc.vtable.close        = st_fb_close;

  g_ltdc.vinfo.xres    = cfg->timing.width;
  g_ltdc.vinfo.yres    = cfg->timing.height;
  g_ltdc.vinfo.nplanes = 1;
  g_ltdc.vinfo.fmt     = cfg->bpp == 16 ? FB_FMT_RGB16_565 :
                         (cfg->bpp == 24 ? FB_FMT_RGB24 : FB_FMT_RGB32);

  g_ltdc.pinfo.fbmem         = (FAR void *)(uintptr_t)cfg->fb_base;
  g_ltdc.pinfo.fblen         = stride * cfg->timing.height;
  g_ltdc.pinfo.stride        = stride;
  g_ltdc.pinfo.display       = 0;
  g_ltdc.pinfo.bpp           = cfg->bpp;
  g_ltdc.pinfo.xres_virtual  = cfg->timing.width;
  g_ltdc.pinfo.yres_virtual  = cfg->timing.height;

  /* Clear the frame buffer to black */

  memset(g_ltdc.pinfo.fbmem, 0, g_ltdc.pinfo.fblen);

  /* Bring hardware up */

  st_ltdc_clockconfig();
  st_ltdc_gpioconfig(cfg->pins, cfg->npins);
  st_ltdc_controllerconfig(cfg);

  syslog(LOG_INFO, "STM32N6 LTDC: %ux%u @ %u bpp, fb=0x%08lx len=%u\n",
         g_ltdc.vinfo.xres, g_ltdc.vinfo.yres, g_ltdc.pinfo.bpp,
         (unsigned long)cfg->fb_base, (unsigned)g_ltdc.pinfo.fblen);
  return OK;
}

/****************************************************************************
 * Name: up_fbinitialize
 *
 * Description:
 *   NuttX framebuffer arch hook, called by the fb_register() inline in
 *   nuttx/video/fb.h before up_fbgetvplane().  Hardware initialization is
 *   performed by st_ltdc_init() from the board layer; here we only verify
 *   that the controller is up and tolerate repeated calls.
 *
 ****************************************************************************/

int up_fbinitialize(int display)
{
  if (display != 0 || g_ltdc.vinfo.xres == 0)
    {
      return -ENODEV;
    }

  return OK;
}

/****************************************************************************
 * Name: up_fbgetvplane
 *
 * Description:
 *   NuttX framebuffer arch hook: return the vtable for the given display
 *   plane, as populated by st_ltdc_init().
 *
 ****************************************************************************/

FAR struct fb_vtable_s *up_fbgetvplane(int display, int vplane)
{
  if (display != 0 || vplane != 0 || g_ltdc.vinfo.xres == 0)
    {
      return NULL;
    }

  return &g_ltdc.vtable;
}
