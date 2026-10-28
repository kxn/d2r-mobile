# 当前c09／12b：25项原厂GPU宏观窗口

**新增三项已在当前12b真机正式窗口取得有效值：约2620万fragment／Surface、纹理引用密度时均2.955、SP外存读取1.039MB／Surface。纹理外存读取85.749MB，主GPU批次19.635ms且近连续；当前应继续减少真实GPU工作和读取，不能把CPU约3ms睡眠直接当成可删帧长。** 这是一份现状诊断，不是优化A/B或稳定涨帧结论。

## 真实资格与分母

证据目录：`.local/logs/gpu-macro-25-game-device-v1`，唯一正式臂`CURRENT-G1`。原receipt为completed/window_qualified/game_stopped，原生命周期存档退出；PID22600、owner22659由原native身份与同trace核准，不选最忙线程。`period60-admission.json`独立qualified：实际target60、pace60、period16666665ns在原允许误差内；原约1Hz状态覆盖整个Surface窗。旧c09没有后来新增的显式cold-cap入口，不能与52d5等新carrier拿FPS作严格收益对照，也不据此证明每帧强制cap或物理面板刷新率。

同日常c09 APK／b828 ELF、cf83 shader、12b8驱动、原BC3共享/lazy资源；实际game/swap1968×900、main1673×765、scale.85、受控22营地。沿原25指标SDK及Perfetto采集，无新计时器/采样器。

25项逐项核provider2690794109、原DB定义、有限正值和fresh覆盖：正式导出每项157点、最大相邻间隔低于500ms，原provider前后覆盖完整Surface窗。三项ID分别941559010／2177205124／2589758990；这是当前12b的实际准入，非援引旧stock的68项。原厂比较器按同399个完整Surface间隔积分rate；Textures/Fragment仅保留ratio均值，不积分、不与fragment均值相乘发明准确请求总数。

| 当前正式窗 | 数值 |
|---|---:|
| Surface间隔数／工具FPS |399／50.366733|
| 帧间隔mean／p95／max |19.854375／22.161411／33.241355ms|
| Fragments Shaded |26.199427M／Surface（1.319580G/s）|
| Textures / Fragment |时均2.955012；原样本2.668244～3.289868|
| SP Memory Read |1.039187MB／Surface（52.340MB/s）|
| Texture Memory Read |85.748798MB／Surface|
| Total Read／Write |104.860435／44.155811MB／Surface|
| FS／VS／CS shading份额 |81.2489／13.2850／5.4661%|
| Texture Fetch Stall／Texture Pipes Busy |12.1756／53.0201%|
| L1 texture hit／L2 texture miss |63.7055／4.3170%|
| Shaders Stalled／System Memory Stalled |16.7205／.4106%|
| Wave Context Occupancy |58.0408%|
| ALU capacity／ALUs Working／EFUs Working |33.8996／39.3661／13.6250%|

全部为global，含compositor等工作。fragment定义是提交给shader engine的工作，不自动等同像素数或Vulkan fragment invocation；跨多次pass/不同分辨率的总量不能除主RT像素直接叫overdraw。阶段份额不是整帧时间比例；不同stall分母可重叠，不相加、不乘GPU毫秒造出独占stall预算。

## GPU作业与CPU等待是否留出大空泡

复用原官方TraceProcessor/KGSL reducer，按本次实际submitted线程重新识别owner context40及RenderThread context39。主/复制角色按真实queued所处API判定；未匹配的不按大小猜。XO19.2MHz与原已资格公开源码时基一致，本窗经验拟合及残差保存在`hardware-analysis/CURRENT-G1/kgsl-summary.json`。

| 同Surface窗口 | 实际值 |
|---|---:|
| 主批SOP/EOP跨度（397批）mean／p95 |19.634888／19.746042ms|
| present-copy（397批）mean |.189301ms|
| 已知GPU区间union／区间内空隙 |99.873173%／10.007240ms（全约7.89s内部窗）|
| 前缀映射unknown |31.436406ms，未外推补齐|
| 主／复制在途数中位数 |3／4|
| owner Running／Runnable／S／D |16.210011／.204264／3.341276／.010528ms／Surface|
| owner未被thread_state覆盖 |.088297ms／Surface，保留unknown|
| present wall／其中S |3.532690／3.090127ms／Surface|
| acquire wall／submit lock／present lock |.082148／.001769／.000736ms／Surface|

GPU union包含访存/同步等待，**不是ALU忙度**。owner S的99.8294%与已知在途GPU跨度重叠，replay wall约99.9031%重叠；kgsl-events对应的后续Runnable唤醒归属约3.08175ms／Surface。这与GPU队列近连续、CPU受呈现背压约束相容，不支持先假定这3ms是GPU空泡或大抢锁。唤醒身份本身仍不等于整个sleep的独占根因。

whole-app Running29.901882ms／Surface；owner实际CPU cpufreq Running加权2045.675MHz，whole-app1500.995MHz。NVN replay Running9.216353ms／Surface。没有同窗PMU，不能把这些值变成精确指令预算或CPU纯计算优化收益；本轮不扩展该方向。

## 资源账与时钟边界

原before/after快照共9.224667s、465个guest提交。现成period合同用官方时钟转换证明这两个端点确实括住Surface窗，额外前736.423ms／后566.348ms；资源delta只除自己的465，不能除399。通用整轮Arm没有PMU时钟receipt而未自动绑定该窗口，本目录`offline-analysis-agent/assessment.json`单列原period映射资格，不改原结果。

内部submit/guest=1，pool全量/使用区上传与readback bytes均0；纹理prepare4193／guest、descriptor allocation .025806／guest、pipeline lookup385／guest。它们是工作计数，不是耗时。实际API每周期仍有主提交和present copy，不把物理两次混成旧stream多次内部提交，也不将GPU外存读取误当CPU上传。

SDP Average Frequency499.687896MHz与Clocks/s499687896.329为同数值信息；没有独立GPU OPP/gating轨迹，不能称物理锁500MHz或用其归一化成本。CPU cpufreq确有实际轨迹；KGSL XO时间戳也不是GPU核频率。

## 这次缩小了什么范围

1. **当前不是“只等CPU交下一帧而让GPU长时间空转”的明显形态。** 主批约19.6ms、已知作业近连续，CPU等待主要与GPU在途重叠；队列锁很小。还不能据此称所有GPU单元都饱和或排除所有相位影响。
2. **外存字节应优先看纹理路径。** Texture Read约为Total Read的81.77%，SP Read仅约为Total的.991%；这是同窗宏观读量关系，不是互斥预算分割或延迟份额。SP不是UBO专用，低SP外存也不排除片上cache命中后的LDC、地址依赖或读取发起成本。
3. **fragment工作量与每fragment纹理密度现在有了实际观测。** 但本窗只有一个固定场景，不能拿2.955平均值否定某个热点FS的多次fetch；也不能确定26.2M来自哪个pass。后续受控GPU候选应同时看fragment rate、纹理密度、Texture/SP/Total实际读量及主批跨度，区分少画、少读与只是搬移读取时点，不再用静态少几条指令替代净收益。

原始/派生入口：`gpu-tools/comparison.json`（原厂25积分）、`offline-analysis-agent/{assessment,single-window}.json`（provider/覆盖/整账及同trace scheduler）、`hardware-analysis/{CURRENT-G1/kgsl-summary,api-overlap-summary}.json`。全部源SHA与SQL保留；本报告没有新采、重新计时或改变生产源码。

root另行完成了最终恢复核查：`final-restoration.json`（SHA `648c8568490fd8a83d420d12ed4d618f80d04e69677d55aa714bc28cb23284e8`）确认本次22文件用户存档与偏好、c09/12b/cf83及六锚点精确保持，游戏和13个本次采集进程/GUI/Xvfb均已退出，三个独立HOF包已卸载。原cleanup的进程退出身份查询竞态失败及独立pm退出码检查失败均保留；最终只读状态核验通过，没有重写原失败。
