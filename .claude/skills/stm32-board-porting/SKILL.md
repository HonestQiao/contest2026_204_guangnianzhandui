---
name: stm32-board-porting
description: "将 openvela/NuttX 移植到新的 STM32 开发板的端到端工作流。覆盖芯片调研、BSP 骨架生成、芯片层(启动/NVIC/串口/堆)、板级层(链接脚本/时钟/引脚)、defconfig 配置、编译排错全流程。Trigger: 移植新开发板、port NuttX board、新硬件平台适配、stm32 bsp、板级支持包、board porting、add new chip."
---

# STM32 开发板 openvela 移植工作流

从 STM32N647 (ATK-DNN647, Cortex-M55) 移植实战中沉淀。适用于把 openvela（NuttX）带到任何新的 STM32 板卡；其他 ARM Cortex-M 芯片同理，只需替换芯片层细节。

## 适用场景

- 新硬件平台适配赛道 / 给 openvela 增加新板卡支持
- NuttX 架构层已有对应内核（如 armv7-m / armv8-m），但芯片无现成驱动

## 前置输入（向用户收集）

1. 芯片型号 + 开发板型号（如 STM32N647X0H3Q + 正点原子 ATK-DNN647）
2. 厂商引脚分配表（Excel）—— 决定 board.h 引脚宏
3. 内存映射（厂商 CMSIS 头文件 / 链接脚本模板）
4. 时钟树（HSE 频率、PLL 系数、目标 SYSCLK）
5. 关键外设引脚：调试串口、LED、按键（最小可运行集）

## 工作流（7 步）

### 第 1 步：调研与文档
- 读厂商 wiki/原理图，整理：内存映射表、时钟配置、引脚分配表
- 确认 NuttX 架构层是否已支持该内核（`nuttx/arch/arm/src/` 下 armv6-m/7-m/8-m）
- 产出研究文档（参考 docs/porting/stm32n647.md 结构）

### 第 2 步：生成 BSP 骨架
```bash
python3 vendor/template/rename.py <vendor> <board_name> <chip_name>
# 例: rename.py st atk-dnn647 stm32n6
```
- 重命名生成 `chips/<chip>/`（芯片层）与 `boards/<chip>/<board>/`（板级层）

### 第 3 步：构建系统
- `chips/<chip>/Make.defs`: `include armv8-m/Make.defs`（按实际内核选）
- `chips/<chip>/Kconfig`: `select ARCH_CORTEXM55` + MPU/FPU/缓存选项
- `boards/.../scripts/Make.defs`: 工具链 `CROSSDEV ?= arm-none-eabi-`

### 第 4 步：链接脚本（无内部 Flash 芯片特别注意）
- STM32N6 **无内部 Flash**：代码运行在 FLEXRAM/SRAM（如 ROM @ 0x34000400）
- 普通芯片：ROM @ 0x08000000 起始的 Flash 地址
- 保留 IRQ 向量表偏移（首 0x400 字节给 FSBL/向量表）

### 第 5 步：芯片层代码（chips/<chip>/）
| 文件 | 职责 | 要点 |
|------|------|------|
| `chip.h` | 中断号/NVIC 宏 | guard 宏命名 `__VENDOR_ST_CHIP_<CHIP>_CHIP_H`；`ARMV8M_PERIPHERAL_INTERRUPTS = NR_IRQS - NVIC_IRQ_FIRST` |
| `st_start.c` | `__start()` 入口 | 先 `arm_earlyserialinit()` 再 `nx_start()`；定义 `g_idle_topstack` |
| `st_irq.c` | NVIC 初始化 | 用 `NVIC_IRQ_SVCALL`/`NVIC_IRQ_HARDFAULT`（非 ARMV8M_IRQ_*）；默认优先级写 `0xf0f0f0f0`；勿重复定义 nvic.h 已有的优先级宏 |
| `st_uart.c` | 控制台串口 | `uart_ops_s` 成员是 `.setup/.shutdown/.attach/.detach/.receive/.rxint/.rxavailable/.send/.txint/.txready/.txempty`（**不是** txchar/rxchar） |
| `st_allocateheap.c` | 堆 | `extern const uintptr_t g_idle_topstack;`，`CONFIG_RAM_END - g_idle_topstack` |

### 第 6 步：板级层（boards/<chip>/<board>/）
- `include/board.h`: 时钟宏（HSE/PLL/SYSCLK）、LED/按键/UART 引脚 AF
- `src/st_ap.c`: `board_early_initialize()` / `board_late_initialize()` / `<board>_bringup()`
- **注意**：C 标识符不能有连字符，`atk-dnn647.h` 里的函数名用下划线 `atk_dnn647_bringup`

### 第 7 步：defconfig + 编译迭代
```bash
./tools/configure.sh -E ../vendor/st/boards/stm32n6/atk-dnn647/configs/nsh
make -j$(nproc)
```
最小 defconfig 必含：
```
CONFIG_ARCH_TOOLCHAIN_GCC=y     # 否则依赖生成用宿主 gcc
CONFIG_STACK_USAGE_WARNING=0    # 否则 -Wstack-usage= 缺参报错
CONFIG_ARCH_ICACHE=y            # 否则 cache.h 空宏与 arm_cache.c 冲突
CONFIG_ARCH_DCACHE=y
CONFIG_USART1_SERIAL_CONSOLE=y
```

## 真机闭环阶段的坑（只有拿到板子才会遇到）

> 以下 9 条全部来自 ATK-DNN647 真机 bring-up，每条都定位到根因并验证修复。
> **移植类任务的真正难点不在编译，而在真机闭环。**

### 坑 1：地址笔误 → HardFault，NSH 静默起不来

写裸寄存器地址时**逐位核对**。典型：RCC 寄存器基址 `0x46028000` + 偏移 `0x25C`
必须算成 `0x4602825c`，写成 `0x46028025c`（少一位）会写无效地址 → **HardFault**。

**症状**：boot trace 只有几个字节（如 `S b0 b0 b1 b1`），NSH 提示符永不出现。
**定位法**：在函数入口用**裸写标记字符**（`'1'`~`'6'`）逐步打点，看 trace 停在哪两个标记之间。

```c
#define URAW(c) do { \
    volatile uint32_t *uisr=(volatile uint32_t *)0x4201101CUL; \
    volatile uint32_t *utdr=(volatile uint32_t *)0x42011028UL; \
    uint32_t ug=200000; \
    while (!(*uisr & (1UL<<7)) && ug-->0) {} \
    *utdr=(c); \
  } while(0)
```

### 坑 2：Boot ROM 会把串口 RX 引脚还原成 analog

数据手册标 `PE6 = USART1_RX(boot)`，Boot ROM 期间已配好，但**ROM 跳转后被还原**。
表现为「板子能发（能看到 boot trace），但完全收不到命令」。

**必须在早期初始化里显式重配**：
```c
m = *afrl;  m &= ~(0xFUL << 24); m |= (7UL << 24); *afrl = m;  /* PE6 AF7 */
m = *pupdr; m &= ~(3UL << 12); *pupdr = m;                     /* no pull */
m = *moder; m &= ~(3UL << 12); m |= (2UL << 12); *moder = m;   /* 复用模式(2)，不是输出(1) */
```
⚠️ 顺序很关键：**先配 AFRL 再配 MODER**。

### 坑 3：帧格式 8E1，不是 8N1

`CR1 = 0x140d`：`M1:M0=01` 是**9 位字长**（8 数据 + 1 校验），`PS=0` 才是**偶校验**。
**`M0` 不是"奇校验"开关** —— 把它当奇偶位会反复误导排查方向。
主机必须 `PARITY_EVEN`；实测 host parity 设 even/odd/none，固件发出的字节完全一致。

### 坑 4：固件跑在 SRAM，复位即丢失

无片内 Flash 的板子，Boot ROM 只把固件下载到 SRAM，**不写任何非易失存储**。
按 REST / 拔插 USB / 被串口工具复位 → 固件消失，必须重新烧录。

### 坑 5：CH340 的 DTR/RTS 接了 BOOT0 与 NRST

串口工具打开/设备时会碰这两根线 → **复位板子**，而固件在 SRAM，复位即丢。

**实测可用的组合（本板真机验证）**：

```bash
# 烧录后紧接 picocom，一行完成
python3 -u n6_flash.py <port> nuttx_bootrom.bin fsbl.bin <entry> 6 ; \
picocom -b 115200 --parity n -d 8 --stopbits 1 -f n --noinit \
         --lower-dtr --lower-rts <port>
```

- **校验位必须是 `n`（无校验），不能用 `e`**：本板 USART 是 9 位字长
  （8 数据 + 1 校验），主机用 8N1 时板子把停止位当校验位收、驱动又不查
  校验错误位，8 位数据照样能还原；实测 `--parity e` 则完全收不到数据。
- `--noinit --lower-dtr --lower-rts` 三个都要带。
- **正常退出 picocom 不会复位，固件仍在 SRAM，可以直接重连**
  （再执行一次同样的 picocom 命令即可，无需重烧）。
  固件只在真正复位时丢失：按 REST 键、拔插 USB、或重新烧录。
  重连若报 `cannot lock ... Resource temporarily unavailable`，等 2~3 秒再连
  （上一个进程还没释放串口）。

> 教训 1：不要因为某组参数失败就断言"这条路不通"。当初用 `--parity e` 测
> 全部失败，就写成"picocom 不可用"，实际是参数错了。**换参数重测**。
>
> 教训 2：判断"固件是否丢失"前，**先确认串口真的释放了**。两次结论都错在
> 这里——退出 picocom 后立刻打开串口会 `cannot lock`，读回 0 字节，
> 被我误判成"固件丢了"。等 2~3 秒再测才是真实状态。

**`screen` 一律禁用**（设不了校验位）。

### 坑 6：调试期插桩污染串口输出

`st_oneshot.c` 的 SysTick ISR 里若留着 `up_putc(0xb3)x2` / `up_putc(0xb4)x2`，
每 10ms 冒一对非 ASCII 字节，终端显示为 `␄` 方块。
0xB3 = 0x33('3')|0x80 → **UTF-8 解码成方块**。

**判据**：先做实验再下结论——实测 host parity 设 even/odd/none 字节不变，
即可排除"校验位被当数据位"的假设，锁定是固件主动 `up_putc`。
清理后 boot trace 1498B → **66B**。

### 坑 7：标准 USERLED_LOWER 用不了

`CONFIG_USERLED_LOWER` 依赖 `CONFIG_ARCH_HAVE_LEDS`，而该符号需由板级 Kconfig
用 `select` 显式启用。**vendor_template 生成的板级 Kconfig 往往是空的** → 选不中。

**解法**：只开上半部 `CONFIG_USERLED=y`，自行实现 lowerhalf 并直接注册：
```c
static const struct userled_lowerhalf_s g_userled_lower = {
  .ll_supported = userled_supported, .ll_setled = userled_setled, .ll_setall = userled_setall,
};
ret = userled_register("/dev/userleds", &g_userled_lower);
```

### 坑 8：openvela 新增 Demo 的真实机制 ≠ 官方文档

官方文档说"手动改 `packages/demos/Make.defs` 加 `CONFIGURED_APPS`"、"手动加 Kconfig source 行"，
但实际版本里：
- 父级 Make.defs 已是 `include $(wildcard .../packages/demos/*/Make.defs)` → **不用改**
- `packages/demos/Kconfig` 是**自动生成的** → 加 demo 后跑
  `cd packages/demos && bash ../../apps/tools/mkkconfig.sh -m "Demos"`

**并且**：自定义 Demo 的 `PROGNAME` 若与社区示例同名（都是 "leds"）→ 冲突，
必须禁用社区的 `CONFIG_EXAMPLES_LEDS`。

### 坑 9：验证脚本自身可能有 bug

写板级验证脚本时，**必须用同一串口会话**完成"烧录→发命令→读响应"，
不要烧录后重开串口（帧同步会丢）。发命令用裸 `\r`（NSH 不认 `\r\n`）。
**不要用 `chr if 32<=b<127` 过滤字节**——会静默丢弃所有非 ASCII 字节，
让正常会话看起来"满屏是点"。

---

## 常见坑（按踩坑顺序）

1. **Kconfig 语法**：openvela 部分仓库用 `osource`/`--help--`，NuttX kconfig-frontends 不支持 → 换 `source`/`---help---`；指向不存在文件的 `source` 直接注释掉
2. **交叉编译器**：必须 `CONFIG_ARCH_TOOLCHAIN_GCC=y` + board Make.defs 里 `CROSSDEV ?=`
3. **VPATH 缺架构目录**：Make 构建靠 VPATH 找源码，新架构目录（如 armv8-m）要加进 `nuttx/arch/arm/src/<arch>/Make.defs`
4. **uart_ops_s 成员名**：以 `nuttx/include/nuttx/serial/serial.h` 定义为准，随版本变化
5. **嵌套仓库**：vendor 下每个子目录（vendor/st 等）是独立 git 仓库，repo 工具管理；提交推送要在子仓库内做
6. **.gitignore `/*/`**：vendor 类仓库忽略所有一级目录，新板卡目录要 `git add -f` 或确认已跟踪

## 真机验证标准（Definition of Done）

编译通过 **不等于** 完成移植。以下每一条都要真机实测：

| # | 验证项 | 判据 | 方法 |
|---|---|---|---|
| 1 | 启动链路 | boot trace 出现 NuttShell 且 `nsh>` 提示符可见 | 抓启动串口输出 |
| 2 | 命令交互 | help/uname/ls/free/ps 全部正常响应 | 验证脚本断言 ALL PASS |
| 3 | 帧格式 | 主机能收到板子命令、且板子能收到主机命令 | 双向验证，别只看单向 |
| 4 | 外设 | 每个声称支持的外设**物理动作可见** | 肉眼/仪器确认，如 LED 亮灭 |
| 5 | 输出纯净度 | 无调试残留噪声 | boot trace 字节数应接近最小值 |
| 6 | 可复现 | 换一个环境/时间重建，结果一致 | 提供一键烧录+验证脚本 |
| 7 | 文档 | 第三方照文档能复现 | 命令逐条实跑过 |

**易踩**：验证脚本里若用字节过滤，会把"噪声多"误判成"功能坏"。
判据要选**能区分信号与噪声**的指标（相对静默基线的增量），而不是绝对量。

## 验证标准
- `make` 产出 nuttx ELF（`file nuttx` → ARM EABI）
- size 报告 ROM/RAM 占用合理
- 真机：USART 控制台出 NSH shell 提示符（开发板在身旁时）

## 参考
- 本仓库实例：`vendor/st/chips/stm32n6/`、`vendor/st/boards/stm32n6/atk-dnn647/`
- 研究报告：`docs/porting/stm32n647.md`（docs 仓库）
- NuttX 架构层：`nuttx/arch/arm/src/armv8-m/`
