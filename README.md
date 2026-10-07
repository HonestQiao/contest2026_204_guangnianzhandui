# contest2026_204_guangnianzhandui — openvela 移植 STM32N647（ATK-DNN647）

**队伍**：光年战队（编号 204）  **赛道**：新硬件平台适配  **开发板**：正点原子 ATK-DNN647（STM32N647X0H3Q，大赛官方板）

本仓是光年战队的专属参赛仓库。作品为 **NuttX/openvela 生态首个 STM32N6 系列板级适配**：完成芯片层（ARMv8-M 启动、NVIC、USART1 控制台、堆管理）与板级层（链接脚本、800MHz 时钟树、引脚定义），编译 100% 通过，产出可运行于 FLEXRAM 的 nuttx ELF（NSH 控制台）。

## 仓库结构

```text
board/
├── chips/stm32n6/              # 芯片层（Cortex-M55 启动/NVIC/USART/堆）
└── boards/stm32n6/atk-dnn647/  # 板级层（链接脚本/时钟/引脚/NSH defconfig）
.claude/skills/stm32-board-porting/  # 自建 Skill：STM32 板卡移植工作流
docs/
└── technical-report.md         # 技术报告（官方模板 3.1–3.7）
logs/HonestQiao/                # AI Coding 日志（manifest.json + 会话 jsonl）
contest2026_204_guangnianzhandui.xml  # repo manifest：linkfile 映射到编译树
```

`board/` 下代码通过 manifest 的 `<linkfile>` 软链到 openvela 工作区：

| 本仓路径 | 链接到 |
|----------|--------|
| `board/chips/stm32n6` | `vendor/st/chips/stm32n6` |
| `board/boards/stm32n6` | `vendor/st/boards/stm32n6` |

## 构建（在 openvela 工作区根目录）

```bash
repo init -u https://github.com/open-vela/contest2026_204_guangnianzhandui \
  -b dev-ai-contest-2026 -m contest2026_204_guangnianzhandui.xml
repo sync -c -j8
cd ..  # openvela 工作区根（含 nuttx/ apps/ vendor/ ...）
./tools/configure.sh -E ../contest2026_204_guangnianzhandui/board/boards/stm32n6/atk-dnn647/configs/nsh
make -j$(nproc)
```

产物：`nuttx`（ELF，加载地址 0x34000400 / FLEXRAM）。实测：ROM 112,096B/511KB，RAM 9,976B/1536KB。

## 烧录运行（真机）

STM32N6 无内部 Flash，固件加载到 FLEXRAM/SRAM 执行：

1. BOOT 引脚按正点原子手册配置（开发调试模式）；
2. STM32CubeProgrammer 经 ST-LINK 将 `nuttx` 下载到 **0x34000400**（FLEXRAM）；
3. 复位运行，USART1（PE5/PE6，115200 8N1）输出 NSH 提示符。

> 对公共仓库的构建修复（nuttx VPATH、Kconfig 兼容性等）以 PR 形式提交至 open-vela 对应仓库的 `dev-ai-contest-2026` 分支，见 docs/technical-report.md 3.4 节。

## 关于 PR 与 CLA

首次向本仓提交 PR 时会运行 `cla/signature` 检查。请先在 [openvela 官网签署 CLA](https://openvela.com/#/community/cla)（报名时的 GitHub 账号），再在 PR 下评论 `/check-cla` 复检。

## AI Coding 日志

`logs/` 目录按《AI Coding 日志归集与提交手册》组织：`logs/<github_login>/manifest.json` + `logs/<github_login>/<date>/<tool>__<sid>.jsonl`。本队使用 Claude Code（VSCode 插件），日志由 contest-log-collector 工具导出。

## LCD 驱动（LTDC）上机验证指南

`release/nuttx-nsh-ltdc-20261007.elf` 为启用 LTDC 的固件（ROM 115,984B/511KB，编译日志 `release/build-ltdc-20261007.log`）。接好 4.3" RGB 屏后按上述步骤烧录，串口应出现：

```
STM32N6 LTDC: 800x480 @ 16 bpp, fb=0x24100000 len=768000
```

预期现象：背光点亮，屏幕全黑（帧缓冲已清零）。NSH 下 `ls /dev` 应能看到 `fb0`。

三个待硬件确认项及快速定位（均在 `board/boards/stm32n6/atk-dnn647/include/board_lcd.h` 一处修改）：

| 现象 | 原因 | 修改 |
|---|---|---|
| 完全无背光 | 背光脚/极性 | `BOARD_LCD_BL_*` 宏 |
| 有背光无图像/花屏 | 像素时钟或时序极性 | `BOARD_LCD_*` 时序参数、`st_ltdc.c` GCR 极性位 |
| 颜色通道错位（红蓝互换等） | GPIO AF 号或 RGB 线序 | `BOARD_LCD_GPIO_AF`、引脚表 |

向 `/dev/fb0` 写 RGB565 数据即可显示（后续 LVGL  demo 基于此）。
