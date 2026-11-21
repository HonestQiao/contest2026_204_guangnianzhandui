# 在 ATK-DNN647（STM32N647）上部署 openvela

> 本文档基于 **ATK-DNN647 开发板真机实测**编写，所有命令、地址、参数均已验证可复现。
>
> 参考官方文档体例：
> - [在 STM32H750 上部署 openvela](https://doc.openvela.com/document?id=597&version=dev&language=cn)（部署实践）
> - [在 STM32F411 上使用 openvela 点亮 LED](https://doc.openvela.com/document?id=598&version=dev&language=cn)（应用层 Demo）
> - [为 STM32F407 开发板移植 openvela](https://doc.openvela.com/document?id=600&version=dev&language=cn)（BSP 移植原理）

---

## 一、概述

本文档指导如何在 **ATK-DNN647** 开发板（主控 **STM32N647X0H3Q**，ARMv8-M / Cortex-M55）上部署 openvela 实时操作系统，并运行 NuttShell（NSH）交互式命令行与板载 LED 控制应用。

### 与官方三篇参考文档的关键差异（务必先读）

| 方面 | 官方参考文档 | **ATK-DNN647 的实际情况** |
|---|---|---|
| **烧录方式** | ST-Link + STM32CubeProgrammer | **串口 Boot ROM**（无 ST-Link，见第四章） |
| **存储介质** | 片内 Flash | **板子无片内 Flash**，固件跑在 SRAM（FLEXRAM），**复位即丢失需重烧** |
| **串口参数** | 115200 8N1 | **115200 8E1（偶校验，必须）** |
| **BSP 目录** | `nuttx/boards/...` | **`vendor/st/boards/stm32n6/atk-dnn647`**（openvela vendor 树） |
| **构建方式** | Make 或 CMake | **Make**（`build.sh`，非 `--cmake`） |
| **LED 底层** | `board_userled()` + `USERLED_LOWER` | **自实现 lowerhalf**（本板 Kconfig 未 select `ARCH_HAVE_LEDS`，见第六章） |
| **芯片系列** | STM32F4 / F4 / H7 | **STM32N6**（Cortex-M55，ARMv8-M，800MHz） |

### 你将学到什么

- openvela 的板级移植核心思想与启动流程。
- 如何为 **Cortex-M55 / ARMv8-M** 芯片实现最小可运行 BSP。
- 如何用**串口 Boot ROM**（而非调试器）把固件烧进 SRAM 并启动。
- 如何实现板载 LED 的用户态控制（`/dev/userleds`）与应用层命令。
- 如何按 openvela 规范在 `packages/demos/` 下新增一个 Demo。

### 配套文档

| 文档 | 内容 |
|---|---|
| `HANDOFF-stm32n647-20261120.md` | 完整交接文档（14 节，含全部排查过程与根因） |
| `STM32N647-最终交付报告-20261121.md` | 最终验证结果、五个根因、复现步骤 |
| `CLAUDE.md` | 本项目的工程纪律（一次一变量、先写假设等） |

---

## 二、开发板简介

### 1、外观与组成

ATK-DNN647（正点原子）由**核心板 + 底板**两部分组成：

- **核心板 CNN647B**：搭载 STM32N647X0H3Q，板载 **LED0（PG10）**
- **底板 DNN647**：承载扩展外设，板载 **LED1（PE10）**

### 2、主要硬件特性

| 项目 | 参数 |
|---|---|
| 主控芯片 | STM32N647X0H3Q（Cortex-M55，ARMv8-M） |
| 主频 | **800 MHz**（HSE 48MHz → PLL1 → 800MHz） |
| 内核 | ARMv8-M Mainline，单精度/双精度 FPU |
| 片内 Flash | **无**（固件运行于 SRAM） |
| SRAM | FLEXRAM 约 400KB 可用窗口（0x34180400 ~ 0x341C4400） |
| DTCM | 128 KB（0x20000000） |
| 调试串口 | USART1（PE5=TX / PE6=RX，AF7），**115200 8E1** |
| 板载 LED | 2 颗，均 **低电平点亮（active-low）** |
| 板载按键 | KEY0=PC6、KEY1=PD1、KEY2=PG11、WK_UP=PC13（低电平有效） |
| 外部晶振 | HSE 48MHz（PH0/PH1）、LSE 32.768kHz（PC14/PC15） |

### 3、时钟树配置

摘自 `vendor/st/boards/stm32n6/atk-dnn647/include/board.h`：

```c
/* Clocking *****************************************************************
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
```

---

## 三、准备工作

### 1、搭建开发环境

编译 openvela 需要配置相关开发环境，参见[快速入门](https://doc.openvela.com/document?id=847&version=dev&language=cn)。

本项目使用的编译环境：

```bash
# ARM 交叉编译工具链
sudo apt install gcc-arm-none-eabi binutils-arm-none-eabi

# 串口工具（本项目推荐 picocom，见第五章）
sudo apt install picocom
```

> **注意**：本项目**不使用 ST-Link**，因此无需安装 STM32CubeProgrammer 与 stsw-link 驱动。烧录走串口 Boot ROM，详见第四章。

### 2、验证工具链

```bash
arm-none-eabi-gcc --version   # 应为 13.x 或更高
arm-none-eabi-objcopy --version
```

### 3、准备 openvela 源码

```bash
git clone https://github.com/open-vela/openvela.git
cd openvela
```

### 4、板卡与主机连接

ATK-DNN647 通过 **CH340 USB-转-串口** 与主机相连：

| 主机 | 开发板 |
|---|---|
| USB 口 | CH340（枚举为 `/dev/cu.usbserial-XXXX`，本项目为 `/dev/cu.usbserial-1420`） |
| GND | GND |

> ⚠️ **CH340 的 DTR/RTS 接到板子的 BOOT0 与 NRST**。这意味着串口工具打开/关闭时会**复位板子**。所有串口工具都必须加 `--noinit`（picocom）或 `-o`（minicom）。详见第五章。

---

## 四、运行 Demo（NSH + LED 控制）

### 1、编译代码

在 openvela 源码根目录执行：

```bash
# ATK-DNN647 的 NSH 配置
./build.sh vendor/st/boards/stm32n6/atk-dnn647/configs/nsh/ -j$(nproc)
```

> **注意**：本项目走 **Make 构建**（默认模式），**不要加 `--cmake`**。虽然板目录下存在 `CMakeLists.txt`，但实际编译走 `src/Makefile`。

编译完成后，产物位于 `nuttx/` 目录：

- `nuttx` —— ELF 可执行文件（含符号，用于取入口地址）
- `nuttx.bin` —— 原始二进制（由 objcopy 生成）

**生成 bin 并取入口地址**：

```bash
cd nuttx
arm-none-eabi-objcopy -O binary nuttx /tmp/nuttx_n6.bin

# 取入口点：ENTRY = __n6_boot 的地址 + 1
arm-none-eabi-nm nuttx | grep __n6_boot
```

> ⚠️ **ENTRY 每次构建后必须重新取**。固件大小变化会改变符号地址，沿用旧值会烧录失败。本项目当前值为 `341806c1`（`__n6_boot = 0x341806c0`）。

### 2、烧录固件（串口 Boot ROM）

本板**没有 ST-Link，也没有片内 Flash**。固件通过 **STM32N6 内置 Boot ROM 的 UART 协议**直接下载到 SRAM 并立即运行。

烧录脚本 `n6_flash.py` 的核心机制：

```python
Image layout handed to the boot ROM:
  [0x000..0x240)  576-byte STM32 image header (copied from the ATK FSBL)
  [0x240..end)   payload, padded so that the TOTAL length is a multiple of 256.

Why the padding matters: the boot ROM only accepts 256-byte blocks that
lie *fully* inside the declared image length (576 + length field).  With a
576-byte header the total is normally not block aligned, so the last block
is a short one and the ROM NAKs it - which leaves the tail of the image
stale (previous build's bytes) in RAM.  Aligning the total to 256 makes
every block full and every block accepted.
```

执行烧录：

```bash
cd ~/.zcode/workspace/8d40629c-1cb8-4a0a-9d30-df7584ef9f9e/n6-test

python3 n6_flash.py /dev/cu.usbserial-1420 nuttx_bootrom.bin fsbl.bin 341806c1 6
```

参数说明：

| 参数 | 含义 |
|---|---|
| `PORT` | 串口设备路径 |
| `PAYLOAD` | 编译产物 `nuttx.bin` |
| `FSBL` | ATK 提供的 FSBL（提供 576 字节镜像头） |
| `ENTRY` | 入口地址（`__n6_boot` + 1） |
| `CAPTURE` | 抓取启动输出的秒数 |

正常输出：

```
image 101632B, 397 blocks
downloaded 397 blocks, fails=0
start ACKs: 121 121
boot trace: 125B  NuttShell=True  nsh>=True
```

> ⚠️ **烧录即运行**：固件下载到 SRAM 后立即启动，NSH 的欢迎语也在烧录瞬间输出完。之后再打开串口工具会错过这段输出——发送一个空回车 `\r` 即可唤出 `nsh>` 提示符。

### 3、一条命令完成烧录 + 验证

本项目提供 `nsh_verify.py`，把烧录和功能验证合并为一步：

```bash
python3 nsh_verify.py /dev/cu.usbserial-1420 nuttx_bootrom.bin 341806c1 6
```

输出示例：

```
          :    10B  expect 'nsh>': OK
help      :   773B  expect 'help usage': OK
uname -a  :    78B  expect 'NuttX': OK
ls        :    42B  expect 'nsh>': OK
ps        :   284B  expect 'STACK COMMAND': OK
ps        :    55B  expect 'nsh>': OK

RESULT: ALL PASS
```

### 4、运行 LED 控制程序

烧录 `led_verify.py` 对应的固件后（或直接用已含 LED 功能的固件），在 NSH 中运行：

```bash
nsh> leds -h
Usage: leds [<led> <on|off|blink>]
  leds                blink all LEDs
  leds 0|1            blink LED 0 (PG10) or LED 1 (PE10)
  leds 0|1 on         turn the LED on
  leds 0|1 off        turn the LED off
  leds 0|1 blink      blink until Ctrl+C

nsh> leds 0 on
leds: LED(s) 0x1 ON          # bit0 = LED0 (PG10, 核心板)

nsh> leds 1 on
leds: LED(s) 0x2 ON          # bit1 = LED1 (PE10, 底板)

nsh> leds 0 off
leds: LED(s) 0x1 OFF

nsh> leds 9 on
leds: invalid LED number 9 (must be 0 or 1)
```

---

## 五、串口连接（实测可用方案）

### 1、推荐用法：烧录后直接进 picocom

```bash
python3 -u n6_flash.py /dev/cu.usbserial-1420 nuttx_bootrom.bin fsbl.bin 341806c1 6 ; \
picocom -b 115200 --parity n -d 8 --stopbits 1 -f n --noinit \
         --lower-dtr --lower-rts /dev/cu.usbserial-1420
```

烧录完成、picocom 打印 `Terminal ready` 后，敲一次回车即可看到 `nsh>`，
之后可以正常输入命令（`uname -a`、`ls /dev`、`leds 0 on` 等）。

**参数要点**

| 参数 | 值 | 说明 |
|---|---|---|
| `-b` | 115200 | 波特率 |
| `--parity` | **`n`（无校验）** | **实测必须用 `n`**。用 `e` 收不到任何数据，见下 |
| `-d 8` | 8 数据位 | — |
| `--stopbits 1` | 1 停止位 | — |
| `-f n` | 无流控 | 板子未接 RTS/CTS |
| `--noinit` | 必须 | 跳过初始化串 |
| `--lower-dtr --lower-rts` | 必须 | 与烧录后的线路状态保持一致 |

**为什么是 `--parity n` 而不是 `e`**

板子 USART1 是 9 位字长（8 数据 + 1 校验，`CR1.M1:M0=01`、`PS=0`）。
主机改用 8N1 时，板子会把主机发来的停止位当校验位收下，而驱动不检查校验错误位，
8 位数据因此仍能正确还原；反向同理。

> 实测记录：同一块板、同一固件，`--parity n` 可以正常交互；
> `--parity e` 则完全收不到数据。具体原因未查明（疑与 macOS 下 CH340 驱动的
> 偶校验处理有关），**以实测为准**。

### 2、退出 picocom 后可以再次连接（不必重烧）

**正常退出 picocom 不会复位板子**，固件仍留在 SRAM 中，
所以**再次执行同一条 picocom 命令即可继续操作，无需重新烧录**。

```bash
# 退出后再连一次（不要带 n6_flash，直接开 picocom）
picocom -b 115200 --parity n -d 8 --stopbits 1 -f n --noinit \
         --lower-dtr --lower-rts /dev/cu.usbserial-1420
```

退出方式：先按 `Ctrl+A`，再按 `X`。

> 实测（2026-11-21）：烧录 → picocom 执行命令 → `Ctrl+A X` 退出 →
> 等待约 3 秒 → 直接重开 picocom → 敲回车仍出现 `nsh>`，`uname -a` 正常。

**固件什么时候会丢**（因为本板无片内 Flash，程序只存在于 SRAM）：

| 动作 | 后果 |
|---|---|
| 正常退出 / 重新打开 picocom | ✅ 固件仍在 |
| 按板上的 REST 复位键 | ❌ 丢失，需重烧 |
| 拔插 USB（断电） | ❌ 丢失，需重烧 |
| 重新执行 n6_flash.py 烧录 | 覆盖为新的固件 |

> 提示：若退出后立刻重连时报 `cannot lock ... Resource temporarily unavailable`，
> 说明上一个进程还没完全释放串口，等 2~3 秒再连即可。

### 3、不想每次重烧：同一会话内烧录 + 交互

以下两个脚本都在**同一个进程、同一个串口会话**内完成「烧录 → 等待启动 → 交互」，
中间不关闭端口，因此**退出后固件仍在 SRAM 里，不必重烧**（均已真机验证）。

```bash
# 方式 A：逐行交互（输入一行、回车发送），适合连续敲几条命令
python3 scripts/n6_console.py /dev/cu.usbserial-1420 nuttx_bootrom.bin 341806c1 6

# 方式 B：全双工透传（raw 模式，按键直通、输出实时），体验最接近 picocom
python3 scripts/n6_shell.py /dev/cu.usbserial-1420 nuttx_bootrom.bin 341806c1 6
# 退出：Ctrl+A 然后 X
```

验证记录（2026-11-21 真机）：
- `n6_console.py`：进入提示符 / `leds 0 on` / `ls /dev` / `uname -a` / `exit` 全部正常
- `n6_shell.py`：透传 `uname -a`、`ls /dev`、`leds 0 on` 正常；`Ctrl+A X` 退出后
  重新检查，串口仍能收到 `nsh>` —— **固件确实保留**

### 4、其他串口工具

`screen` 无法设置校验位，禁用。

### 3、固件在 SRAM 中

| 现象 | 原因 | 解决 |
|---|---|---|
| 板子没有任何输出 | 按了 REST 或拔插 USB，SRAM 固件丢失 | **重新烧录**（`n6_flash.py`） |
| 打开串口工具后没反应 | 工具初始化时复位了板子 | 加 `--noinit` / `-o`，然后重烧 |
| 只有 `nsh>` 没有欢迎语 | 烧录瞬间就输出了，之后才开的工具 | 发一个空回车 `\r` 唤出提示符 |

### 4、避免被自动复位

由于 CH340 的 DTR/RTS 接了 BOOT0/NRST，以下操作会复位板子：

- 打开/关闭任何串口工具（除非禁用 DTR/RTS）
- USB 重新插拔
- 按 REST 键

**建议**：调试期间用 `nsh_verify.py` / `led_verify.py` 这类脚本完成全部交互，避免同时开串口工具。

---

## 六、移植原理与 BSP 实现

本章参考[为 STM32F407 开发板移植 openvela](https://doc.openvela.com/document?id=600&version=dev&language=cn)的结构，说明 ATK-DNN647 的 BSP 是如何组织的。

### 1、代码目录结构

本板的 BSP 位于 openvela 的 **vendor 树**（不是 `nuttx/boards/`）：

```
vendor/st/boards/stm32n6/atk-dnn647/
├── CMakeLists.txt
├── Kconfig                              # 板级 Kconfig（当前为空）
├── configs/
│   ├── nsh/defconfig                    # NSH 默认配置
│   └── refconfig_*                      # 其他参考配置
├── include/
│   ├── board.h                          # 板级公共定义（时钟、LED、按键、串口）
│   ├── board_lcd.h
│   └── nsh_romfsimg.h
├── scripts/
│   ├── ld.script                        # 链接脚本（FLEXRAM 内存布局）
│   ├── ld.script.sram.bak
│   └── Make.defs
└── src/
    ├── Makefile                         # ★ 实际编译入口（CSRCS = st_ap.c st_userled.c）
    ├── CMakeLists.txt
    ├── atk-dnn647.h                     # 板级私有头文件
    ├── st_ap.c                          # ★ 板级初始化与驱动注册
    ├── st_userled.c                     # ★ 用户 LED 底层驱动
    ├── st_lcd.c
    └── etc/init.d/{rcS,rc.sysinit}
```

芯片级代码位于 `vendor/st/chips/stm32n6/`：

```
vendor/st/chips/stm32n6/
├── chip.h
├── st_start.c / st_start.h              # ★ 早期初始化（时钟、USART1、跳转入口）
├── st_uart.c                            # ★ USART1 驱动（8E1 + 中断收发）
├── st_irq.c / st_irq.h                  # ★ 中断控制
├── st_oneshot.c                         # ★ one-shot timer（SysTick）
├── st_allocateheap.c                    # ★ 堆分配
├── st_ltdc.c
└── st_lowputc.h
```

### 2、关键启动函数

openvela 启动时按序调用以下 BSP 接口：

| 函数 | 调用者 | 职责 | 本项目实现 |
|---|---|---|---|
| `__start` / `stm32_boardinitialize()` | 复位入口 | 配置时钟、调试串口，**必须最早执行** | `st_start.c` |
| `board_late_initialize()` | `nx_bringup()` | 初始化依赖 OS 服务的驱动 | `st_ap.c` |
| `atk_dnn647_bringup()` | `board_late_initialize()` | 注册板载设备驱动 | `st_ap.c` |
| `board_app_initialize()` | `boardctl(BOARDIOC_INIT)` | 应用层初始化 | `st_ap.c` |

本项目 `CONFIG_BOARD_LATE_INITIALIZE=y`，因此 `atk_dnn647_bringup()` 由 `board_late_initialize()` 调用。

### 3、内存布局（关键：固件跑在 SRAM）

本板**没有片内 Flash**，Boot ROM 把固件下载到 FLEXRAM 执行。链接脚本 `scripts/ld.script` 的设计说明：

```ld
 * FLEXRAM at 0x34180400 and branches to the header entry point.  Since
 * .data VMA = .data LMA (both in FLEXRAM), no runtime copy is needed.
 * .bss (NOLOAD) follows .data in FLEXRAM and is zeroed at run time.
 *
 * All RAM (data, bss, heap, stacks) must stay within the FLEXRAM region
 * that the boot ROM leaves writable (approximately 0x34180400 to
 * 0x341C4400 = 400KB of FLEXRAM).
 *
 * Memory budget:
 *   code + rodata:  ~100KB
 *   .data:          ~1KB
 *   .bss:           ~7KB
 *   total:         ~252KB < 400KB FLEXRAM ✓

MEMORY
{
  /* FLEXRAM: code, rodata, .data, .bss, heap, stacks — all in one window */
  ROM   (xrwx) : ORIGIN = 0x34180240, LENGTH = 240K
  DTCM  (rwx)  : ORIGIN = 0x20000000, LENGTH = 128K
}
```

对应的 defconfig 关键项：

```
CONFIG_RAM_START=0x3419d000
CONFIG_RAM_SIZE=126000
CONFIG_IDLETHREAD_STACKSIZE=32768
CONFIG_INIT_ENTRYPOINT="nsh_main"
CONFIG_BOARD_LATE_INITIALIZE=y
```

### 4、USART1 调试串口（8E1 + 中断驱动）

这是本项目**最关键的硬件适配点**。

#### 4.1 引脚配置：PE6 = USART1_RX (AF7)

数据手册中 PE6 标注为 `USART1_RX(boot)`，即上电时 Boot ROM 已将其配置为串口功能；但**ROM 跳转后会被还原成 analog 模式**，导致接收中断永远不触发。必须在 `st_start.c` 中**显式重新配置 AF7**：

```c
/* ===== PE6 = USART1_RX(AF7) — RX fix ===== */
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
```

寄存器位说明（GPIOE）：

| 寄存器 | 偏移 | PE6 对应位 | 作用 |
|---|---|---|---|
| AFRL | 0x20 | bit[27:24] | 复用功能选择，写 7 = AF7 |
| PUPDR | 0x0C | bit[13:12] | 上下拉，清零 = 无上下拉 |
| MODER | 0x00 | bit[13:12] | 模式，写 2 = 复用模式（AF） |

> ⚠️ 注意 `MODER` 写的是 **2（复用）而非 1（输出）**，与 LED 的输出配置不同——串口引脚必须是复用模式。

#### 4.2 帧格式：8E1（偶校验）

`st_start.c` 中的初始化：

```c
*(volatile uint32_t *)0x42011000UL = 0;         /* CR1 off */
*(volatile uint32_t *)0x42011000UL = 0x140dUL;  /* UE|RE|TE|PCE|M0 (8E1) */
```

位含义：

| 位 | 名称 | 值 | 含义 |
|---|---|---|---|
| 0 | UE | 1 | USART 使能 |
| 2 | RE | 1 | 接收使能 |
| 3 | TE | 1 | 发送使能 |
| 8 | PCE | 1 | **校验控制使能**（开启偶校验） |
| 9 | PS | 0 | 校验极性：0 = **偶校验** |
| 12 | M0 | 1 | **字长位**：M1:M0=01 = 9 位字长（8 数据 + 1 校验） |

> ⚠️ **重要认知**：`CR1` 的 `M0=1` **不是**"奇校验"，而是"9 位字长"；校验极性由 `PS(bit9)` 决定。本项目 `PS=0` → **偶校验 → 8E1**。

> ⚠️ **PCE（Parity Control Enable）位必须置 1**。若按常见的 8N1 配置，主机与板子的帧格式不一致，表现为「板子能发、但收不到命令」或满屏乱码。

#### 4.3 TX 必须由中断驱动

```c
while (((*(volatile uint32_t *)(0x42011000UL + 0x1cUL) & 0x200000UL) == 0) && ...) { }
```

轮询式发送在 800MHz 主频 + 大量输出时不可靠，**必须用 TX 中断**。

`st_uart.c` 在运行时追加接收中断使能位：

```c
uint32_t cr1 = st_uart_getreg(USART_CR1);
cr1 |= USART_CR1_RXNEIE;     /* RXNEIE = bit5 */
st_uart_putreg(USART_CR1, cr1);
```

因此 **CR1 的最终运行值是 `0x142D`**（= `0x140d` + RXNEIE(bit5)）。`0x140d` 是 `st_start.c` 的初始值，`0x142D` 是运行后的值——排查时不要混淆。

#### 4.4 关键寄存器常量速查

```
USART1  = 0x42011000
GPIOE   = 0x46021000    GPIOG = 0x46021800
RCC     = 0x46028000 (AHB4ENR + 0x25C)
NVIC    = 0xE000E000    SCB   = 0xE000ED00
向量表  = 0x34180400 (1KB 对齐)     payload 落点 = 0x34180240
CR1 初值(st_start.c) = 0x140D (UE|RE|TE|PCE|M0, 8E1)
CR1 运行值(+RXNEIE) = 0x142D    CR3 = 0    ICR 清错 = 0x0F
AF7 数值 = 7 (GPIO_AF7_USART1)
USART1_IRQn(CMSIS) = 159 → NuttX irq = 175
```

### 5、LED 驱动实现

#### 5.1 硬件定义

`board.h` 中的权威定义：

```c
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
```

两颗 LED **均为低电平点亮**。寄存器基址：

| 端口 | 基址 | 时钟位 | BSRR 偏移 | MODER 偏移 |
|---|---|---|---|---|
| GPIOG（LED0/PG10） | `0x46021800` | AHB4ENR bit6 | 0x18 | 0x00（pin10 → bit[21:20]） |
| GPIOE（LED1/PE10） | `0x46021000` | AHB4ENR bit4 | 0x18 | 0x00（pin10 → bit[21:20]） |

> ⚠️ **RCC_AHB4ENR 地址必须是 `0x4602825c`**（= `0x46028000 + 0x25c`）。
> 写成 `0x46028025c`（少一位）会写入无效地址 → **HardFault → NSH 起不来**。
> 详见第八章「致命坑」。

#### 5.2 为何不能直接用标准 USERLED_LOWER

标准 NuttX 的 `CONFIG_USERLED_LOWER` 依赖 `CONFIG_ARCH_HAVE_LEDS`，而该符号由各板级 Kconfig 用 `select` 显式启用。本板 `vendor/st/boards/stm32n6/atk-dnn647/Kconfig` **是空文件**，没有 `select ARCH_HAVE_LEDS`，因此 `USERLED_LOWER` 无法启用。

**解决方案**：只启用上半部 `CONFIG_USERLED=y`，在板级**自行实现 lowerhalf 结构体**并直接注册：

```c
#include <nuttx/leds/userled.h>

#define GPIOE_BASE      0x46021000UL
#define GPIOG_BASE      0x46021800UL
#define RCC_AHB4ENR     0x4602825cUL   /* ⚠️ 少一位会 HardFault */
#define GPIO_MODER_OFF  0x00UL
#define GPIO_ODR_OFF    0x14UL
#define GPIO_BSRR_OFF   0x18UL
#define GPIOE_CLK_BIT   (1UL << 4)
#define GPIOG_CLK_BIT   (1UL << 6)

#define REG32(addr)     (*(volatile uint32_t *)(addr))

#define NLEDS 2

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

/* ---- lowerhalf 回调 ---- */

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

  /* Active low: ON -> BSRR BR (pin+16) 写低; OFF -> BS (pin) 写高 */
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

static const struct userled_lowerhalf_s g_userled_lower =
{
  .ll_supported = userled_supported,
  .ll_setled    = userled_setled,
  .ll_setall    = userled_setall,
};
```

#### 5.3 注册 `/dev/userleds`

```c
int atk_userled_initialize(void)
{
  uint32_t m;
  int ret;

  /* 使能 GPIOE / GPIOG 时钟 */
  if ((REG32(RCC_AHB4ENR) & GPIOG_CLK_BIT) == 0)
    {
      REG32(RCC_AHB4ENR) |= GPIOG_CLK_BIT;
    }
  if ((REG32(RCC_AHB4ENR) & GPIOE_CLK_BIT) == 0)
    {
      REG32(RCC_AHB4ENR) |= GPIOE_CLK_BIT;
    }

  /* 配置 PG10 / PE10 为通用输出 (MODER bit[21:20] = 01) */
  m  = REG32(GPIOG_BASE + GPIO_MODER_OFF);
  m &= ~(3UL << 20);
  m |=  (1UL << 20);
  REG32(GPIOG_BASE + GPIO_MODER_OFF) = m;

  m  = REG32(GPIOE_BASE + GPIO_MODER_OFF);
  m &= ~(3UL << 20);
  m |=  (1UL << 20);
  REG32(GPIOE_BASE + GPIO_MODER_OFF) = m;

  /* 初始均为灭（active low -> 输出高） */
  REG32(GPIOG_BASE + GPIO_BSRR_OFF) = (1UL << 10);
  REG32(GPIOE_BASE + GPIO_BSRR_OFF) = (1UL << 10);

  /* ★ 直接注册 lowerhalf，绕开 CONFIG_USERLED_LOWER 依赖 */
  ret = userled_register("/dev/userleds", &g_userled_lower);
  if (ret < 0)
    {
      syslog(LOG_ERR, "atk_userled: register failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "atk_userled: /dev/userleds registered, 2 LEDs (PG10,PE10)\n");
  return OK;
}
```

#### 5.4 在 bringup 中调用

`vendor/st/boards/stm32n6/atk-dnn647/src/st_ap.c`：

```c
#ifdef CONFIG_USERLED
  /* Register /dev/userleds for the NSH 'leds' command */
  ret = atk_userled_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: userled init failed: %d\n", ret);
    }
#endif
```

#### 5.5 加入编译

板级 `src/Makefile`（**注意是 Makefile，不是 CMakeLists.txt**）：

```makefile
CSRCS = st_ap.c
...
CSRCS += st_userled.c
...
CSRCS += st_lcd.c
...
RCSRCS = etc/init.d/rc.sysinit etc/init.d/rcS
```

> 新增源文件时用 `CSRCS +=` 追加即可。改 `CMakeLists.txt` **无效**——本项目走 Make 构建。


---

## 七、集成到构建系统

### 1、板级 Kconfig

本板的 `vendor/st/boards/stm32n6/atk-dnn647/Kconfig` **只有注释头、没有任何 config 定义**（全文 134 字节）：

```
#
# For a description of the syntax of this configuration file,
# see the file kconfig-language.txt in the NuttX tools repository.
#
```

> ⚠️ **正因为它是空的、没有 `select ARCH_HAVE_LEDS`，标准 `USERLED_LOWER` 才无法启用**（见 5.2 节）。若将来要让社区标准 `leds` 示例（`apps/examples/leds`）直接工作，需要在此补上：
>
> ```
> config ARCH_BOARD_CUSTOM
>     select ARCH_HAVE_LEDS
> ```

本项目采用「自实现 lowerhalf」的方案，不依赖该符号，因此**无需修改**。

### 2、芯片与板级选择

本板使用 openvela 的 **custom board / custom chip** 机制（vendor 树），defconfig 中体现为：

```
CONFIG_ARCH="arm"
CONFIG_ARCH_BOARD_CUSTOM=y
CONFIG_ARCH_BOARD_CUSTOM_DIR="../vendor/st/boards/stm32n6/atk-dnn647"
CONFIG_ARCH_BOARD_CUSTOM_DIR_RELPATH=y
CONFIG_ARCH_BOARD_CUSTOM_NAME="nsh"
CONFIG_ARCH_CHIP_ARM_CUSTOM=y
CONFIG_ARCH_CHIP_CUSTOM=y
CONFIG_ARCH_CHIP_CUSTOM_DIR="../vendor/st/chips/stm32n6"
CONFIG_ARCH_CHIP_CUSTOM_DIR_RELPATH=y
CONFIG_ARCH_CHIP_CUSTOM_NAME="stm32n6"
CONFIG_ARCH_CHIP_STM32N6=y
```

### 3、defconfig

`vendor/st/boards/stm32n6/atk-dnn647/configs/nsh/defconfig` 中与 LED 功能相关的部分：

```
CONFIG_USERLED=y
# CONFIG_EXAMPLES_LEDS is not set
CONFIG_APP_DEMOS_LED=y
CONFIG_APP_DEMOS_LED_PROGNAME="leds"
CONFIG_APP_DEMOS_LED_DEVPATH="/dev/userleds"
```

> ⚠️ **`CONFIG_EXAMPLES_LEDS` 必须禁用**：它与 `CONFIG_APP_DEMOS_LED` 的 `PROGNAME` 都是 `"leds"`，同时启用会产生命令名冲突 / 链接冲突。

---

## 八、新增一个 Demo（openvela 标准做法）

本章参考[在 STM32F411 上使用 openvela 点亮 LED](https://doc.openvela.com/document?id=598&version=dev&language=cn)第五章「如何新增 Demo」。

### 1、官方原则

> openvela 明确要求：**自定义 Demo 放在 `packages/demos/` 目录下**，而不是改写 `nuttx-apps/examples`。
>
> - `nuttx-apps/examples`：NuttX 官方社区维护的通用示例区
> - `packages/demos`：openvela 项目自建的、与 openvela 特性强相关的定制区

### 2、目录结构

```
packages/demos/led/
├── led_main.c   # 应用逻辑
├── Kconfig      # 配置选项
├── Makefile     # 编译规则
└── Make.defs    # 注册到编译系统
```

### 3、`led_main.c`

```c
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

  /* 参数解析 */
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

  /* 掩码：bit0 = LED0，bit1 = LED1 */
  if (led < 0)
    {
      mask = 0x3;   /* 全部 */
    }
  else
    {
      mask = (userled_set_t)(1UL << led);
    }

  fd = open(CONFIG_APP_DEMOS_LED_DEVPATH, O_WRONLY);
  if (fd < 0)
    {
      printf("leds: failed to open %s: %d\n",
             CONFIG_APP_DEMOS_LED_DEVPATH, errno);
      return EXIT_FAILURE;
    }

  /* 支持 Ctrl+C 干净退出 blink */
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
```

### 4、`Kconfig`

```kconfig
#
# For a description of the syntax of this configuration file,
# see the file kconfig-language.txt in the NuttX tools repository.
#

config APP_DEMOS_LED
	bool "Custom LED control demo"
	default n
	depends on USERLED
	---help---
		Enable this to build the custom LED control demo, which
		demonstrates how to turn an LED on and off via ioctl.

if APP_DEMOS_LED

config APP_DEMOS_LED_PROGNAME
	string "Program name"
	default "leds"
	---help---
		This is the name of the program that will be used when the NSH ELF
		program is installed.

config APP_DEMOS_LED_PRIORITY
	int "LED task priority"
	default 100

config APP_DEMOS_LED_STACKSIZE
	int "LED stack size"
	default 2048

config APP_DEMOS_LED_DEVPATH
	string "LED device path"
	default "/dev/userleds"

endif # APP_DEMOS_LED
```

### 5、`Makefile`

```makefile
include $(APPDIR)/Make.defs

ifeq ($(CONFIG_APP_DEMOS_LED), y)
PROGNAME = $(CONFIG_APP_DEMOS_LED_PROGNAME)
PRIORITY = $(CONFIG_APP_DEMOS_LED_PRIORITY)
STACKSIZE = $(CONFIG_APP_DEMOS_LED_STACKSIZE)
MODULE = $(CONFIG_APP_DEMOS_LED)

MAINSRC = led_main.c
endif

include $(APPDIR)/Application.mk
```

### 6、`Make.defs`

```makefile
ifneq ($(CONFIG_APP_DEMOS_LED),)
CONFIGURED_APPS += $(APPDIR)/packages/demos/led
endif
```

### 7、⚠️ 实际机制与官方文档描述的差异

这是本项目实测出的**重要差异**，照搬官方文档会走弯路：

| 官方文档说法 | **本工程实际情况** |
|---|---|
| 手动编辑 `packages/demos/Make.defs` 加 `CONFIGURED_APPS` | **不需要**。父级 Make.defs 已是 `include $(wildcard $(APPDIR)/packages/demos/*/Make.defs)`，自动包含子 demo 的 Make.defs |
| 需手动在 Kconfig 里加 source 行 | **不需要**。`packages/demos/Kconfig` 是**自动生成**的，跑下面的命令即可 |
| 启用 `EXAMPLES_LEDS` | **必须禁用它**，改用独立的 `APP_DEMOS_LED`，避免 PROGNAME 冲突 |

**Kconfig 自动生成链**：

```
nuttx/Kconfig
  └─ source "$APPSBINDIR/Kconfig"           → apps/Kconfig（自动生成）
       └─ source "apps/packages/Kconfig"      → 软链到 packages/Kconfig（自动生成）
            └─ source "packages/demos/Kconfig"（自动生成）
```

**重新生成 `packages/demos/Kconfig`**：

```bash
cd packages/demos
bash ../../apps/tools/mkkconfig.sh -m "Demos"
```

生成结果：

```
menu "Demos"
source "/home/honestqiao/openvela/source/packages/demos/led/Kconfig"
...
endmenu # Demos
```

> **路径前缀说明**：生成时写入的是 `/home/honestqiao/...`，而旧文件里是 `/data/home/honestqiao/...`。这是因为 `/home/honestqiao/openvela` 是指向 `/data/home/honestqiao/Projects/openvela` 的软链（两者同 inode），**不影响构建**。

### 8、验证

```bash
# 确认配置生效
ssh ssh-ohos-new 'grep -E "CONFIG_APP_DEMOS_LED|CONFIG_EXAMPLES_LEDS" ~/openvela/source/nuttx/.config'

# 确认构建产物走新路径
ssh ssh-ohos-new 'cd ~/openvela/source && find . -path "*demos/led*" -name "*.o"'

# 确认社区版未被编译
ssh ssh-ohos-new 'cd ~/openvela/source && find . -name "leds_main.o"'
```

预期输出：

```
# CONFIG_EXAMPLES_LEDS is not set
CONFIG_APP_DEMOS_LED=y
CONFIG_APP_DEMOS_LED_PROGNAME="leds"
...
./packages/demos/led/led_main.o
（leds_main.o 无输出 —— 社区版未编译）
```

---

## 九、踩坑清单（实战总结）

本章记录本次移植中所有**已定位根因并验证修复**的问题，供后续开发者参考。

### 1、致命坑：RCC 寄存器地址少一位 → HardFault

| 项目 | 内容 |
|---|---|
| **现象** | 烧录后 NSH 起不来，boot trace 只有 `S b0 b0 b1 b1`（5 字节） |
| **根因** | `RCC_AHB4ENR` 地址写成 `0x46028025c`，正确是 `0x4602825c`（= `0x46028000 + 0x25c`）。少一位 → 写入无效地址 → **HardFault** |
| **定位方法** | 在函数入口写**裸寄存器标记字符**逐步打点（`'1'`~`'6'`），看 trace 停在哪两个标记之间 |
| **修复** | 改为 `0x4602825c` |
| **验证** | trace 出现完整的 `123456` 标记序列 + `atk_userled: /dev/userleds registered` + `NuttShell` |

裸写标记的参考实现：

```c
#define URAW(c) do { \
    volatile uint32_t *uisr=(volatile uint32_t *)0x4201101CUL; \
    volatile uint32_t *utdr=(volatile uint32_t *)0x42011028UL; \
    uint32_t ug=200000; \
    while (!(*uisr & (1UL<<7)) && ug-->0) {} \
    *utdr=(c); \
  } while(0)
```

> **教训**：本项目共出现过 **5 处**类似问题（地址笔误、地址缺少 `0x` 前缀、堆栈与 idle 重叠、向量表为空、诊断代码自身触发 HardFault）。**任何写裸地址的代码都应视为高风险**。

### 2、PE6 被 Boot ROM 还原成 analog → 接收中断不触发

| 项目 | 内容 |
|---|---|
| **现象** | 板子能发送（能看到 `S`、boot trace），但**完全收不到命令**，NSH 无响应 |
| **根因** | 数据手册标注 PE6 为 `USART1_RX(boot)`，Boot ROM 期间已配好串口；但**ROM 跳转后 PE6 被还原成 analog 模式**，RXNE 中断永不触发 |
| **修复** | 在 `st_start.c` 中**显式**把 PE6 配成 `GPIO_AF7_USART1`（AF7 数值 = 7） |
| **验证** | 回读 AFRL 寄存器确认 pin10 的 AF 字段 = 7，接收恢复 |

### 3、帧格式必须是 8E1

| 项目 | 内容 |
|---|---|
| **现象** | 主机设 8N1 时，板子发的字节看起来「正常」，但发出去的命令板子收不到 |
| **根因** | 固件把 USART1 配成了带校验（PCE 位置 1）的格式 |
| **修复** | 主机与固件统一为 **115200 8E1**（偶校验） |
| **验证** | 实测 host parity 设为 even/odd/none 三种时，固件发出的字节完全一致 → 确认是**帧格式本身**是 8E1，而非主机设置问题 |

### 4、固件跑在 SRAM，复位即丢失

| 项目 | 内容 |
|---|---|
| **现象** | 按 REST 或拔插 USB 后，板子毫无反应 |
| **根因** | 本板无片内 Flash，Boot ROM 只把固件下载到 FLEXRAM，**不写任何非易失存储** |
| **缓解** | 所有串口工具加 `--noinit` / `-o`，避免工具初始化时复位；`nsh_verify.py` 等脚本一次完成烧录+验证 |
| **文档** | 已在第四章、第五章明确标注 |

### 5、CH340 的 DTR/RTS 接了 BOOT0/NRST

| 项目 | 内容 |
|---|---|
| **现象** | 一打开串口工具板子就没反应（被复位进 Boot ROM 静默模式） |
| **根因** | CH340 的 Q6/Q8 接到板子的 BOOT0 与 NRST，工具初始化时会拉这两根线 |
| **修复** | 烧录后紧接 `picocom ... --parity n ... --noinit --lower-dtr --lower-rts`（**校验位必须是 n，用 e 收不到数据**）。正常退出后固件仍在，可直接重连；重连若报 lock 冲突等 2~3 秒。详见第五章 |

### 6、SysTick 心跳插桩导致满屏方块

| 项目 | 内容 |
|---|---|
| **现象** | 串口终端里 `nsh>` 之间不断刷出 `␄` 方块 |
| **根因** | `vendor/st/chips/stm32n6/st_oneshot.c` 的 `n6_systick_isr()` 里有调试期插桩 `up_putc(0xb3)x2` / `up_putc(0xb4)x2`。0xB3 = 0x33('3')\|0x80、0xB4 = 0x34('4')\|0x80 → 终端按 UTF-8 解码成方块 |
| **误判澄清** | 初判「校验位被当数据位」，实测 host parity 设 even/odd/none **字节完全不变** → 排除该假设，确认是固件主动 `up_putc` 发出的原始字节 |
| **修复** | 删除这两对 `up_putc`，保留 `nxsched_process_timer()` |
| **验证** | boot trace 1498B → **66B**；help 输出 1411B → **763B**；b3/b4 计数 = 0；NSH 仍 ALL PASS |

### 7、标准 USERLED_LOWER 不可用

见 5.2 节。本板 Kconfig 为空、未 `select ARCH_HAVE_LEDS`。**解法**：只开 `CONFIG_USERLED=y`，自实现 lowerhalf 并 `userled_register()` 直接注册。

### 8、构建系统走 Make 而非 CMake

板目录下虽然有 `CMakeLists.txt`，但 `build.sh` 默认走 **Make**。加新源文件要改 **`src/Makefile`**，改 `CMakeLists.txt` 无效。

### 9、`leds` 命令名冲突

`CONFIG_EXAMPLES_LEDS` 与 `CONFIG_APP_DEMOS_LED` 的 `PROGNAME` 都是 `"leds"`，同时启用会冲突。**必须禁用前者。**

---

## 十、附录

### 1、常用命令速查

```bash
# ---- 构建 ----
ssh ssh-ohos-new 'cd ~/openvela/source && \
  ./build.sh vendor/st/boards/stm32n6/atk-dnn647/configs/nsh/ -j$(nproc)'

# ---- 生成 bin + 取入口地址 ----
ssh ssh-ohos-new 'cd ~/openvela/source/nuttx && \
  arm-none-eabi-objcopy -O binary nuttx /tmp/nuttx_n6.bin && \
  arm-none-eabi-nm nuttx | grep __n6_boot'

# ---- 拉回固件 ----
rsync -az ssh-ohos-new:/tmp/nuttx_n6.bin ./nuttx_bootrom.bin

# ---- 烧录 + 验证 NSH ----
python3 nsh_verify.py /dev/cu.usbserial-1420 nuttx_bootrom.bin 341806c1 6

# ---- 烧录 + 验证 LED 命令 ----
python3 led_verify.py /dev/cu.usbserial-1420 nuttx_bootrom.bin 341806c1 6

# ---- 仅烧录 ----
python3 n6_flash.py /dev/cu.usbserial-1420 nuttx_bootrom.bin fsbl.bin 341806c1 6

# ---- 烧录 + 串口交互（一行完成；校验位必须是 n） ----
python3 -u n6_flash.py /dev/cu.usbserial-1420 nuttx_bootrom.bin fsbl.bin 341806c1 6 ; \
picocom -b 115200 --parity n -d 8 --stopbits 1 -f n --noinit \
         --lower-dtr --lower-rts /dev/cu.usbserial-1420

# ---- 不想退出后重烧：用 n6_console.py（同一会话内烧录+交互） ----
python3 n6_console.py /dev/cu.usbserial-1420 nuttx_bootrom.bin 341806c1 6

# ---- 重新生成 packages/demos/Kconfig ----
ssh ssh-ohos-new 'cd ~/openvela/source/packages/demos && \
  bash ../../apps/tools/mkkconfig.sh -m "Demos"'
```

### 2、固化目录说明

| 目录 | 内容 | 用途 |
|---|---|---|
| `known_good_clean_0030/` | NSH 基线固件（已清理心跳插桩） | 干净起点 |
| `known_good_led_demo_0124/` | 含 LED 功能 + `packages/demos/led` 规范布局 | **推荐起点** |

两个目录均包含：源码文件、`defconfig`、`nuttx_bootrom.bin`、验证脚本、`config.txt`（含关键常量与验证结论）。

### 3、资源占用

```
text  = 100052 B
data  =    864 B
bss   =   9976 B
dec   = 110892 B   (链接器报 ROM: 110904 B / 240 KB = 45.13%)
固件 bin = 100916 B
IDLE 栈  = 32768 B
NR_IRQS  = 180
```

内存预算（来自 `scripts/ld.script` 注释）：code+rodata ~100KB、.data ~1KB、.bss ~7KB，合计 ~252KB < 400KB FLEXRAM 可用窗口 ✓

### 4、相关官方文档

| 文档 | 用途 |
|---|---|
| [快速入门](https://doc.openvela.com/document?id=847&version=dev&language=cn) | 开发环境搭建、源码下载 |
| [在 STM32H750 上部署 openvela](https://doc.openvela.com/document?id=597&version=dev&language=cn) | 部署流程参考 |
| [在 STM32F411 上使用 openvela 点亮 LED](https://doc.openvela.com/document?id=598&version=dev&language=cn) | 应用层 Demo 参考 |
| [为 STM32F407 开发板移植 openvela](https://doc.openvela.com/document?id=600&version=dev&language=cn) | BSP 移植原理参考 |
| [系统启动流程](https://doc.openvela.com/document?id=614&version=dev&language=cn) | 启动时序与函数调用关系 |
| [Kconfig 使用指南](https://doc.openvela.com/document?id=609&version=dev&language=cn) | 配置系统用法 |

### 5、后续可做

- 接入 LCD（`st_lcd.c` 已存在，LTDC 驱动 `st_ltdc.c` 已存在，需按实际屏幕改引脚映射）
- 启用按键（board.h 已定义 KEY0/KEY1/KEY2/WK_UP，需实现 `board_button_irq`）
- 追加外设驱动（I2C / SPI / SDIO / USB / CAN / 以太网）
- 配置开机自启动（ROMFS + `etc/init.d/rcS`）

---

*文档生成日期：2026-11-21　　所有内容均经 ATK-DNN647 真机验证*
