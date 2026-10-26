# 工作线程 QoS、采集开销与整线程指令预算（2026-10-27）

结论：提高新建 guest worker 优先级没有得到明显涨帧；现有应用采集标记也不能解释剩余差距。硬件 PMU 发现当前版本每个完整出图间隔退休的用户态指令比上游多约 36%，但用户态 CPU cycles 反而少约 9%。因此，“指令更多”可以定位多做的管理工作，不能直接解释约 40 对 43 FPS，也不能换算成同等 CPU 时间收益。

## 代码与实际运行条件

分支 `codex/android-worker-qos-v1`。`374a228` 增加启动时固定的 Android guest worker QoS 对照，`1f8f8ba` 补齐 gettid 声明，`4f5e67e` 修正启动工具的实际选择证明。只适配宿主线程优先级，没有改游戏逻辑、已加工模块、Shader、贴图或驱动。

原上游 Android `pthread/qos.h` 将 USER_INTERACTIVE 映射为 nice -10；上游 hle_thread 会调用，当前原路径只在 Apple 调用。本次增加显式 `--guest-worker-qos` / `--no-guest-worker-qos`：冷启动设置，默认关闭，无热路径环境变量读取。只涉及新建 guest worker，不改采用的主线程、UI、完成线程或驱动线程。每个 worker 记录 requested/actual nice、设置结果和 errno，不能用记录的 guest QoS 类替代系统实际优先级。

实验安装的是 `1f8f8ba0d3266b77ddbdb39a5a1e857bd619b463` 的规范构建：APK SHA256 `2216ec8c94618f779c7576fa188859654384c905f51a8f21f430c937746d9fce`，宿主 ELF `053c398ddab4c2faead47a1b8465de670ce60bc7f8243e0adae9038ecf044cbb`。固定产物和 build.json 在 `.local/build/thread-policy-parity-v1/`。`4f5e67e` 只改变启动工具的日志验证，不把另一次构建冒充已经部署。

设备为 USB Q735 / Adreno 735；1968×900 游戏输出、固定 0.85、2358×1080 Android 缓冲、f830 Turnip prefer_sysmem、共享 native 资源、shader chain `29e56b51bce5`、原调用者内翻译、CPU mask 0x80、关闭非必要 stream submit。每组进入真实游戏营地，tick≥2500 且八次不同状态观测的管线/特化数量稳定才采集；窗口内 PSO 180、variants 32 不增长。不是探针或 CPU 模拟执行。

## 优先级实际对照

官方 Perfetto 完整 Surface 间隔、调度 Running；20 秒窗口。A/B/A 三次冷启动，中间 B 同 PID 另采一次，不称四次独立冷启动。

| 组 | FPS | 整个游戏/翻译 owner Running ms/出图间隔 |
| --- | ---: | ---: |
| A1，不改 worker | 40.082 | 20.146 |
| B1，worker nice -10 | 40.334 | 20.016 |
| B1，同 PID 重复 | 40.265 | 20.053 |
| A2，不改 worker | 40.143 | 20.044 |

A 平均 40.112，B 平均 40.299 FPS，差约 0.47%，不足以认定明显改善。B 实际 21 个 worker 均读到 nice -10、set_result=0、errno=0；owner 主线程优先级未改。保留冷启动实验与实际验证，不宣称减少翻译运算或解决线程争锁。上游优先级映射值得保留作平台对照，但统一提高 worker 的调度策略没有减少工作量的机制，不据单个营地把它改成生产默认。

首次工具启动检查错误地依赖日志文件初始化之前的选择行，拒绝了该次运行。修正后验证实际 worker 启动日志，拒绝缺失/冲突记录；没有放松为“传了参数就算启用”。启动工具 24 项检查通过。首次 gettid 声明缺失的构建失败、启动拒绝及存档记录均保留。

## 同 PID 的应用标记开销隔离

相同 PID 12400、同 APK、普通 mode 0、QoS 关闭；按 ON/OFF/OFF/ON 四个连续 20 秒窗口。OFF 只删除 Perfetto 的 atrace_apps 选择，保留调度、频率与 Surface 数据；实际检查 ON 有 replay slice、OFF 无该 slice。没有换 APK 或改游戏状态。

| 组 | FPS | 整个 owner Running ms/出图间隔 |
| --- | ---: | ---: |
| ON1 | 40.252 | 20.027 |
| OFF1 | 40.300 | 20.008 |
| OFF2 | 40.355 | 20.096 |
| ON2 | 40.332 | 20.062 |

ON 平均 40.292，OFF 40.327，相差约 0.09%。这排除了当前应用标记是数毫秒损失的解释；不是证明所有 profiler 都零开销。四段 PSO/variants 均不变。没有拿抓帧、截图、Simpleperf 同时运行的结果代替这组窗口。

## 用实际硬件计数区分“多做工作”和“耗时更多”

系统自带 Simpleperf 对实际 owner TID 记录 `instructions:u`，199Hz，`--no-inherit --add-counter cpu-cycles:u --duration 32`，使用默认 MONOTONIC 时钟。实际 MetaInfo 核实 clockid，两个记录均无 lost records/samples。现成 Perfetto `time.conversion` 模块的 `to_monotonic()` 转换完整 Surface 的起止时间，裁到同一窗口。

重要限制：Simpleperf 过滤报告中的附加 counter 会保留过滤之前的累计前缀，不能拿打印数直接除帧数。本次读取官方 ReportLib 完整记录的累计 counter snapshots，在起止边界分别取前后样本，差分得到严格的上下界。只有整理现成工具输出，没有自写计时器、采样器或运行时插桩。

| 每个完整 Surface 间隔 | 当前 QOS-A5-MONO | 上游 UPSTREAM-C3-MONO |
| --- | ---: | ---: |
| FPS | 40.113 | 43.031 |
| 完整间隔数 | 798 | 855 |
| 用户态退休指令，百万 | 72.522–72.576 | 53.217–53.251 |
| 用户态 CPU cycles，百万 | 28.206–28.227 | 30.964–30.983 |

当前约多 19.3M 指令，但少约 2.76M cycles。指令流的 IPC 不同，CPU 自动调频也会变化，不能说“删 36% 指令就会少 36% 时间/多 36% FPS”。本表是整个实际 owner，包含原生游戏执行，不是纯翻译；不含其他线程和 GPU 时间。

此前 BOOTTIME 下的软件/硬件事件组合和 cycles leader 曾返回 EINVAL，错误与完整命令保留。成功方案同时更换为 instructions leader 和默认 MONOTONIC，不把失败归因单独证明为某一个选项。MONO 本地编排脚本仍有旧文件名/提示中的 BOOTTIME 字样；实际命令、MetaInfo 与转换边界才是时间基准证据，不改写历史收条。

早期查询 Simpleperf 帮助时，设备工具曾自动把 `security.perf_harden` 改为 0，首次 QoS 三臂均为此条件。后续明确恢复 1，成功 MONO/后继采集使用各自收条记录，结束恢复 1；不把跨该条件的不同测试拼成一组精确收益。

## 当前最清楚的额外 CPU 工作在哪里

使用相同窗口的官方 instructions leaf 报告，按完整间隔数归一。以下为采样分配估计，不是每个函数独立的精确计数；不累加父子栈。宿主符号匹配确切 ELF。当前约 48.0M 指令/间隔落在宿主库，上游约 29.4M；约 18.5M 差额基本覆盖了整线程指令差。驱动约 5.91 对 6.10M，libc 约 5.51 对 5.80M，未发现驱动 CPU 指令数翻倍。

| 当前 SELF 项目 | 估计百万指令/间隔 | 含义 |
| --- | ---: | --- |
| plan 的 walk | 4.19 | 展开 CALL/JUMP、验证并记录操作及深度/模板状态 |
| mtl_sub_index | 1.90 | 整提交规划入口及编译器归入该符号的扫描工作 |
| vk_shader_cpu_sources | 1.39 | Shader 源资源枚举和 CPU 可见性准备，不能全部当上传 |
| TLS resolver | 1.24 | 动态 TLS 访问；上游约 0.18M，仍须定位具体 owner |

**规划两项合计约 6.09M，是整个 owner 指令预算的约 8.4%，也是约三分之一额外指令。** 当前普通模式 Perfetto 的 planning 约 1.49ms，已有独立 Store 实验曾减少约 0.7ms；两者有包含关系，不能相加为额外可省时间。

下一步应优先识别这段预解释哪些结果可在不可变命令记录/结束时生成，哪些必须在提交时由实际初始状态决定，而不是再加逐 draw 命中检查。改变整段工作的执行次数之前，必须列出查询活性、深度/模板、状态段、Store 前瞻等消费者；已有 NOPLAN 会恢复保守查询/附件成本，不能拿它当等价移除预分析。

也不能简单说“上游不做资源准备”。上游 descriptors SELF 约 4.43M、snapshot 约 1.96M、uniform_snapshot_cached 约 2.30M；当前绑定的整个父区间不是全部多余。原 source-partition 实验已表明分类/失效管理能抵消省下的枚举，不能未经新机制就重复启用。

核对上游 `xbg_game.cpp`：`samples_report_seen` 在此版本主要约束异步 pipeline 跳过资格；实际 draw 的 counting 仍是 `state.count_samples && !no_occlusion_queries && !ab_off("occ")`，随后按连续 run 合并。**不能仅凭注释将其误读为“从未 report 就直接跳过所有 occlusion”，更不能据此删除我们的第一份真实查询语义。**

## Shader 与图像声明排查边界

复用已有官方 GFXReconstruct 捕获，用官方 extract、spirv-dis 与 spirv-cross 反射实际绑定的重 FS；没有为此重复抓帧。主 HDR FS 两边都是 321 draw、53 个静态 image 操作、11 UBO + 2 SSBO，无显式 Float16 类型或 RelaxedPrecision。当前 SPIR-V 约 100.8KB，上游约 144.6KB；静态 load 数、文件大小不代表硬件带宽、寄存器溢出或真实 Shader 耗时。

对应 HDR 附件格式/尺寸一致，当前并非一律带 STORAGE；部分符合条件的附件增加 INPUT_ATTACHMENT，上游有一个 R11 图像带 STORAGE。没有找到“上游重 Shader 默认半精度而我们漏掉”或“当前全部 HDR 被错误 STORAGE 声明”这两个简单解释。导入资源的完整离线回放未资格化，捕获只用来确认实际 API 和二进制结构。

## 收尾与证据

所有已结束测试原生命周期确认存档后停止；没有测试 Mali 或平板。精确普通桌面 APK/profile 的恢复另存 restoration-proof，不把恢复已资格化产物称为新一次完整游戏验收。

证据根目录 `.local/logs/thread-policy-parity-v1/`：`performance-comparison.json`、`tracing-overhead-comparison.json`、`exact-instruction-comparison.json`、两份 MONO 目录及原始 perf.data/Perfetto/SQL、`reduce-monotonic-counters.py`、实际 worker 日志、`shader-audit/actual-HDR-shaders.json`、`attachment-create-audit.json`。失败记录与成功窗口分开保留。

Primary / Secondary 已完成两版实现、结构核对及重测：第二版 40.132→40.222 FPS，后台 reset 0.186–0.320→0.0365–0.0373ms/次；剩余一个额外 HDR Pass，故不默认替代现有模式。详见 [完整实验](ANDROID_PRIMARY_SECONDARY_EXPERIMENT_2026-10-27.zh-CN.md)。这些结果排除了几种“大块隐蔽等待”的解释，没有宣称约 40 对 43 FPS 的余差已经全部归因。
