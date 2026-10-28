# 当前12b GPU动态Profile能力与最小补口

**当前可直接使用的是22项原厂全局指标、KGSL作业跨度和Perfetto调度轨迹；尚不能把等待归到某个FS的某条LDC、某张纹理或某个PC。** 已安装Snapdragon Profiler 2026.9确有逐Shader/指令profile入口，但菜单存在不等于当前12b私有驱动已经可用。本文只整理现有证据，不新增采集、计时器或采样器。

## 已经真实可用的22项

准入依据是`fs-bounded-uniform-prefetch-game-device-v1`两正式Surface窗：实际driver SHA均为`12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5`，22项各有155/156个有效样本，非旧stock数据。以下保留原厂名称，便于复现选项。

| 分组 | 已有指标 | 可回答的问题与界限 |
|---|---|---|
| 外存与总线（4） | Read Total；Write Total；Texture Memory Read BW；GPU % Bus Busy | 比较整窗实际读写量和GPU到系统内存总线活动。Texture Read是Total Read子集；Bus Busy不是整机DRAM饱和率。 |
| 纹理请求（4） | % L1 Hit；% Texture L2 Miss；% Texture Fetch Stall；% Texture Pipes Busy | 看命中比率、请求受阻和任一纹理管线活动；不能给出某条采样/LDC的次数或独占延迟。 |
| Shader活动（4） | % Shaders Busy；% Shaders Stalled；% Stalled on System Memory；% Wave Context Occupancy | 区分可见的活跃wave、全部shader idle但有active wave、L2等系统内存受阻与占用情况；各分母不同，不能相加。 |
| 算术与EFU（5） | % Shader ALU Capacity Utilized；% Time ALUs Working；% Time EFUs Working；Fragment Instructions / Second；Fragment EFU Instructions / Second | 检查算术/EFU活动和发出工作量。Fragment Instructions为full-precision scalar ALU-equivalent，含插值/EFU、排除texture fetch，不是静态ISA条数或load次数。 |
| 阶段（3） | % Time Shading Fragments；% Time Shading Vertices；% Time Compute | 是相对全部shading工作的阶段份额，不能当整帧耗时比例。 |
| 时钟观测（2） | Clocks / Second；GPU Average Frequency | 可记录原厂观测；目前没有独立物理OPP或gating证据，详见下文。 |

这些值为**global**，包含其他GPU工作及compositor。rate应按相同完整Surface时间窗积分，再除以完整Surface帧数；百分比按原定义加权报告。不能把全局metric乘GPU毫秒推算某个pass的可省时间，也不能用百分比之和补出“全部stall”。

KGSL已用实际队列/线程识别主批与present copy，并校准XO时间基准，可比较SOP/EOP跨度、已知区间union及CPU等待的重叠。跨度包含GPU内部访存/同步等待，**不是ALU busy**；逐draw重放时间也包含TBDR边界与前序工作，不能当shader exclusive。

## 下一次必要采集只补三个宏观量

当前provider目录有下列条目，同机旧stock的两个已核窗口各有239个连续、有限、非负样本；**当前12b正式22项未选择它们，尚未准入，不能声称已支持或已不支持**。

| 最小补口／目录ID | 应排除的假设 | 仍不能推出 |
|---|---|---|
| Fragments Shaded / Second／941559010 | 纹理总工作变化是否主要随fragment量变化 | 不直接等同像素数或Vulkan fragmentShaderInvocations。 |
| Textures / Fragment／2177205124 | fragment量相近时，纹理引用密度是否变化 | 不是某条native纹理issue/LDC计数，不能直接指定材质或阴影。 |
| SP Memory Read (Bytes/Second)／2589758990 | shader processor非纹理外存读取方向是否值得继续查 | 不是UBO专用，不能以SP+Texture拼Total；很低也不能排除片上命中后的地址依赖等待。 |

先在实际12b上核fresh provider、定义、有限值和同窗覆盖，再冻结共同指标。只对rate积分，Textures/Fragment仍按原厂ratio解释；不得用旧stock数值填当前空白。更多指标会增加采集干扰，不为字段齐全一次打开68项。上述三个量能区分宏观工作量假设，不能单独证明LDC等待来源。

## 逐Shader／逐PC的实际缺口

原厂GUI、插件确有shader profiling replay、`IShaderProfile`及DrawID/Stage/InstructionIndex/CounterType/Counter字段；SnapshotPlayer也有`-counterlist`与`-fetchcounters`。后者是逐draw重放counter，不能自动称为per-PC采样。

但本次两DB的`SCOPEShaderProfiles`、`SCOPEDrawStages`、`SCOPEDrawStageMetrics`、`SCOPEStageMetrics`均0行；既有124份DB清单也无这些表的非空动态结果。已有Shader Analyzer的指令/short-long sync条数是静态编译统计，不能解释成真实等待周期。

工具实际依赖私有`vkGetProfilingProcAddressQCOM`／SCOPE合同，准确12b冻结源码/include没有该接口的实现或引用。随后在必要的HOF离屏功能验证中，普通UID10377已实际加载同一12b，global/instance/device三路ProcAddr查询均返回NULL（`qcom-hof-offscreen-v1/ACTUAL-DRAW1/raw-compile.log`，`HOFDRAW_PROFILE_ENTRY global=0 instance=0 device=0`）。因此所需入口在这个实际进程不可见；查询没有调用未知ABI，也没有取得原厂SCOPE的availability返回码，不能把它写成SnapshotPlayer已经返回某个unsupported错误。global KGSL counter可用不能替代这项准入；默认player使用system driver的结果也不能冒充私有12b。

若确需这一级归因，先做同12b的原厂availability功能门，确认CounterType单位/含义，再要求动态表非空、draw/native身份闭合，才可谈逐指令归属。手册5.3.3另明确compute和secondary command buffer的Vulkan Snapshot metrics尚不支持；不能先改生产录制方式后把重放结果当原运行预算。若接口确实拒绝，保留具体返回，继续现成全局工具与受控候选对照，不另写profiler。

## 频率与当前判断

已核正式窗内GPU Average Frequency与Clocks/s÷1e6逐点相同，没有提供两份独立约束。Perfetto有真实CPU cpufreq事件，但没有GPU OPP/电源状态初值及有效轨迹。配置请求过`power/gpu_frequency`且它不在unknown/failed列表；零样本不能证明物理频率恒定或接口不支持。KGSL的19.2MHz是XO时基，与shader core频率无关。

因此可比较实际读写、作业跨度、CPU Running/等待及原厂running-clock观测；不能据约500MHz宣称锁频、调频因果或归一化GPU成本。独立频率调查若进入下一次必要工具窗，应先核设备实际可用的pwrlevel／clock rate／电源状态及初值，不改频、不写sysfs。

现有预取对照已证候选命中、整帧外存读取略增、主批净变化很小，**不能据此认定Uniform等待必然很小**。上述三项用于区分fragment量、纹理密度与SP外存工作；逐PC功能门用于判断是否能直接定位依赖等待；频率初值/轨迹用于排除供给变化混淆。三类证据各回答一个问题，不互相冒充。

## 证据入口

- 动态能力审查：`.local/logs/shader-dynamic-observability-audit-v1/REPORT.zh-CN.md`，及同目录`current-formal22-and-dynamic-tables.json`、`historical-stock-additional3-proof.json`、`catalog-and-sample-availability.json`。
- 物理频率边界：`.local/logs/gpu-frequency-independent-audit-v1/REPORT.zh-CN.md`，含正式窗配对CSV、官方TraceProcessor输出及2026.9手册来源ledger。
- 真实预取对照与候选边界：[游戏报告](ANDROID_BOUNDED_UNIFORM_PREFETCH_GAME_2026-10-28.zh-CN.md)。本文没有重新测量或改变既有结果。
