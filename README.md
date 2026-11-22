# contest2026_204_guangnianzhandui — openvela 移植 STM32N647（ATK-DNN647）

**队伍**：光年战队（编号 204）  **赛道**：新硬件平台适配  **开发板**：正点原子 ATK-DNN647（STM32N647X0H3Q，大赛官方板）

本仓是光年战队的专属参赛仓库。作品为 **NuttX/openvela 生态首个 STM32N6 系列板级适配**：完成芯片层（ARMv8-M 启动、NVIC、USART1 控制台、堆管理）与板级层（链接脚本、800MHz 时钟树、引脚定义），编译 100% 通过，产出可运行于 FLEXRAM 的 nuttx ELF（NSH 控制台）。

## 仓库结构

```text
board/
├── chips/stm32n6/              # 芯片层（Cortex-M55 启动/NVIC/USART/堆）
└── boards/stm32n6/atk-dnn647/  # 板级层（链接脚本/时钟/引脚/NSH defconfig）
.claude/skills/stm32-board-porting/  # 自建 Skill：STM32 板卡移植工作流
app/
└── led_demo/                   # LED 控制应用（openvela 规范：packages/demos）
docs/
├── technical-report.md         # 技术报告（官方模板 3.1–3.7）
├── xts-admission-test-notes.md # xTS 准入测试探索记录（含逐项实测数据）
└── porting-guide/              # 完整移植部署文档（十章，含踩坑清单）
scripts/                        # 一键烧录 + 验证脚本
├── n6_flash.py                 # 串口 Boot ROM 烧录
├── nsh_verify.py               # 烧录 + NSH 命令验证（RESULT: ALL PASS）
└── led_verify.py               # 烧录 + LED 命令验证
logs/HonestQiao/                # AI Coding 日志（manifest.json + 会话 jsonl）
contest2026_204_guangnianzhandui.xml  # repo manifest：linkfile 映射到编译树
```

`board/` 下代码通过 manifest 的 `<linkfile>` 软链到 openvela 工作区：

| 本仓路径 | 链接到 |
|----------|--------|
| `board/chips/stm32n6` | `vendor/st/chips/stm32n6` |
| `board/boards/stm32n6` | `vendor/st/boards/stm32n6` |
| `app/led_demo` | `packages/demos/led` |

## 构建（在 openvela 工作区根目录）

```bash
repo init -u https://github.com/open-vela/contest2026_204_guangnianzhandui \
  -b dev-ai-contest-2026 -m contest2026_204_guangnianzhandui.xml
repo sync -c -j8
cd ..  # openvela 工作区根（含 nuttx/ apps/ vendor/ ...）

# 构建（Make 模式，非 --cmake）
./build.sh vendor/st/boards/stm32n6/atk-dnn647/configs/nsh/ -j$(nproc)
```

产物：`nuttx`（ELF，加载地址 0x34180240 / FLEXRAM）。实测：
ROM 110,904B/240KB (45.13%)，RAM 9,976B，DTCM 0B。

可选配置：`configs/nsh`（默认，NSH + LED）、`configs/xts`（xTS 测试框架，
**探索性、未达验收标准**，详见 `docs/xts-admission-test-notes.md`）。

> **入口地址随固件大小变化，每次构建后必须重新取**：
> ```bash
> arm-none-eabi-nm nuttx | grep __n6_boot   # ENTRY = 该地址 + 1
> ```

## 烧录运行（真机）

STM32N6 **无内部 Flash**，固件加载到 FLEXRAM 执行；且本板未接 ST-Link，
因此走 **串口 Boot ROM** 烧录（本平台唯一可行的部署方式）。

**一键烧录 + 验证**（推荐）：

```bash
# NSH 全命令验证（期望输出 RESULT: ALL PASS）
python3 scripts/nsh_verify.py /dev/cu.usbserial-1420 nuttx_bootrom.bin <ENTRY> 6

# LED 命令验证
python3 scripts/led_verify.py /dev/cu.usbserial-1420 nuttx_bootrom.bin <ENTRY> 6
```

**仅烧录**：

```bash
python3 scripts/n6_flash.py /dev/cu.usbserial-1420 nuttx_bootrom.bin fsbl.bin <ENTRY> 6
```

**串口交互**（⚠️ 参数必须用 `--parity n`，且必须禁用 DTR/RTS）：

```bash
picocom -b 115200 --parity n -d 8 --stopbits 1 -f n --noinit \
         --lower-dtr --lower-rts /dev/cu.usbserial-1420
```

> `--parity` 必须用 **`n`（无校验）**——实测 `--parity e` 完全收不到数据；
> 本板 USART 为 9 位字长（8 数据 + 1 校验），主机 8N1 即可正常收发。
>
> `--noinit --lower-dtr --lower-rts` 三个都要带：CH340 的 DTR/RTS 接了板子的
> BOOT0/NRST，工具初始化时会复位板子；而固件跑在 SRAM，**硬件复位即丢失需重新烧录**。
> 但**正常退出 picocom 不会复位，固件仍在，可直接重连**（重连若报串口被占用，等 2–3 秒）。
>
> 完整步骤、寄存器地址速查、9 条踩坑详见 `docs/porting-guide/`。

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

## LED 控制命令（`leds`）

ATK-DNN647 板载 2 颗 LED，均为**低电平点亮**：

| LED | GPIO | 位置 |
|-----|------|------|
| LED0 | PG10 | 核心板 CNN647B |
| LED1 | PE10 | 底板 DNN647 |

```text
nsh> leds 0 on       # 点亮 LED0
nsh> leds 1 on       # 点亮 LED1
nsh> leds 0 off      # 熄灭 LED0
nsh> leds 0 blink    # LED0 闪烁（Ctrl+C 停止）
nsh> leds            # 两颗一起闪
nsh> leds -h         # 用法
```

底层实现：`board/boards/stm32n6/atk-dnn647/src/st_userled.c` 自行实现
`userled_lowerhalf_s` 并 `userled_register("/dev/userleds", ...)` 注册
（**不依赖** `CONFIG_USERLED_LOWER`——该选项需要 `ARCH_HAVE_LEDS`，
而 vendor_template 生成的板级 Kconfig 为空，选不中）。

应用代码位于 `app/led_demo/`（映射到 `packages/demos/led`），
遵循 openvela 规范：**独立 `APP_DEMOS_LED` 配置项，不复用 `EXAMPLES_LEDS`**，
社区 `apps/examples/leds` 保持原状未被修改。
