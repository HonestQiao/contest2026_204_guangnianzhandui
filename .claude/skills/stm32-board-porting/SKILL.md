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

## 常见坑（按踩坑顺序）

1. **Kconfig 语法**：openvela 部分仓库用 `osource`/`--help--`，NuttX kconfig-frontends 不支持 → 换 `source`/`---help---`；指向不存在文件的 `source` 直接注释掉
2. **交叉编译器**：必须 `CONFIG_ARCH_TOOLCHAIN_GCC=y` + board Make.defs 里 `CROSSDEV ?=`
3. **VPATH 缺架构目录**：Make 构建靠 VPATH 找源码，新架构目录（如 armv8-m）要加进 `nuttx/arch/arm/src/<arch>/Make.defs`
4. **uart_ops_s 成员名**：以 `nuttx/include/nuttx/serial/serial.h` 定义为准，随版本变化
5. **嵌套仓库**：vendor 下每个子目录（vendor/st 等）是独立 git 仓库，repo 工具管理；提交推送要在子仓库内做
6. **.gitignore `/*/`**：vendor 类仓库忽略所有一级目录，新板卡目录要 `git add -f` 或确认已跟踪

## 验证标准
- `make` 产出 nuttx ELF（`file nuttx` → ARM EABI）
- size 报告 ROM/RAM 占用合理
- 真机：USART 控制台出 NSH shell 提示符（开发板在身旁时）

## 参考
- 本仓库实例：`vendor/st/chips/stm32n6/`、`vendor/st/boards/stm32n6/atk-dnn647/`
- 研究报告：`docs/porting/stm32n647.md`（docs 仓库）
- NuttX 架构层：`nuttx/arch/arm/src/armv8-m/`
