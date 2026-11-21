/****************************************************************************
 * packages/demos/led/led_main.c
 *
 * Custom LED control demo for ATK-DNN647 (STM32N647).
 *
 * Usage:
 *   leds            - blink all LEDs
 *   leds <n>        - blink LED n (n=0 or 1)
 *   leds <n> on     - turn LED n on
 *   leds <n> off    - turn LED n off
 *   leds <n> blink  - blink LED n (until Ctrl+C)
 *
 * The LED lower half is registered by vendor/st/boards/stm32n6/atk-dnn647/
 * src/st_userled.c which provides /dev/userleds.
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>

#include <nuttx/leds/userled.h>

#ifndef CONFIG_APP_DEMOS_LED_DEVPATH
#  define CONFIG_APP_DEMOS_LED_DEVPATH "/dev/userleds"
#endif

#define DEFAULT_BLINK_COUNT  10
#define BLINK_DELAY_US       300000   /* 300 ms */

static volatile bool g_blink_run = true;

static void sigint_handler(int signo)
{
  g_blink_run = false;
}

static void usage(void)
{
  printf("Usage: leds [<led> <on|off|blink>]\n");
  printf("  leds                blink all LEDs\n");
  printf("  leds 0|1            blink LED 0 (PG10) or LED 1 (PE10)\n");
  printf("  leds 0|1 on         turn the LED on\n");
  printf("  leds 0|1 off        turn the LED off\n");
  printf("  leds 0|1 blink      blink until Ctrl+C\n");
}

int main(int argc, FAR char *argv[])
{
  int fd;
  userled_set_t ledset;
  userled_set_t mask;
  int led = -1;               /* -1 = all LEDs */
  const char *action = "blink";
  int i;
  struct sigaction sa;

  /* Parse arguments */
  if (argc > 3)
    {
      usage();
      return EXIT_FAILURE;
    }

  if (argc == 2)
    {
      if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
        {
          usage();
          return EXIT_SUCCESS;
        }

      led = atoi(argv[1]);
      if (led < 0 || led > 1)
        {
          printf("leds: invalid LED number %d (must be 0 or 1)\n", led);
          usage();
          return EXIT_FAILURE;
        }
    }
  else if (argc == 3)
    {
      led = atoi(argv[1]);
      if (led < 0 || led > 1)
        {
          printf("leds: invalid LED number %d (must be 0 or 1)\n", led);
          usage();
          return EXIT_FAILURE;
        }

      action = argv[2];
      if (strcmp(action, "on") != 0 && strcmp(action, "off") != 0 &&
          strcmp(action, "blink") != 0)
        {
          printf("leds: invalid action '%s' (use on|off|blink)\n", action);
          usage();
          return EXIT_FAILURE;
        }
    }

  /* Build the LED mask: bit0 = LED0, bit1 = LED1 */
  if (led < 0)
    {
      mask = 0x3;   /* all LEDs */
    }
  else
    {
      mask = (userled_set_t)(1UL << led);
    }

  /* Open the LED driver */
  fd = open(CONFIG_APP_DEMOS_LED_DEVPATH, O_WRONLY);
  if (fd < 0)
    {
      printf("leds: failed to open %s: %d\n",
             CONFIG_APP_DEMOS_LED_DEVPATH, errno);
      return EXIT_FAILURE;
    }

  /* Install SIGINT handler for clean Ctrl+C exit from blink */
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = sigint_handler;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGINT, &sa, NULL);
  g_blink_run = true;

  if (strcmp(action, "on") == 0)
    {
      ledset = mask;
      ioctl(fd, ULEDIOC_SETALL, (unsigned long)ledset);
      printf("leds: LED(s) 0x%x ON\n", (unsigned int)mask);
    }
  else if (strcmp(action, "off") == 0)
    {
      ioctl(fd, ULEDIOC_SETALL, (unsigned long)0);
      printf("leds: LED(s) 0x%x OFF\n", (unsigned int)mask);
    }
  else  /* blink */
    {
      printf("leds: blinking LED(s) 0x%x, %d cycles (Ctrl+C to stop)\n",
             (unsigned int)mask, DEFAULT_BLINK_COUNT);
      for (i = 0; i < DEFAULT_BLINK_COUNT && g_blink_run; i++)
        {
          ioctl(fd, ULEDIOC_SETALL, (unsigned long)mask);
          usleep(BLINK_DELAY_US);
          if (!g_blink_run)
            {
              break;
            }

          ioctl(fd, ULEDIOC_SETALL, (unsigned long)0);
          usleep(BLINK_DELAY_US);
        }

      ioctl(fd, ULEDIOC_SETALL, (unsigned long)0);  /* ensure off */
      printf("leds: blink done\n");
    }

  close(fd);
  return EXIT_SUCCESS;
}
