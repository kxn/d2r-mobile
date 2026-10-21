# Android 普通 NVN 调用不再更新诊断计数

## 依据和边界

179 同一次官方 Simpleperf 全 PID cpu-clock 数据共有 1,065 个样本；
NvN 翻译线程占 618 个。另三条主要游戏线程分别占 165、107、102 个。
这三条线程中，`nvnth_381` 共 18 个叶样本，`nvnth_380` 共 9 个，
合计占全 PID 样本的 2.535%。名字表对应 `BindUniformBuffer` 和
`BindVertexBuffers`。精确宿主 ELF 显示每次调用都执行共享槽上的
`STADD`，即使没有要求输出诊断调用表。

这些样本的 PC 落在跳板末尾的 `br`；不能把全部时间归给原子指令，
也不能用 2.535% 预测帧率收益。固定 16 字节槽中计数和实现指针相邻，
多个函数槽还会共享缓存行；普通模式删除这些共享写，有明确减少指令及
缓存一致性流量的机制，不要求修改 NVN 资源、着色器或游戏逻辑。

同一 Surface 窗口为 7.777795205 秒。原 Perfetto 显示 NvN Running
6,465.081 ms，Runnable 合计 45.526 ms，最长单段 0.791 ms。
另外三条线程 Running 合计 4,994.437 ms；当前证据不支持把 NvN
瓶颈解释成调度饥饿。多数游戏栈无符号，不能将其命名为忙等或特定游戏任务。

源数据：`.local/logs/native-cpu-pool-pin-coherent-179-full2838-Q735-v1/native/official-performance/`。
官方 ReportLib 展开和原 Perfetto SQL 结果分别在
`.local/logs/cpu-pool-pin-coherent-179-full2838-offline-analysis-v1/` 与
`.local/logs/cpu-pool-pin-coherent-179-other-threads-review-v1/`。
这里只分析已有记录，没有新增采集、计时器或设备操作。

## 实现

生成入口仍为 `dm/gfx/tools/gen_protos.py`。保留原 523 个计数入口和
`nvn_thunk` 表；仅 `DM_ANDROID_APP` 另外生成 523 个免计数入口及表。
两种入口都将相同的 `nvn_slot[i]` 地址放到 `x16`，再从槽的 `+8`
读取实现指针并跳转。不改 16 字节槽、参数寄存器、返回值、未实现入口
或计时包装入口的 ABI。普通路径省掉 `mov x17,#1` 和 `stadd`。

初始化槽表时一次选定地址表，在首次发布 NVN 函数地址前完成：

| 启动配置 | Android APP |
| --- | --- |
| 普通启动，或外部性能分析模式 | 免计数 |
| 显式存在 `DM_NVN_FRAMES`、`DM_NVN_TSV`、`DM_NVN_PROF` 之一 | 原计数入口 |

按原诊断开关的存在性处理，因此 `DM_NVN_PROF=0`、
`DM_NVN_FRAMES=0` 仍选计数入口；`TSV_CALLS=0` 只控制 TSV 输出内容。
启动后不切换函数地址，不在每次调用中查环境变量、分支或读取新标志。
`DM_NVN_LOG` 和外部 Profile 开关不恢复调用计数。

免计数时 `dump_frame()` 跳过计数槽读取及次数报告，不输出虚假的零调用。
帧号推进、命令诊断、内存读数和看门狗、确定性时钟推进继续执行。
显式计数诊断的 TSV 和 `DM_NVN_PROF` 包装保持原行为。
Apple 和 Linux CLI 的默认计数行为不变。

Android 额外静态指令为 523 × 24 = 12,552 字节，地址表为
523 × 8 = 4,184 字节。最终链接重定位及 APK 压缩后的增量尚未测量。

## Host 资格

`dm/gfx/tools/tests/test_nvn_thunks.py` 使用实际生成器、NDK LLVM
和从生产源码提取的实际初始化、取址、帧边界函数：

- 生成文件重建一致；LLVM 核对 CLI 523 和 APP 1,046 个入口的每条指令、
  全部槽位偏移及两张地址表的实际重定位目标。
- 16 组 CLI/APP 冷初始化、523 个重复取址、启动后环境变化不改变地址、
  TSV/PROF/FRAMES 兼容、普通模式不伪报次数、帧边界副作用，ASan+UBSan 通过。
- 完整生产 `hle_nvn.c` 以原 NDK 编译 CLI 与 APP，包含共享池、原生资源、
  导入窗口编译选项，通过。
- CLI ELF 和 iOS Mach-O 的跳板对象分别与父版本逐字节相同。

原始记录在 `.local/logs/nvn-android-uncounted-thunks-v1/`。
初次使用系统 Clang 19 时因缺 ASan runtime 未能链接；随后使用现有
系统 `cc` 的 ASan+UBSan 完成测试，保留失败记录。
没有执行 ARM64 模拟器、真实游戏或设备部署；尚不能报告整帧净收益。

另查 `hle_count.c`：它只在显式 `DM_HLECOUNT` 存在时于装载阶段包装入口，
普通 APP 默认关闭，179 的既有配置未见该开关。本候选不改它或命令统计。
