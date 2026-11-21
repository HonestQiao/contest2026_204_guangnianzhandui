/****************************************************************************
 * vendor/st/boards/stm32n6/atk-dnn647/src/st_userled.c
 *
 * User LED lower-half driver for ATK-DNN647 (STM32N647).
 *   LED0 = PG10 (active low, core board CNN647B)
 *   LED1 = PE10 (active low, base board DNN647)
 *
 * Registers /dev/userleds directly via userled_register(), so it does NOT
 * depend on CONFIG_USERLED_LOWER or ARCH_HAVE_LEDS (this custom board's
 * Kconfig does not select ARCH_HAVE_LEDS).
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <syslog.h>
#include <errno.h>
#include <nuttx/leds/userled.h>

/* GPIO register bases */
#define GPIOE_BASE      0x46021000UL
#define GPIOG_BASE      0x46021800UL
#define RCC_AHB4ENR     0x4602825cUL
#define GPIO_MODER_OFF  0x00UL
#define GPIO_ODR_OFF    0x14UL
#define GPIO_BSRR_OFF   0x18UL
#define GPIOE_CLK_BIT   (1UL << 4)
#define GPIOG_CLK_BIT   (1UL << 6)

#define REG32(addr)     (*(volatile uint32_t *)(addr))

#define NLEDS 2

/* LED descriptor: port base, pin number */
struct led_desc_s
{
  uint32_t base;
  uint8_t  pin;
};

static const struct led_desc_s g_leds[NLEDS] =
{
  { GPIOG_BASE, 10 },   /* LED0: PG10 */
  { GPIOE_BASE, 10 },   /* LED1: PE10 */
};

/****************************************************************************
 * Lower-half callbacks
 ****************************************************************************/

static userled_set_t userled_supported(FAR const struct userled_lowerhalf_s *lower)
{
  return (userled_set_t)((1UL << NLEDS) - 1);  /* 0x03 */
}

static void userled_setled(FAR const struct userled_lowerhalf_s *lower,
                           int led, bool ledon)
{
  uint32_t base;
  uint8_t  pin;

  if ((unsigned)led >= NLEDS)
    {
      return;
    }
  base = g_leds[led].base;
  pin  = g_leds[led].pin;

  /* Active low: ON -> BSRR BR (pin+16) writes low; OFF -> BS (pin) writes high */
  if (ledon)
    {
      REG32(base + GPIO_BSRR_OFF) = (1UL << (pin + 16));
    }
  else
    {
      REG32(base + GPIO_BSRR_OFF) = (1UL << pin);
    }
}

static void userled_setall(FAR const struct userled_lowerhalf_s *lower,
                           userled_set_t ledset)
{
  int i;
  for (i = 0; i < NLEDS; i++)
    {
      userled_setled(lower, i, (ledset & (1UL << i)) != 0);
    }
}

#ifdef CONFIG_USERLED_LOWER_READSTATE
static void userled_getall(FAR const struct userled_lowerhalf_s *lower,
                           userled_set_t *ledset)
{
  uint32_t set = 0;
  uint32_t m;
  int i;
  for (i = 0; i < NLEDS; i++)
    {
      m = REG32(g_leds[i].base + GPIO_ODR_OFF);
      if ((m & (1UL << g_leds[i].pin)) == 0)  /* active low */
        {
          set |= (1UL << i);
        }
    }
  *ledset = set;
}
#endif

static const struct userled_lowerhalf_s g_userled_lower =
{
  .ll_supported = userled_supported,
  .ll_setled    = userled_setled,
  .ll_setall    = userled_setall,
#ifdef CONFIG_USERLED_LOWER_READSTATE
  .ll_getall    = userled_getall,
#endif
};

/****************************************************************************
 * Name: atk_userled_initialize
 ****************************************************************************/

int atk_userled_initialize(void)
{
  uint32_t m;
  int ret;

  /* Enable GPIOE and GPIOG clocks if not already on */
  if ((REG32(RCC_AHB4ENR) & GPIOG_CLK_BIT) == 0)
    {
      REG32(RCC_AHB4ENR) |= GPIOG_CLK_BIT;
    }
  if ((REG32(RCC_AHB4ENR) & GPIOE_CLK_BIT) == 0)
    {
      REG32(RCC_AHB4ENR) |= GPIOE_CLK_BIT;
    }

  /* Configure PG10 and PE10 as general-purpose output (MODER bit[21:20]=01) */
  m = REG32(GPIOG_BASE + GPIO_MODER_OFF);
  m &= ~(3UL << 20);
  m |=  (1UL << 20);
  REG32(GPIOG_BASE + GPIO_MODER_OFF) = m;

  m = REG32(GPIOE_BASE + GPIO_MODER_OFF);
  m &= ~(3UL << 20);
  m |=  (1UL << 20);
  REG32(GPIOE_BASE + GPIO_MODER_OFF) = m;

  /* Both LEDs OFF initially (active low -> set high) */
  REG32(GPIOG_BASE + GPIO_BSRR_OFF) = (1UL << 10);
  REG32(GPIOE_BASE + GPIO_BSRR_OFF) = (1UL << 10);

  ret = userled_register("/dev/userleds", &g_userled_lower);
  if (ret < 0)
    {
      syslog(LOG_ERR, "atk_userled: register failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "atk_userled: /dev/userleds registered, 2 LEDs (PG10,PE10)\n");
  return OK;
}
