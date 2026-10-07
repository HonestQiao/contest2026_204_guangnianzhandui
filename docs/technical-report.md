# 2026 首届 openvela AI 硬件开发者大赛 · 技术报告

## 1、信息表

| 项目 | 内容 |
|------|------|
| 作品名称 | openvela 移植 STM32N647 —— Cortex-M55 + NPU 边缘 AI 硬件平台适配 |
| 队伍名称 | 光年战队 |
| 团队分工 | 独立完成 |
| 选题方向 | 新硬件平台适配 |

## 2、摘要

STM32N647 是 ST 新一代旗舰 MCU（Cortex-M55 800MHz + Neural-ART NPU 600GOPS + 4.2MB SRAM，无内部 Flash），但 openvela（NuttX）对其零支持，生态开发者无法在该硬件上使用 openvela 的组件化能力。本作品完成 STM32N647（正点原子 ATK-DNN647 开发板）的 openvela 全量板级适配：从芯片层（ARMv8-M 启动、NVIC 中断、USART 控制台、堆管理）到板级层（链接脚本、800MHz 时钟树、引脚定义），并修复 openvela 构建系统 7 类问题。成果：新增 39 个源文件，编译 100% 通过，产出 nuttx ELF（ROM 112,096B / 511KB，RAM 9,976B / 1536KB），串口 NSH 控制台就绪；沉淀可复用移植 Skill 1 个、AI Coding 日志全量归档。本作品是 NuttX/openvela 生态首个 STM32N6 系列 BSP。（真机串口验证因开发板暂不在手边，状态见 3.5 节如实说明）

## 3、正文

### 3.1 绪论

**项目背景与问题定义**

边缘 AI 设备（工业传感、视觉检测、语音交互）需要"够用算力 + 实时系统 + 低功耗"的异构平台。ST 2024 年推出的 STM32N6 系列是该方向的重要硬件：Cortex-M55（ARMv8.1-M，Helium MVE 向量扩展）@ 800MHz，集成 Neural-ART NPU（600 GOPS）与 NeoChrom GPU，4.2MB 片上 SRAM——但**没有内部 Flash**，代码运行在 FLEXRAM/SRAM 或外部 XSPI 存储器上。正点原子 ATK-DNN647 是该芯片的国内主流开发板，且为本大赛官方支持板卡。

痛点：**openvela 对 STM32N6 零支持**——NuttX 主线及 openvela 全分支均无 STM32N6 芯片层与板级 BSP；厂商侧仅提供 STM32Cube HAL 例程。结果是：想在 STM32N647 上用 openvela 的图形/AI/多媒体框架做产品的开发者，第一步（系统启动）都迈不出去。

**技术难点**

1. **无内部 Flash 的启动架构**：与所有现有 STM32 BSP（Flash @ 0x08000000）完全不同，需按"Boot ROM → FSBL → App"多级引导设计内存布局，链接脚本、向量表位置、加载地址全部重新规划。
2. **Cortex-M55（ARMv8.1-M）新内核**：构建系统需切到 armv8-m 架构层；启动代码、NVIC 接口、缓存使能与 ARMv7-M 存在差异（本作品还发现并修复了 armv8-m 在 Make 构建下 VPATH 缺失的真实 bug）。
3. **Kconfig 方言差异**：openvela 部分仓库使用 `osource` 等非标准关键字，NuttX kconfig-frontends 解析直接报错，需逐项定位修复。
4. **从零起步的驱动**：无现成代码可移植，USART 驱动需按寄存器级（GPIO 复用 AF7、波特率分频）手写。
5. **RIF/TrustZone 资源隔离**：STM32N6 引入 Resource Isolation Framework，外设访问权限需在后续驱动开发中正确处理。

**创新点**

- **首个 STM32N6 系列 openvela/NuttX BSP**：填补该旗舰芯片在 NuttX 生态的空白，板卡为大赛官方板 STM32N647。
- **针对"无 Flash"架构的完整启动链设计**：提出并落地 FLEXRAM 直接运行的单阶段构建方案（当前）与 FSBL 两级引导路线（规划），为同架构芯片（STM32N6 全系）提供范式。
- **AI-Native 开发范式**：整个移植由 Claude Code 驱动，沉淀出可复用的 `stm32-board-porting` Skill，把"移植一块新 STM32 板"的经验产品化。

### 3.2 系统方案设计

**系统总体架构**

```
┌────────────────────────────────────────────────────────┐
│  应用层   NSH Shell / (规划：LVGL 图形、NPU 推理 demo)   │
├────────────────────────────────────────────────────────┤
│  openvela 组件  procfs / romfs / serial driver framework│
├────────────────────────────────────────────────────────┤
│  NuttX 内核  调度 / 中断 / 内存管理（arch/arm/armv8-m） │
├──────────────┬─────────────────────────────────────────┤
│ 芯片层 BSP    │ 板级层 BSP（vendor/st）                  │
│ chips/stm32n6│ boards/stm32n6/atk-dnn647                │
│ 启动/NVIC/   │ 链接脚本/时钟树(800MHz)/引脚/初始化       │
│ UART/堆管理  │                                         │
├──────────────┴─────────────────────────────────────────┤
│  硬件：STM32N647X0H3Q                                   │
│  FLEXRAM 400K + SRAM1_AXI 1MB(×2) + DTCM + XSPI NOR/HyperRAM│
│  引导：Boot ROM(0x0800_0000) → FSBL → App(0x3400_0400) │
└────────────────────────────────────────────────────────┘
```

采用 openvela **board-only vendor 模式**：芯片代码进芯片层，厂商仓库（vendor/st）只持有板卡 BSP，与 openvela 多仓库（repo）管理模型一致。

**方案论证与选型**

- **开发板选型**：STM32N647 为本大赛官方板；Cortex-M55 + NPU 的边缘 AI 定位与"AI 硬件"赛题契合；4.2MB SRAM 足以承载 openvela 图形栈；无 Flash 架构虽是难点，但正是新硬件适配赛道的价值点。
- **OS 选型**：openvela 基于 Apache NuttX，组件化 Kconfig 配置、POSIX 接口、已有 LVGL/多媒体框架，适合快速构建产品级固件；相比裸机 HAL，可获得文件系统、网络协议栈、OTA 等完整能力；相比 RT-Thread，openvela 的国际化生态与大赛资源（MiMo、专属仓、评审链）更完整。
- **端/云职责**：本作品为系统层适配，端侧全自主运行（无云端依赖，天然支持断网）；规划中的 NPU 推理完全端侧，云端仅用于开发期的 AI Coding 辅助。

**关键模块设计**

- **启动链模块**：当前为单阶段构建（FSBL 由调试器/ST-LINK 代替，App 直接链到 FLEXRAM 0x34000400 运行）；规划中 FSBL 驻留 SRAM3_AXI，上电由 Boot ROM 加载后初始化 XSPI，再加载 App。
- **串口控制台模块**：USART1（PE5/PE6，AF7），BRR=556 分频（64MHz/115200），对接 NuttX serial driver framework，NSH 经由它交互。
- **内存管理模块**：空闲线程栈顶（`_ebss + CONFIG_IDLETHREAD_STACKSIZE`）之上至 SRAM1_AXI 末尾全部划为堆，DTCM 128KB 预留给高频数据。

### 3.3 核心算法与技术原理

**AI 算法实现**

本作品属系统适配类，端侧 AI 推理为规划内容：STM32N647 集成 ST Neural-ART NPU（600 GOPS @ 1GHz，支持 INT8/INT16），规划路线为 ONNX 模型经 ST 工具链转换后由 NPU 运行时调度，Cortex-M55 Helium 作为回退算力；云端大模型侧，开发过程使用大赛 MiMo（经 API 接口）辅助代码生成与文档撰写。

**关键机制设计**

1. **无 Flash 芯片的链接与加载机制**：传统 STM32 链接脚本以 0x08000000 为基址；本作品将 ROM 区重定位到 FLEXRAM（0x34000000，零等待总线），首 0x400 字节保留（FSBL 标志/向量表重定向），RAM 区落在 SRAM1_AXI（0x34080000，1536KB），DTCM 区（0x20000000，128KB）独立保留——三区分栏，兼顾执行速度与数据吞吐。
2. **NVIC 优先级与中断分发**：沿用 NuttX armv8-m 层向量模板，外设中断号经 `ARMV8M_PERIPHERAL_INTERRUPTS = NR_IRQS - NVIC_IRQ_FIRST` 映射；默认优先级 0xF0 批量写入，SVCALL/HARDFAULT 挂内核默认 handler。
3. **时钟树配置**：HSE 48MHz → PLL（M=6, N=100, P1=1）→ SYSCLK 800MHz，USART1 时钟 64MHz，所有系数以宏形式集中在 board.h，便于按板型重配。

**openvela 系统能力的深度运用**

- **已落地（系统基础能力）**：NuttX 内核调度、procfs/romfs 文件系统、serial driver framework、Kconfig 组件化配置、NSH 命令行。
- **图形能力（规划，硬件已备）**：板载 4.3" RGB LCD（800×480，24bit LTDC 接口，核心板完整引出 PA/PB/PG bank），计划以 LTDC 驱动 + LVGL 落地图形能力，引脚资源已在本作品引脚表中完整梳理。
- **对 openvela 的改进（已提交）**：修复 armv8-m Make 构建 VPATH 缺失（真实 bug）；修复 Kconfig 解析兼容性 6 处（osource 方言、悬空 source、--help-- 写法、缺失行尾换行导致的跨文件栈错乱）；更多改进建议见 3.7。

### 3.4 系统实现

**软件/固件架构**

```
vendor/st/
├── chips/stm32n6/            # 芯片层（与板无关）
│   ├── chip.h                # 中断映射、NVIC 宏
│   ├── Make.defs / Kconfig   # 构建系统（select ARCH_CORTEXM55）
│   ├── st_start.c            # __start() → arm_earlyserialinit() → nx_start()
│   ├── st_irq.c              # NVIC 初始化、优先级、使能/禁用
│   ├── st_uart.c             # USART1 寄存器级驱动（uart_ops_s）
│   └── st_allocateheap.c     # 堆：g_idle_topstack → CONFIG_RAM_END
└── boards/stm32n6/atk-dnn647/ # 板级层
    ├── include/board.h       # 时钟宏、LED(PG10/PE10)、按键、UART 引脚
    ├── scripts/ld.script     # FLEXRAM/SRAM1_AXI/DTCM 三区链接脚本
    ├── scripts/Make.defs     # armv8-m 工具链
    ├── configs/nsh/defconfig # 最小 NSH 配置
    └── src/st_ap.c           # early/late init + atk_dnn647_bringup()
```

**数据流与关键流程（启动流程）**

```
上电/复位
   │  Boot ROM @ 0x08000000 运行（ST 固化，不可修改）
   │  当前：调试器加载           规划：Boot ROM 加载 FSBL 到 SRAM3_AXI
   ▼
__start()  [st_start.c]  ← 链接地址 0x34000400 (FLEXRAM)
   │  1. 使能 ICACHE/DCACHE（armv8-m 层）
   │  2. arm_earlyserialinit()  → USART1 @ 115200 就绪
   │  3. nx_start()  → NuttX 内核初始化
   ▼
nsh_main()  →  procfs 挂载（atk_dnn647_bringup）→ NSH 提示符
   ▼
用户命令经 USART1(PE5/PE6) 交互
```

**硬件设计与适配（新硬件适配赛道核心）**

- **是否完成全新硬件平台适配**：**是**。芯片 STM32N647X0H3Q（Cortex-M55，ARMv8.1-M），开发板正点原子 ATK-DNN647（大赛官方板）。
- **驱动类型**：启动代码、NVIC 中断控制器、USART1 串口（寄存器级）、时钟树/内存布局。
- **适配难点与解决方案**：

| 难点 | 解决方案 |
|------|---------|
| 无内部 Flash，现有 STM32 BSP 范式不可用 | 重设计链接脚本：ROM→FLEXRAM 0x34000400，RAM→SRAM1_AXI，DTCM 独立保留 |
| armv8-m 在 Make 构建下找不到 arm_cache.c 等源码 | 修复 nuttx 构建系统：armv8-m 加入 VPATH（已提交 nuttx 仓库） |
| kconfig-frontends 不支持 `osource`/`--help--`，悬空 source 报错 | 逐项定位 6 处：改 `source`、注释悬空引用、修正 endmenu 配对（已提交 apps/external/tests 仓库） |
| `uart_ops_s` 结构体成员随版本变化 | 以 nuttx serial.h 现行定义为准实现 12 个回调 |
| 交叉编译器选型错（宿主 gcc 吃 -mfloat-abi） | defconfig 增加 `CONFIG_ARCH_TOOLCHAIN_GCC=y` + 板级 `CROSSDEV ?=` |
| 头文件保护宏/标识符命名冲突 | 修正 guard 宏；连字符板名函数改下划线（`atk_dnn647_bringup`） |

- **关键 BOM/接口**：核心板 ATK-CNN647B + 底板 ATK-DNN647；USART1 经 PE5/PE6 接 ST-LINK 虚拟串口；外设引脚全集（LTDC/I2C2/SDMMC/XSPI/DVP）已整理于研究文档。

**自定义 Skill（硬性要求）**

新增 `stm32-board-porting` Skill（位于专属仓 `.claude/skills/stm32-board-porting/`）：把本次移植的 7 步工作流（调研输入清单→骨架生成→构建系统→链接脚本→芯片层→板级层→defconfig 编译迭代）与 6 类踩坑记录产品化。**触发场景**：任何"把 openvela/NuttX 移植到新 STM32/ARM 板卡"的任务，AI 读取后即可按图施工，无需重新摸索。

**应用/交互端设计**

当前交互端为 NSH 命令行（USART1 115200 8N1）；图形交互端（LVGL on LTDC）为规划内容，引脚与内存资源已就位。

### 3.5 系统测试与结果分析

**测试环境**：x86_64 Linux，arm-none-eabi 工具链（GCC，支持 ARMv8.1-M），openvela repo 管理的 dev-ai-contest-2026 分支，configure.sh + make 构建。

**功能测试**

| 测试项 | 方法 | 结果 | 佐证 |
|--------|------|------|------|
| 固件编译 | `configure.sh -E` + `make -j` | ✅ 通过，0 错误 | 构建日志（会话日志已归档 logs/） |
| 产物架构正确性 | `file nuttx` | ✅ ARM Cortex-M firmware, EABI5, 静态链接 | file 输出 |
| 链接地址正确性 | 检查 ELF program headers | ✅ 加载地址 0x34000400 (FLEXRAM) | readelf 记录于研究文档 |
| 内存占用 | `size` / 链接报告 | ✅ ROM 112,096B/511KB (21.43%)，RAM 9,976B/1536KB (0.63%)，DTCM 0B | 链接器输出（见下） |
| NSH 串口交互 | 真机 USART1 回环 | ⏳ 待验证（开发板暂不在手边） | — |
| LED GPIO 翻转 | 真机 PG10/PE10 | ⏳ 待验证 | — |

链接器实际输出：
```
Memory region         Used Size  Region Size  %age Used
             ROM:      112096 B       511 KB     21.42%
             RAM:        9976 B      1536 KB      0.63%
            DTCM:           0 B       128 KB      0.00%
```

**性能测试**

| 指标 | 数值 | 状态 | 说明 |
|------|------|------|------|
| 编译产物大小 | ELF 190KB（含符号） | ✅ 实测 | 上文链接报告 |
| ROM/RAM 占用 | 21.43% / 0.63% | ✅ 实测 | 大量余量预留给图形/NPU 栈 |
| 冷启动时间（到 NSH 提示符） | 待真机测量（SRAM 执行零等待，预期亚秒级） | ⏳ | 需串口时间戳脚本，已与复测计划一并准备 |
| 推理帧率/时延 | 待 NPU 驱动落地后测量 | ⏳ | 无实测数据，不预估虚报 |

**可靠性与稳定性测试**：待真机环境补充连续运行（≥72h）、内存波动、异常恢复测试；测试脚本与判定标准已在复测计划中定义。

> 如实说明：本作品当前为"编译验证通过 + 真机验证待完成"状态。评审可 clone 专属仓复现编译结果（命令见 3.6 补充）；真机串口日志、演示视频将在取得开发板后补充上传。

### 3.6 AI-Native 开发说明

| 指标 | 数据 |
|------|------|
| AI Coding 代码占比 | 约 90%（口径：最终入库代码行中由 Claude Code 生成/修改的比例；人负责硬件资料收集、需求决策、方案评审与命令确认） |
| 使用的 AI 工具 | Claude Code（VSCode 插件，模型 mimo-v2.5-pro / kimi-for-coding 等） |
| MCP 工具使用情况 | 未使用专用 MCP；网络检索通过 WebFetch/WebSearch + 代理完成 |
| Skills 使用与新增 | 使用：官方 openvela-build、skill-creator、contest-log-collector 等；**新增沉淀：`stm32-board-porting`**（见 3.4） |
| Token 使用总量 | 以大赛控制台统计为准（本机会话日志已全量归档专属仓 logs/，可核对） |

**AI 工具对开发效率的提升**：移植类任务的信息密度极高（寄存器手册、Kconfig dialect、多仓库结构），AI 将"调研→骨架→排错"周期从预估的 2–3 周压缩到约 2 天；典型场景：AI 一次性定位出 6 处 Kconfig 报错根因并批量修复、按 serial.h 现行定义重写 UART 驱动回调集。**遇到的问题与解决**：AI 早期曾引用不存在的 NVIC 接口（`arm_prioritize_nvic`）与过时结构体成员，通过"以树内头文件为准"的核对口令纠正；开发板不在手边导致无法真机闭环，已如实标注。

### 3.7 总结与展望

**成果总结**

- 完成 STM32N647X0H3Q 芯片层 + ATK-DNN647 板级层 BSP，**NuttX/openvela 生态首个 STM32N6 适配**；
- 编译 100% 通过，产出可复现的 nuttx ELF（构建命令：`./tools/configure.sh -E ../vendor/st/boards/stm32n6/atk-dnn647/configs/nsh && make -j`）；
- 向 openvela 修复 7 处真实问题（armv8-m VPATH、6 处 Kconfig 兼容性），全部位于 dev-ai-contest-2026 分支；
- 沉淀 `stm32-board-porting` Skill 1 个；AI Coding 日志全量归档专属仓 logs/ 目录。

**应用前景与商业价值**

目标受众：边缘 AI 产品团队（工业视觉质检、智能传感、语音终端）、STM32 生态嵌入式开发者、高校教学。STM32N6 的 NPU+大 SRAM 定位中端边缘推理，openvela 补齐后，"openvela 图形/网络栈 + STM32N6 推理算力"可形成低成本端侧 AI 方案；商业模式上可复用本 BSP 做行业 ODM 固件交付，Skill 化移植方法可降低客户新板适配成本（预估从周级到天级）。

**不足与未来工作**

1. **P0**：真机验证（NSH 串口、LED），补串口日志与演示视频；
2. **P0**：FSBL 两级启动（Boot ROM→SRAM3→SRAM1），脱离调试器独立上电；
3. **P1**：LTDC RGB LCD 驱动 + LVGL，落地图形能力；I2C2 EEPROM、SDMMC 存储；
4. **P2**：DCMI OV5640 摄像头、SPI5 无线、FDCAN、以太网；
5. **P3**：Neural-ART NPU 推理 demo（端侧 AI 能力）、NeoChrom GPU 加速图形；
6. TrustZone/RIF 安全域配置；向 openvela 主线合入。

---

## 附：提交材料与仓库对照

| 材料 | 位置 | 状态 |
|------|------|------|
| 芯片/板级源码 | 专属仓 vendor/st `dev-ai-contest-2026` 分支（boards/stm32n6、chips/stm32n6） | ✅ 已提交 |
| 上游修复（nuttx/apps/external/tests） | 各仓库 `dev-ai-contest-2026` 分支 | ✅ 已提交 |
| AI Coding 日志 | 专属仓 `logs/HonestQiao/` | ✅ 已归档 |
| 自建 Skill | 专属仓 `.claude/skills/stm32-board-porting/` | ✅ 已提交 |
| 研究/过程文档 | docs 仓库 `porting/stm32n647.md` | ✅ 已提交 |
| 演示视频（≤5min） | 【取得开发板后录制补充】 | ⏳ 待补 |
| 实物照片 | 【取得开发板后拍摄补充】 | ⏳ 待补 |
