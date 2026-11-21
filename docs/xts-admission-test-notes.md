# xTS 准入测试 —— 探索记录与结论

> 日期：2026-11-21
> 目标板：ATK-DNN647（STM32N647，Cortex-M55，**无片内 Flash**）
> 依据：openvela《xTS 认证测试用例精简集》V1.0（2026-05-19），通用自测用例为必测项

---

## 结论摘要

**xTS 全套测试无法在本板以单颗固件形式运行**，根因是两条硬件约束：

1. **镜像体积上限**：串口 Boot ROM 拒绝过大的镜像（实测 113,704B 可启动，172,664B 不可）；
   而全套 xTS 编译产物 187,720B，超出该上限。
2. **无空闲 RAM 可做破坏性测试**：板无片内 Flash，代码/数据/堆/栈全部挤在约 240KB 的
   FLEXRAM 窗口内，堆仅约 86KB；`ramtest` 找不到安全的目标区域（连 DTCM 也会 fault）。

**当前验证状态**：`configs/nsh`（NSH + LED）已在真机 **RESULT: ALL PASS**，
这是本板的已验证交付基线。`configs/xts` 保留为探索性配置，**未达到验收标准**。

---

## 一、实测的镜像大小与启动能力

串口 Boot ROM 逐块接收镜像后跳转。逐个配置实测：

| 配置 | 包含内容 | ROM | bin | 启动 |
|---|---|---|---|---|
| `nsh` | NSH + LED | 110,904B | 100,916B | ✅ 正常 |
| `nsh` + `TESTING_MM` | 单一测试 | 114,192B | 104,060B | ✅ 正常 |
| `nsh` + `TESTING_MM` + cmocka/testsuites | 内核测试框架 | 117,688B | 105,320B | ✅ 正常 |
| `nsh` + cmocka/mm/fstest/ramtest/driver_test | 驱动测试框架 | 126,080B | 113,704B | ✅ 正常（NSH ALL PASS） |
| **全套 xTS** | 上述 + ostest/getprime/scanftest | **187,720B** | **172,664B** | ❌ **无任何串口输出** |

**失败特征**：烧录返回 `downloaded N blocks, fails=0`、`start ACKs: 121 121`（ROM 已接受并启动），
但 boot trace **0 字节** —— 连最早期的 `S` 标记都没有。

**排除项**（均实测）：
- 向量表正常：`_vectors = 0x34180400`，`vector[0]`（初始 SP）= `0x341b1584`，有效
- `g_idle_topstack` 值正确：`_ebss + IDLETHREAD_STACKSIZE`
- 入口地址正确：`__n6_boot = 0x341806c0`，`ENTRY = 0x341806c1`
- 与 idle 栈大小无关：32KB 与 16KB 均失败
- 硬件无问题：同一块板回退 `nsh` 版立即可 ALL PASS

**结论**：失败边界在 113KB~172KB 之间，性质是镜像体积超限，而非代码缺陷。

---

## 二、逐用例执行结果（`configs/xts`，bin 113,704B）

烧录后 `nsh_verify.py` → **RESULT: ALL PASS**（help/uname/ls/ps/`ls /dev/userleds` 全通过），
在此基础上执行 xTS 用例：

### ✅ 可正常执行

| xTS 用例 | 命令 | 结果 |
|---|---|---|
| 1.3.1 烧写测试 | `n6_flash.py` 串口 Boot ROM | ✅ 烧录成功、启动进 NSH |
| 设备节点枚举 | `ls /dev` | ✅ console / null / ram0 / userleds / zero |
| 系统 RAM 统计（1.2.3） | `free` | ✅ 正常工作（`Umem` 行：total 101,096 / used 8,160 / free 92,936）。此前缺此项是因 `CONFIG_FS_PROCFS_EXCLUDE_MEMINFO=y` 让 `nsh.h` 强制 `#define CONFIG_NSH_DISABLE_FREE 1`，去掉该选项后恢复 |
| 大量文本输出 | `help -v`（>2KB） | ✅ 完整输出并返回提示符（**TX 链路无吞吐问题**） |
| 任务列表 | `ps` | ✅ 正常 |

### ✅ 第 2 批（ostest / getprime / scanftest）实测

| xTS 用例 | 命令 | 镜像 | 结果 |
|---|---|---|---|
| 1.1.5 getprime 多线程性能 | `getprime` | 104,192B | ✅ **通过**：找到 1230 个素数，耗时 540ms |
| 1.1.7 sscanf 测试 | `scanftest` | 120,416B | ✅ **OK: 164, FAILED: 0**（全通过） |
| 1.1.4 Kernel-ostest | `ostest` | **172,988B** | ❌ **超 Boot ROM 上限 40%，无法运行** |

`scanftest` 能全通过的前提：需要 `/tmp` 可写（否则 `fscanf` 两项失败）。
为此在 `atk_dnn647_bringup()` 中增加了 tmpfs 自动挂载（仅 `CONFIG_FS_TMPFS=y` 时编译）：

```c
mkdir("/tmp", 0777);
nx_mount(NULL, "/tmp", "tmpfs", 0, NULL);
```

修好前是 `OK: 94, FAILED: 2`（两项都是 `Error opening /tmp/scanftest.txt for write`），
修好后 `OK: 164, FAILED: 0`（启用浮点后测试项本身也从 96 项增加到 164 项）。

### ⚠️ 部分可用 / 受硬件限制

| xTS 用例 | 观察 | 判定 |
|---|---|---|
| 1.1.6 Kernel-mm 内存测试 | `mm` 正常启动：系统堆 87,916B，分配/释放与 mallinfo 变化正确；随后停滞 | ⚠️ 堆仅 86KB，测试预期远超此规模 |
| 1.3.3 RAM 读写性能 `ramtest` | `ramtest -a 0x20000000 -s 32768` → `RAMTest: Marching` 后 panic | ❌ 无安全目标区；DTCM 访问同样 fault |
| 1.3.2 RAM 读写 `fstest` | 需要 `/tmp` 已挂载文件系统 | ❌ 本配置未启用文件系统 |
| 1.3.6/1.3.10 GPIO/UART 驱动测试 | `driver_test` 命令未注册 | ❌ `TESTING_DRIVER_TEST` 依赖未满足 |

### ❌ 未执行

- 1.1.x 其余内核用例（需 ostest/getprime/scanftest，属另一颗固件）
- 1.2.x 启动类（`Reboot`/`Cold boot` 语义特殊：无 Flash → reboot 后固件丢失）
- 1.2.4 Flash 资源统计、1.3.5 Flash 功能测试 —— **本板无片内 Flash，不适用**
- 3.1.1 12h 稳定性 —— 未执行

---

## 三、两个真实的工程结论

### 3.1 无 Flash 平台的 xTS 必须分批

官方《移植指南》§五以 1.4MB Flash 的参考板为背景。在无 Flash、代码与数据共享
同一块 SRAM 的设计上：

- 每增加一个测试套件，代码体积直接挤压堆空间与镜像体积上限
- 全套 187KB 超出 Boot ROM 接受范围
- **必须拆成多颗固件（每颗 <120KB）分别烧录测试**

这不是代码质量问题，是硬件约束。建议后续按「内核测试 / 驱动测试 / 其余」三批组织。

### 3.2 ostest 单独一批也放不下

`Kernel-ostest` 体积 70KB（nsh 102,924B → 加 ostest 后 172,988B），且它的 Kconfig
没有细粒度裁剪开关（只有 FPU 测试开关与循环次数），无法瘦身到可用范围。

结论：**ostest 在本板无法运行**，除非放弃 LED/NSH 等现有功能。这属于硬件容量限制。

### 3.3 堆只有 86KB，部分用例天然跑不了

`mm` 测试的分配规模明显按更大内存设计（可以看到它请求 124,336B 时直接
"exceeds the threshold, skipping"，说明阈值本身就设得比本板可用内存大）。

要让 `mm` 完整通过，需要：
- 缩小各测试的数据规模（需改测试源码，不属于 BSP 适配范围），或
- 提供更大的连续堆（本板做不到）

---

## 四、另外发现的一个构建系统坑（对后续开发者有用）

删除某个 `configs/<name>/` 目录后，`nuttx/Make.defs` 会变成**失效软链**
（它是指向 `configs/<name>/../../scripts/Make.defs` 的符号链接），
导致后续 `build.sh` 全部失败并报：

```
tools/Unix.mk:32: nuttx/Make.defs: No such file or directory
```

**修复**：`rm -f nuttx/Make.defs`，然后重新 `build.sh`（或手动重建软链指向
`vendor/st/boards/stm32n6/atk-dnn647/scripts/Make.defs`）。

---

## 五、当前状态与后续计划

| 项 | 状态 |
|---|---|
| `configs/nsh`（NSH + LED） | ✅ 真机 ALL PASS，**已验证交付基线** |
| `configs/xts` | ⚠️ 探索性配置，文件头已标注 WIP，**不计入验收** |
| 分批跑通全部通用必测用例 | 🔲 待做（需 3 颗固件） |
| 12h 稳定性 | 🔲 待做 |

**原则**：宁可如实标注「未完成」，也不把未验证的配置当作验收结果提交。
