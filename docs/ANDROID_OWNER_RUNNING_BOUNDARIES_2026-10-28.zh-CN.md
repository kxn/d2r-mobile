# 当前 owner CPU Running 的高层边界（2026-10-28）

当前工具证据能把 owner 的 18.235013 ms/Surface 分成三块：NVN 重放
11.252623 ms、其他已标记根区间 0.737943 ms、尚无调用栈归属的
6.244447 ms。最后一块不能直接称为游戏逻辑或 NVN 录制；其中
5.927996 ms 位于 `NVN SyncWait` 结束至下一次重放开始之间，包含
尚未被重放标记覆盖的 Vulkan 提交前准备。

## 数据、窗口与身份

- 原始目录：`.local/logs/native-bc3-three-arm-game-v1/OFFCPU-G1/`。
- 官方 Perfetto：`perfetto/trace.pftrace`，SHA256
  `8630af71597234b90473811fd4a3f3f0714149ec084fad935741c4db919e636a`。
- 实际 PID 29199、owner TID 29256；来自成功 CPU mask=80 日志及同进程
  Tgid 核验，不按最忙线程推测。
- APK `992c6f159660c67aced5532b4e7727e946183bcd5e53acd4dae241dd39025735`，
  host ELF `7a37a24221e269c36c640c7bef56fd6b0ad0bbb9c66ec13317a3777860543497`，
  0e53 BC3-linear 驱动。此窗为带 ATrace 的诊断，receipt 明确禁止作为普通 FPS 基线。
- 沿既有官方 SQL，以 tracing-service 实际起止之内的完整
  `PresentFenceSignaled` 裁剪：BOOTTIME
  `[257994614417881, 258002513325743)`，342 个 Surface 间隔。
  间隔均值 23.096222 ms，p95 33.234472 ms。
- owner 所有 Running 均在 CPU7；频率计数覆盖 6,217.793171 /
  6,236.374524 ms Running，覆盖部分加权均值 1,591.401665 MHz。
  不是锁频，也不能用此平均数把用户态 cycles 换算成内核时间。

## 互斥预算

只把同线程 `parent_id IS NULL` 的根 slice 与官方 `sched_slice` Running
求交，再统一裁剪到上述窗口。根区间交叠检查为0；没有未闭合 owner slice。
子 slice 不另加到总量，未覆盖部分保留为 unknown。

| 互斥归属 | Running ms/Surface | 说明 |
|---|---:|---|
| NVN command replay | 11.252623 | 339 次与窗口相交的根重放 |
| Vulkan present call | 0.456007 | 包含其内部 queueBuffer 等 CPU 工作 |
| Vulkan queue submit | 0.177949 | 两类提交合计；不等于 GPU 执行时间 |
| Vulkan swapchain acquire | 0.089025 | CPU Running，未把睡眠算进去 |
| Vulkan presentation feedback | 0.007064 | 已含内层查询 |
| NVN SyncWait | 0.003212 | 包含 result 子标记 |
| submit queue lock | 0.001985 | 仅此已标记锁区间的 CPU Running |
| present queue lock | 0.000861 | 同上，不能代表所有锁 |
| QueueSubmit / FenceSync 瞬时标记 | 0.001841 | 仅标记开销，不覆盖整个 API |
| 未标记 Running | 6.244447 | 不猜测其调用栈 |
| **owner 总计** | **18.235013** | 其余已标记根合计 0.737943 |

`NVN submission planning` 的 0.937896 ms 已包含在重放中；更深的 attachment
store index finalization 又是 planning 的子区间，不能重复相加。
原 `owner-running-spans.csv` / first-look 的约11.35 ms来自全 trace 的重放求交，
没有统一按 Surface 窗口裁剪；不能把它与此表的18.235013直接相减。
本窗339次重放和342个 Surface 间隔也不是同一个分母。

## 未标记工作集中在哪里

按相邻根区间之间的空隙再次与同一个 Running 集合求交：

| 前后实际边界 | Running ms/Surface |
|---|---:|
| NVN SyncWait 结束 → 下一 NVN command replay 开始 | **5.927996** |
| replay 结束 → Vulkan submit queue lock 开始 | 0.226930 |
| presentation feedback 结束 → SyncWait 开始 | 0.029375 |
| acquire 结束 → submit queue lock 开始 | 0.023723 |
| 其余空隙及窗口尾端合计 | 0.036423 |

第一个区间占未标记 Running 的94.93%。其 thread_state 互斥拆分为
Running 5.927996、I 0.506155、S 0.364836、Runnable R 0.226660 ms/Surface；
它不只是一个睡眠等待区间，仍不能由 Running 自动推出没有自旋或锁成本。

实际源码边界：`dm/gfx/nvn/hle_nvn.c:x_submit()` 先执行
`mtl_host_apply()`、`mtl_sub_guest_begin()`，随后 `mtl_sub_index()` 才由
`dm/gfx/vulkan/nvn_vk_plan.c` 调用 `vk_stream_begin()` 开启 replay slice。
因此这个5.927996 ms还可能包含：原游戏/HLE/录制入口、提交交接，及
`mtl_sub_guest_begin()` 内的 `vk_texture_budget_maintain()`、
`begin_submission()`（pool-window 准备、prewarm、回收及录制资源准备）。
这三个相关源码文件与992 carrier冻结源码的SHA逐字节一致。

## 优先级和可省上界

1. **先细分 SyncWait→replay 的5.928 ms。** 它是当前最大未解释职责区间，
   不是可全部删除的预算；需要实际调用栈才能知道其中录制、HLE、游戏与提交前准备各占多少。
2. **重放仍是最大的已知工作块。** 11.253 ms不能当作全部资源绑定；其中
   planning 只有0.938 ms。新的采样应先按实际 ATrace 大阶段分桶，再看完整祖先，
   禁止把一个叶函数的总SELF预算全算给某个小调用入口。
3. replay→submit 锁之前的准备当前只有0.227 ms；已标记两个队列锁CPU合计
   0.002846 ms。这些边界不能解释整个6.244 ms缺口。锁内阻塞另看调度状态，
   不把wall和Running相加。

当前没有证明这5.928 ms中哪一部分可以安全省去，也没有新优化的毫秒收益结论。
追踪、源码候选与已证实的可删除工作必须分开。

## 现成工具的实际缺口及下一步

本 trace 的 `perf_sample`、`cpu_profile_stack_sample` 都是0；ftrace 中没有
syscall 用户栈。ATrace能确定上述边界，不能把未标记区间再细分为函数职责。
原 `run-as ... simpleperf ... --trace-offcpu` 在采样前失败，明确报
`sched:sched_switch` 不可用；这不是普通 on-CPU 采样失败的证据。

最小补充是继续用已多轮跑通的系统 Simpleperf、原 run-as 授权和 DWARF：
保留 `--in-app --no-inherit -t <已核owner> -e cpu-clock:u -f 199`
及 `--call-graph dwarf,32768 --clockid monotonic --duration 16`，去掉
`--trace-offcpu`，与原官方 Perfetto 同采。只取完整同窗 Surface 对应的
MONOTONIC 样本，并复用官方 ReportLib、精确ELF/BuildID和现有祖先归约。
cpu-clock 的周期单位是ns估计，不能冒充 instructions、精确函数计时或全部内核Running。
没有完整祖先的样本继续单列，不填成guest或录制。

如果另需阻塞调用栈，应先由现成工具在 shell UID下核
`/system/bin/simpleperf list --show-features` 是否确实支持 trace-offcpu，再考虑
官方 `record --app <包名> -t <owner> --trace-offcpu` 路径；debuggable/profileable
和 tracepoint 权限是不同条件，不能保证换shell一定可用，也不为此反复设备重试。
本次普通on-CPU窗口由独占设备的代理执行，本文不操作设备。

复现SQL、完整CSV、工具错误日志位于
`OFFCPU-G1/perfetto/owner-duty-audit/{duties.sql,duties.csv,tool.stderr}`。
没有新增运行时计时器、采样器、游戏逻辑或生产图形修改。

## 普通 on-CPU 样本：6 ms里实际是什么

同一992 APK、7a37 host、0e53驱动的后继 `ONCPU-G1` 已完成官方普通
Simpleperf `cpu-clock:u` /199Hz/DWARF32768采集；没有再请求offcpu。
原数据 `ONCPU-G1/perfetto/oncpu.data` SHA256
`5fde250bcc2ef680bb267c8da1b477527edf9ab14ca60dd07a7269dfd40a3de5`。
全记录1,798样本、0丢样；裁剪后为1,186个owner样本、341个完整Surface间隔。
实际 PID32711、TID32763。SDP全程暂停，采集结束已存档退出。

本窗需单独看，不能将样本分配套到上一窗的精确Running：

| 同窗官方调度结果 | ms/Surface |
|---|---:|
| owner总Running | 18.570387 |
| 重放根（含planning） | 11.469836 |
| 其余已标记根 | 0.753553 |
| 未标记Running | 6.346998 |

本窗owner全部在CPU7；Running频率计数覆盖100%，加权均值
1,595.105613 MHz。Surface间隔均值23.129429 ms，p95 33.232396 ms。
工具开启的诊断窗不能作为普通FPS收益对照。

以下为按实际ATrace阶段、实际DWARF祖先和官方采样周期分配的**互斥估计**，
单位是cpu-clock采样周期ns归一后的ms/Surface，不是精确函数计时、硬件指令
或全部kernel Running。inclusive祖先表另存，绝不与此表相加。

| 职责 | 估计ms/Surface | 样本数 |
|---|---:|---:|
| 重放，排除planning子区间 | 10.551289 | 716 |
| planning | 0.854713 | 58 |
| 实际guest main PC，缺少调用栈 | 3.330435 | 226 |
| 实际guest sdk PC，缺少调用栈 | 0.972605 | 66 |
| HLE线程服务/等待控制 | 0.559985 | 38 |
| 其他host或尚未归属 | 0.515775 | 35 |
| 重放标记之外的其他public NVN | 0.235783 | 16 |
| 有真实祖先的NVN录制 | 0.162101 | 11 |
| present | 0.132628 | 9 |
| 重放之前的提交准备 | 0.103155 | 7 |
| acquire | 0.044209 | 3 |
| queue submit | 0.014736 | 1 |

main/sdk没有符号或祖先，原工具显示`unknown`。这里没有把它们猜成guest：
使用本PID实际启动日志的载入区间，逐条核对官方样本原始PC。main为
`[0x7619e00000, 0x761d388000)`，sdk为
`[0x761ea00000, 0x761f94c000)`；292条无祖先PC全部落在上述两个范围。
日志身份来自capture中已核的owner-native日志和同PID启动行。
**这只证明模块归属，仍不知道游戏中的函数、功能或可省部分。**

host和驱动符号分别以实际BuildID
`7f6a8f0d3547e38728d9ad28b7aa3887b3eda29b`、
`2d9845ab18aa63e42598859a61bea6b40f1e67b9`匹配；没有用NDK ABI stub或
其他驱动替代缺失符号。上述约0.103 ms的pre-replay样本都具有
`h_nvnQueueSubmitCommands→x_run→begin_submission→reclaim_submissions`
祖先，涉及shader/graphics回收；只有7样本，不能认定其精确耗时或把它放大成6 ms。
HLE约0.56 ms中34样本落在`block_inner→now_ns→clock_gettime`路径，
这与等待策略的CPU成本有关，不能简单视为可删除工作：缩短自旋也可能增加交接延迟。
NVN录制仅11条有明确祖先的样本，不能据此证明全部录制只占这个数，
也没有证据继续把整个未标记区间当成资源准备。

当前最有价值的新判断是：未标记区间的大部分采样PC确实在原生游戏main/sdk，
并不是此前怀疑的提交前资源准备大热点。应将原游戏执行、HLE等待控制、NVN录制、
重放分开预算；不为缩短一个被误称“翻译线程”的总数去修改游戏业务。
重放仍是最大的可辨认后端CPU工作，下一步应依实际祖先再分其内部成本。
目前只证明组成，没有新增优化收益或整个4.303 ms guest执行可被删除的结论。

同一批样本里，重放的几个实际祖先包络为：draw_record约7.530 ms、
binding准备约2.579 ms（内部texture_view约1.017 ms）、CPU source preflight
约0.811 ms、state segment约0.589 ms。它们互相嵌套，**不能相加**。
例如binding SELF仅约0.678 ms，draw_record SELF约0.884 ms；不能把整个
draw包络都当成其SELF，或把binding的2.579 ms都当作某个叶端缓存能省掉的预算。
这些数支持先看完整资源消费流程，再定位其重复工作；没有证据支持一个
未处理的单项即可删去整帧6 ms。

离线入口：`ONCPU-G1/analysis-agent/reduce.py`，调用官方Trace Processor和
Simpleperf report/ReportLib，产物包括`phase-duty-budget.json`、
`symbol-source.json`、`official-self.csv`、`official-ancestors.txt`、实际样本及原SQL。
phase内按最深实际slice归属；丢失祖先继续单列。采样量不足的个位数项目仅作方向，
没有用采样周期总量与Running的差额反推精确内核时间。

## KGSL序列：present睡眠是否代表GPU断粮

另用OFFCPU-G1的既有KGSL事件、原CPU根标记做全trace序列检查，独立于上面的
Surface归一CPU预算，不能将这两种窗口的总量相加。原retired事件1,148条，
按同事件内容去重后692条；按event/context/timestamp没有多版本冲突。
目标context35有686条submitted，另一个context34只有6条。通过实际CPU根区间
定位338条main、321条present-copy，其余33条保留未匹配/其他，不按长短猜用途。

335段present睡眠合计1,220.397 ms。每一段开始时，context35都有2–4条已经
submitted但尚未观测retire的工作，325段为4条；其中1,200.895 ms属于睡眠期间
出现retire事件的段。后者不是GPU忙碌时间，只说明睡眠与完成上报发生在同一段。
341个可比replay中，337个开始早于前一main的retire上报，326个连结束也早于它。
这些CPU事件顺序支持队列背压/有未退休工作，不能证明GPU每一刻都在执行。

同context连续683对工作，原始GPU start与上一retire之差中位数57，663对小于100，
最大73,827；已匹配main active均值约438,735，copy约3,692，全部保留原始单位。
没有按19.2MHz换成ms，没有把submit→retire的墙钟驻留时间称为执行时间。
上述原数检查先于后续时基资格；尤其retire上报可能晚于硬件完成。
因此目前不支持“CPU睡眠约3.5ms，所以GPU也空转约3.5ms”的推断，
也不据此宣布已经排除全部GPU空泡。

完整官方SQL、去重序列、配对、原始值和边界声明在
`OFFCPU-G1/perfetto/kgsl-sequence-audit/`。没有新采集或测量逻辑。

## KGSL时基确认后：GPU批次几乎连续占据，CPU睡眠主要重叠其间

已核的公开源码为Google托管的Qualcomm KGSL graphics树，固定commit
`df85c99c4978e19c47a740ff616bad125a8756e2`，由tag
`android-wear-14.0.0_r0.6`解析。它不是PD2528当前内核的完整源码；没有将
另一设备的发布包冒充本机固件。以下源码定义再由本机原始trace独立校准。

- [`adreno_trace.h`](https://android.googlesource.com/kernel/msm-modules/graphics/+/df85c99c4978e19c47a740ff616bad125a8756e2/adreno_trace.h#258)
  将retire事件的start/retire直接赋为SOP/EOP；
  [`adreno_gen7_hwsched_hfi.c`](https://android.googlesource.com/kernel/msm-modules/graphics/+/df85c99c4978e19c47a740ff616bad125a8756e2/adreno_gen7_hwsched_hfi.c#191)
  从GMU retire消息取这些字段，active按HFI版本取EOP−SOP或GMU提供值。
- [`kgsl.c`](https://android.googlesource.com/kernel/msm-modules/graphics/+/df85c99c4978e19c47a740ff616bad125a8756e2/kgsl.c#1043)
  明确active单位为19.2MHz XO周期，并以`active*10000/192`转为ns再发work-period。
  这不是GPU shader核心频率，不能拿SDP核心clock替代。
- [Gen7提交时间配对入口](https://android.googlesource.com/kernel/msm-modules/graphics/+/df85c99c4978e19c47a740ff616bad125a8756e2/adreno_gen7_hwsched_hfi.c#2760)
  在关中断区间读取always-on计数、发sync trace、取local_clock，随后将
  local_clock的秒/微秒写入submitted事件，目的正是映射GPU和CPU时域。

本机692组ticks与事件自带local_clock秒/微秒，拟合为
**19,199,999.944 Hz**，相对19.2MHz偏差−0.002904 ppm，拟合残差绝对中位
0.652 μs、最大3.283 μs。用sync的官方BOOTTIME时间作19.2MHz映射，残差
绝对中位0.678 μs、最大2.708 μs。local_clock与Perfetto转换后的MONOTONIC
有约2.609129秒的稳定偏移，不能直接混钟使用；区间使用sync的BOOTTIME锚点。
本机active与EOP−SOP差值仅±1tick，支持这些字段采用同一时基。

由686条目标context35的实际retired SOP/EOP，求硬件区间并集，再与Surface
窗口相交；只在有硬件边界证据的共同内段计算覆盖，不将首尾未知填成idle：

| 指标 | 结果 |
|---|---:|
| 共同可观察内段 | 7,863.726 ms |
| SOP/EOP区间并集 | 7,835.959 ms |
| 并集覆盖率 | **99.6469%** |
| 内段区间之间总空隙 | 27.767 ms |
| 相邻空隙中位数 / 最大 | 2.969 μs / 3.845 ms |
| Surface窗口前沿无硬件区间证据 | 35.182 ms，保留unknown |
| Surface窗口后沿无证据 | 0 ms |

独立与335段CPU present睡眠求交：共1,220.397 ms睡眠，其中1,220.052 ms
在硬件可观察范围内，**1,216.617 ms与SOP/EOP区间重叠，占可观察睡眠的
99.7185%**。另0.345 ms在硬件证据边界外，保留unknown。
时域锚点的上述微秒残差仍适用，不声称逐纳秒因果精度。

按原CPU标记定位的两类批次，其active分布由官方Trace Processor求得：

| 批次 | 已退休样本 | 均值ms | p50ms | p95ms | 最大ms |
|---|---:|---:|---:|---:|---:|
| main（replay后） | 337 | 22.850761 | 22.877604 | 22.963479 | 23.035885 |
| present-copy（acquire后） | 320 | 0.192296 | 0.191849 | 0.204484 | 0.221979 |

未匹配的27条context35批次仍不猜用途；前表序列总计33条未匹配/其他还包含
6条其他context。没有按奇偶编号或时长强行将它们塞进主组/复制组。

这改变了CPU等待的性能判断：**本窗口不支持“约3.5ms CPU等待等于可省
3.5ms帧时间”**，CPU睡眠几乎都与GPU批次占据区间重叠，且主批次平均
22.85ms与约23.10ms呈现间隔接近。现有证据更支持GPU侧工作决定当前稳态吞吐，
CPU少量减负或搬走present等待未必立即涨帧。
99.65%是SOP/EOP批次占据覆盖，**不是ALU busy百分比**，内部仍可能包括
纹理/带宽/同步stall，也不能单独确诊带宽bound；CPU准备、峰值卡顿和延迟仍应优化。
少数最大3.845ms间隙也不能被均值覆盖率抹去。

来源与完整性：`kgsl-sequence-audit/source/frozen-source-ledger.json`保存固定commit、
各下载源码SHA及失败入口；`clock-and-union-summary.json`保存映射、残差、
覆盖/unknown边界；`clocks.sql/csv`及`sequence.sql/csv`保留官方查询和原字段。
现trace的gpu_slice和gpu_work_period事件都为0，不能冒称已有现成GPU占用轨道；
这里仅整理原驱动已经记录的计数/事件，不新增采样器、运行时计时或采集。

## 转入GPU工作：现有热点证据能复用到哪一步

当前共享BC3-linear/0e53/29e56的原厂数据已经具备宏观负载，入口是
`.local/logs/native-bc3-three-arm-game-v1/gpu-tools/comparison-all-frozen.json`
中的`PARITY-LINEAR-G3`及原SDP数据库/receipt/九项原样本。
342个完整Surface、43.3009FPS；每Surface总读105.260 MB、总写44.141 MB，
片元指令约1.8821G（原指标为full-precision scalar等价ALU，含插值/EFU，不含纹理fetch）。
Shaders Busy约74.14%、Shaders Stalled15.71%、Texture Fetch Stall10.34%、
System Memory Stall0.331%、Wave Context Occupancy53.23%。所有这些都是
**全GPU计数，包含系统合成/UI**，各百分比可重叠，不能相加或乘23ms制造某pass
独占stall时间。它们支持关注shader/纹理消费，不足以单独宣布DRAM带宽饱和。

现有最接近当前配置的按pass实测入口是
`.local/logs/remaining-upstream-gap-v1/MESA-PASS2-{mesa-gpu-trace.csv,mesa-summary.json,receipt.json,launch.json}`。
其29e56、1968×900/.85、shared-lazy、prefer_sysmem、pixel-local、quarter-bloom
与当前方向一致；但是它使用旧B3/ASTC包、旧ef3e1f6宿主，不能将旧毫秒数直接套给当前BC3。
300个完整49-pass主CB（排除尾16）的原工具结果如下，仅用于候选排序：

| 旧实际组 | 旧工具ms/主CB | 当前用途 |
|---|---:|---|
| 1673×765、双附件、641draw HDR | 7.782 | 第一热点候选 |
| 1024×1024、单附件、107draw | 2.877 | 需结合旧API附件确认用途，不能仅凭尺寸命名 |
| 1673×765、单附件、641draw | 2.366 | 第二类全尺寸几何/深度候选 |
| 1673×765、双附件、每pass1draw，三次 | 1.094合计 | 后处理/搬运候选，需按事件边界核身份 |
| 1673×765、三附件、29draw | 1.087 | 另一全尺寸候选 |

旧主CB合计25.085ms；不是普通运行22.851ms的新测量，也不是纯shader时间。
pass边界会包含同步和移动GPU执行的归属，默认per-draw timestamp关闭，
不能重新从单draw时间判定被遮挡消除收益。

旧汇编也有两种资格不同的来源：

- 本实现旧GFXR的HDR module318为`e1027540…`，已有原source-BLAKE3关联到
  六个实际IR3块，约1967–2133条静态指令、19full registers、10max waves。
  入口：`remaining-upstream-gap-v1/HDR-source-hash-association.json`。
  这证明旧模块关联，不能证明当前所有PSO特化仍逐字相同，更不提供shader耗时。
- `q735-system-native-ir-v1/analysis-agent/matched-pso-native-audit.json`的12对严格
  配对是**上游后端**PSO/输入，目标FS SHA为`4fe6bf20…`、`6011038c…`，
  与上述本实现HDR输入不同。不能把其20/21条descriptor prefetch或native指令数
  直接算作当前后端预算；System内部表示返回0，不能声称拿到System汇编。

因此现有数据足以选择“主HDR、相邻几何/深度、全屏处理”作为待核的几组，
**仍不足以按当前BC3精确排序其毫秒或将22.85ms分配到这些组**。
最小真实缺口是一次当前同ELF/BC3/29e56的原Mesa pass/CB/IB诊断，保留完整
CB/实际renderpass和附件格式/尺寸/模式；不用再证明旧模式实验或重跑所有正确性。
若后续确需shader级优化，再对当前重组取得实际PSO、SPIR-V和variant身份，
检查能否与已存IR3逐字匹配，匹配后才复用旧静态结论。

现成入口`--mesa-gpu-trace`只启用原Mesa u_trace wrapper，per-draw关闭，
不需要新计时器。原`reduce-mesa-csv.py`可复用其配对/分组思路，但其固定49pass、
最后300batch不适合作为新窗的硬门；batch编号也不能当Surface帧号。
工具窗FPS与普通基线必须分开。

还核了实际0e源码的时域细节：`tu_trace_read_ts()`把GPU query交给
`tu_device_ticks_to_ns()`，后者使用整数`1000000000/19200000`，即52ns/tick；
这相对精确XO换算少约0.16%。`u_trace.c`原样打印GPU timestamp，不提供BOOTTIME。
KGSL提交配对是GMU always-on；Mesa的RB_DONE_TS query及可选Perfetto路径另有
GPU时域offset。因此不能因为两者都写19.2MHz，就直接把CSV绝对时间当Surface时钟。
原工具相对区间可以保留并明示换算口径；精确同窗关联须有可证的提交/时域锚点，
否则继续把pass样本窗与Surface窗的界限写明，而非虚构对应。
