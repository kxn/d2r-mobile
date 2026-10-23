# 翻译线程执行速率与呈现等待的同窗诊断

当前尚未达到高通 60FPS，也没有证明整线程的约 5800 万条指令可以大块删除。本轮首次实际控制翻译线程所在核心，区分必要工作量与执行这些工作的速率。两个固定大核窗口都减少了 CPU Running，但 Surface 出图间隔没有相应改善；不能继续把约 26ms 当成不随执行环境变化的算法成本，也不能把更快的 CPU 当成已经涨帧。

## 实际代码及资源

诊断源 `76886568b8c75584b1321e73930e2b590d10c451`，独立分支 `codex/nvn-owner-affinity-v1`；Android canonical APK SHA `1262e1cea00e39c9552712764a1f5efeb33aa969d32fa42172b6c48a8f34b226`，host BuildID `0c263474f214b37be09308df0f089d7c6843f37d`。根独立核验 1035 项源码、六个实际打包库、原始 NSO fresh rewrite 的 28 项输出及全部构建证据；24 项执行／规则文件与原安装相同，保留原安装的完整 provenance，未叠改 binary。构建树的额外 `work` 仅为原资源路径软链接，tracked 源码干净且全部输入哈希匹配。

原 f830 驱动、原 950 shader 链、6217 项 native 表和原 CASC 不变。固定 .60、2750×1260、target60、1MiB shared lazy、span reference、512MiB private image budget、freeze/extrap OFF。原 ARM64 模块仍由硬件执行，原装载／宿主门／HLE／NVN→Vulkan 不变，没有 CPU 模拟逻辑。

开关仅在原 `nvn-translate` 启动、接收首个任务前执行 self-affinity；CPU 必须属于原 allowed mask，失败即拒绝该臂。默认 -1 不做 affinity syscall，也不在 draw 增加判断。既有 run-as/taskset 对别的 TID 操作被拒，实际 self API 补足的是这个权限域缺口；测量仍全部使用原系统 Simpleperf 和 Perfetto，不新增计时器或采样器。不改频率、优先级、uclamp、温控或其它线程。

## 六臂实际结果

顺序：默认 → CPU5 → CPU7 → CPU7 → CPU5 → 默认。每臂原 Simpleperf group read 10 秒、重叠 Perfetto 8 秒；原 ReportLib/dump 与 TraceProcessor 交叉核验，保留 full/observable 两种窗口。百分比驻核来自实际 sched 数据，不是启动日志的 mask。

|臂|实际驻核|指令 M / Surface|用户周期 M / Surface|Running ms / Surface|Surface 均值 ms|加权 MHz|
|---|---|---:|---:|---:|---:|---:|
|A1 默认|主要 CPU3–6|57.617–58.043*|33.951–34.216*|26.808|33.133|1441.88|
|P5A|CPU5 100%|57.872–57.957|32.743–32.794|26.694|33.130|1381.97|
|P7A|CPU7 100%|58.186–58.270|19.763–19.791|20.839|32.995|932.24|
|P7B|CPU7 100%|58.347–58.432|19.527–19.553|21.223|32.989|912.84|
|P5B|CPU5 100%|58.170–58.255|34.141–34.190|27.283|33.171|1416.23|
|A2 默认|主要 CPU3–6|57.881–58.305|33.831–34.080|26.599|33.077|1441.44|

*A1 CPU0 的 0.198907ms Running 没有齐计数边界，覆盖 99.99684268%；标记范围仅覆盖有边界的核，不是完整 TID 上界。其它五臂 PMU 覆盖 100%。计数范围是窗口边界不确定性，不是统计置信区间。

全部 PSO 174、target60；A1 variants30→32，P5A/P7B 为32→32，其余为30→30。相同 variant 数不证明每条绘制完全相同；实际 View 等消费者数也有差别，没有 MHz 归一或挑选有利窗口。CPU7 相对于 CPU5 的约 40% 用户周期减少是描述性对照，不是减少了指令，也不能全部归因到缓存／迁移／某一种微架构特性。固定 CPU5 相对默认没有可重复的明显 Running 改善。

原 v1 首次资格要求 variants≥32，实际稳定30未达，在正式工具启动前退出；原记录保留且存档两次。v2 六臂统一改为现有稳定观察加最低30，记录真实前后变化，不重装或修改场景。A2 正式工具成功，原存档同 PID commits=2；force-stop 后的即时校验短暂仍看见 PID，collector 终态1。原 finally 和根两次后续独立 pidof 均确认退出。原 A2 失败不改，派生分析视图仅补实际 lifecycle 收条并保留原错误与 terminal1，未重采。它是补证的收尾，不宣称干净 collector 成功。

完整结果与 200 项原输入核验：`.local/logs/nvn-owner-affinity-device-v2/six-arm-offline-review.{md,json}`、`six-arm-offline-manifest.json`。派生恢复边界见 `A2-stop-recovery.json`、`A2-qualified/`。

## CPU 缩短的时间去了哪里

对 A1/P5A/P7A 原 Surface 窗，用官方 sched/thread_state 与已有 ATrace 职责交叉查询；规划、submit 等嵌套范围按最内职责切分，互斥 Running 合计与原 facts 精确相等。不能将嵌套 span wall 相加当 CPU 时间。

P5A → P7A：总 Running 26.694→20.839ms，少5.855ms；总 Sleeping 增5.585ms。`present` 内 Sleeping 6.033→11.606ms，Runnable .129→.451ms。Surface 只从33.130→32.995ms。回放本体 Running 减少，但规划并非同比变快，不能推断全部阶段都有同样收益。P7A thread_state 尚缺 .036916ms/Surface 的覆盖，单列未知，不补成睡眠或运行。

三个窗 owner 都没有 `Vulkan CPU waits queue completion` 标记；swapchain acquire 很短，主要为 Running。游戏线程的 guest GPU fence 是另一个线程，不能移入 owner 账。`present` 内新增睡眠说明省出的 CPU 时间被后续等待吸收，**不单独证明 GPU 纯执行时间、30FPS 限制或具体 WSI 机制**。

阶段原查询／输入哈希及闭合账：`.local/logs/nvn-owner-affinity-phase-v1/`。

进一步使用同一 trace 的 SurfaceView Buffer 事件，按完整 layer、frame_number、buffer track 连接 Queue／AcquireFenceSignaled／Latch／PresentFenceSignaled。A1/P5A/P7A 分别有234/234/236个完整帧，缺边缘事件单列，不把派生的旧buffer参数当新帧。P5A→P7A 的 Queue→ready 均值38.929→44.287ms，ready→present 7.577→7.646ms，额外延迟主要在ready之前；现有数据不支持“已完成帧更多地堵在显示端”。这些帧区间相互重叠，且含队列及先决依赖，不能相加或称GPU纯执行时间。SF自己的GPU Deadline Missed也不能当游戏某个pass的热点。

旧工具结论已核查更新：实际f830＋950的原游戏曾成功使用官方 Snapdragon Profiler Realtime，入口可复用；旧负载为6217项ASTC4，而当前包含2838项ASTC6替换，旧计数不是当前结果。下一步需在当前已验证资源上补足GPU工作画像，用现成工具区分带宽／纹理／shader压力，不能只靠上述等待命名猜测。原成功证据与复用方案在 `.local/logs/nvn-owner-affinity-phase-v1/gpu-evidence-gap/`。

## 改变的研究顺序

1. 不再仅凭独立样本份额乘约26ms，预报缓存候选的毫秒；必须同时看实际总指令、周期、Running、执行核心和呈现阶段。当前已经证明执行率会明显影响这个数。
2. 当前单核大核的 Running 仍大于16.67ms；同时 CPU 缩短并未涨帧。因此需要区分剩余 CPU 成本和呈现的实际阻挡条件，不能只堆末端缓存或直接启用绑核。
3. 对当前 CPU7 执行状态追加原 public ADPF 的 OFF/ON/ON/OFF 敏感性诊断，问题仅是已有接口能否改变这一个新状态。旧80a未绑核的实际负结果保留，不能把重复开启 ADPF 当新优化；不改接口／时钟／温控／帧回调。实际结果如下，没有认领新的提速。
4. 保留有明确省工机制的既有优化。纯报告统计的新只读审查仅识别0.18–0.50%用户态样本，不是完整上界，但没有显示值得转成大主线的成本；原trace注释观察成本0.46–1.55%另列。见 `.local/logs/current-report-only-stat-cost-v1/`。

本诊断不默认改变日常选核，不提供持续热量／能耗／战斗全场景保证，也没有达到60FPS。Mali 和借出的平板未访问。测试结束须恢复原日常 APK 并核验原驱动／资源／模块、进程退出；恢复证据以设备实际收条为准。

## 当前 CPU7 与 public ADPF 的四臂结果

四臂均为同一诊断包，official sched 实际100% CPU7、PMU计数完整覆盖。两 ON 臂都建立真实会话、TID匹配翻译owner、target16666665ns、reports随每个guest submit增加一次、failures为0。接口被接受与执行改善必须分开。

|臂|Running ms / Surface|Surface 均值 ms|指令 M / Surface|用户周期 M / Surface|加权 MHz|
|---|---:|---:|---:|---:|---:|
|OFF1|21.249|33.034|58.517–58.602|19.949–19.977|932.59|
|ON1|21.275|32.940|58.354–58.439|19.791–19.818|932.17|
|ON2|21.156|33.035|57.881–57.966|19.641–19.668|935.85|
|OFF2|22.461|43.477|58.588–58.701|20.124–20.188|930.37|

首对 Running 几乎相同；第二对 OFF2 存在不同的等待节奏：177个 queue-completion wait 相交标记、clipped wall3737.395ms，present call的wall仅111.573ms；其它三臂没有该queue wait、present wall约2884–2917ms。不同线程／嵌套span不能直接相加。这一慢窗完整保留，不能平均两对宣称 ADPF 改善或造成回归。ON2与OFF2均variants30→32；首对30→30，PSO均174、target60。

本次没有把执行带到16.67ms，也不能推广为 ADPF 对任何设备／场景都无效。不再把相同接口开关当下一项省工优化。原记录／125项输入核验在 `.local/logs/nvn-owner-affinity-adpf-v1/four-arm-offline-review.{md,json}`；根另核实际TID／target／计数覆盖、保存退出与原输入。

十个正式窗口和一次正式工具前的预热拒绝均已完成原生命周期存档两次并退出（A2即时stop校验race的补证仍单列）。原日常 APK `b8f4020f…` 已实际恢复，原f830／模块28项／native表及原CASC逐SHA复核，进程缺席。实际恢复收条 `.local/logs/nvn-owner-affinity-device-v1/restore-result.json`，不是仅准备的恢复计划。诊断源码分支已推至用户fork备份，主线默认未启用绑核。

## 当前日常资源上的官方 GPU 实测

恢复原日常包之后，重建唯一属于本次的官方 SDP2026.9 Realtime GUI，Q-only ADB wrapper 排除借出的其它设备；实际暂停控件、九指标及当次 GUI／13个collector身份核验后，原 helper 执行12秒Realtime及其中8秒官方Perfetto。游戏仍为b8f/d34、原f830/950/6217混合ASTC4＋6/.60，无绑核、hint reference，不更改驱动或游戏产物。预热到tick1020后，正式窗PSO174／variants32前后不变。

官方窗8.005945s，游戏同一SurfaceView的239个PresentFenceSignaled形成238个完整间隔；完整窗7.850245s，平均32.984222ms。九指标每项157个正式样本，最大gap59.903ms，原字段定义／global／有限值／覆盖均通过。使用实际REALTIME↔BOOTTIME clock_snapshot和首末Surface fence裁剪分子与分母，没有12秒流量除8秒帧。

|官方全局 GPU 指标|正式窗时间加权均值|完整 Surface 窗每间隔|
|---|---:|---:|
|GPU clocks|499.986 M/s|16.492 M reported clocks|
|Shaders Busy|66.522%|66.513%|
|Texture Pipes Busy|41.261%|41.260%|
|L1 hit / Texture L2 miss|64.501% / 8.616%|64.510% / 8.624%|
|总读|4.011 GB/s|132.347 MB|
|其中纹理读|3.016 GB/s|99.531 MB|
|总写|1.865 GB/s|61.564 MB|
|Fragments Shaded|1.014 G/s|33.484 M|

GB/MB为十进制。纹理读是总读的子集，不相加；busy指标有不同且重叠的定义，不相加或换算游戏GPU毫秒。全局包括compositor，fragments不是唯一屏幕像素数，不能称overdraw倍数。没有实际带宽峰值／stall证据，不能把4.011GB/s写成带宽饱和或纯ALU瓶颈。

同完整Surface窗的翻译线程Running25.014ms/interval、1405.835MHz，CPU频率覆盖100%；仍大于16.67ms，不能说只剩GPU问题。原replay/planning/present调用分母和嵌套关系另存，不与这个整线程数相加。SurfaceFlinger的213条GPU Deadline Missed属于合成器，不是游戏独占GPU pass时间。旧全ASTC4只证明工具可复用，不作资源优化因果对照。

实际PID15843原生命周期save2、游戏退出；根独立核GUI/Xvfb/13个collector均缺席、层设置不变、原日常APK与32项资源身份不变。采集在 `.local/logs/current-execution-rate-sdp-game-v1/`；原工具分析及46项输入／输出哈希在 `.local/logs/current-execution-rate-sdp-game-v1-analysis/`。

下一工具缺口是GPU队列／renderstage的实际时间，而不是重复猜测逐draw成本。原f830构建关闭Mesa Perfetto；已另开独立构建，仅启用Mesa现成Perfetto SDK／官方`gpu.renderstages` producer，先核KGSL已有timestamp支持与工具本身的捕获扰动。它不是自制计时器，当前未换日常驱动、未改资源身份、未通过GPU资格；编译完成也不能冒充已取得队列Profile。CPU侧同时完成Buffer阶段聚合资格审查：范围账本与保活已有复用，尚不能把9%父桶视为可删；有限几何预解析须先补实际Pool／别名／发布边界资格，不立即增加另一层检查。见 `.local/logs/current-buffer-phase-aggregation-v1/`。
