# 有界 Uniform 预取：真实游戏对照

本轮已实现并执行 DSP3 有界预取候选：新模块在合格窗口实际被选择，但**尚未证明稳定整帧收益**。同条件单对 FPS 为50.133312→50.242624（+0.218%），GPU主批仅19.713489→19.697857ms（−0.015632ms）；全局总读取量反而约+0.55%、Fragment scalar ALU-equivalent约+0.75%。候选及机制证据保留，不把静态改写当涨帧，也不只因一次FPS差异很小而删除实验。

状态：两正式窗口均完成、原生命周期各存档2次并停止；root已独立核实恢复旧正式c09/12b/cf83及本次入口22个用户文件，所有本次工具退出，详见末节。候选仍保留为实验，未成为日常默认；host测试不替代真实游戏结果。

## 固定条件与证据

数据目录：`.local/logs/fs-bounded-uniform-prefetch-game-device-v1/`，仅使用其中BASE-G1与PREFETCH-G1，两者receipt均completed/window_qualified/game_stopped。

| 项目 | 两臂共同条件 |
| --- | --- |
| APK /host ELF | `52d5a2588a83…` /`97bf4816984f…`，同一normal cap60 carrier |
| driver /资源 | Turnip `12b8b54a2514…`；同BC3 linear payload、producer及认证表`6890107f0b86…` |
| 输出 | 实际game/swap1968×900，3D1673×765，比例.85 |
| 调度 | inline CPU7、30µs、stream/hints关闭，原fixed/extra cache、Store domain、clear、sysmem/UBWC相同 |
| 节奏 | 实际target/pace60，period16666666ns；已有约1Hz状态括住Surface，不冒充物理panel逐帧刷新测量 |
| 场景 | 同受控22文件seed；camp area/client_area1，5225/6289，level3/经验2920/gold51 |
| 稳态 | 各窗PSO181、variants32、pending0，图像allocation/rebuildable313450496/126820352B不变 |
| 唯一候选变化 | shader派生cache `cf83dfece8e7`→`e7afdaddc94e`；14个已验证runtime文件CAS发布；base.spv、原selector和游戏CPU binary不变 |

同窗原厂22项GPU数据由既有Snapdragon Profiler入口采集，CPU/调度/Surface及KGSL由既有Perfetto入口取得。没有新增计时器或采样器。整轮对照：`whole-profile/comparison.{json,md}`与`whole-profile/REPORT.zh-CN.md`；原厂积分：`gpu-tools/comparison.json`；硬件原SQL、CSV、时基及角色：`hardware-analysis/`。

## 原生机制成立，新增成本也保留

原生编译证据在`.local/logs/fs-bounded-uniform-table-compile-v2/ACTUAL-COMPILE1-analysis/`。普通UID、实际12b驱动与正确PSO上下文下，两baseline `/1`精确复现原native `4a9791e49ba8…` /`b453050bc416…`。v1曾被编译器变成无条件旧动态LDC，失败证据未覆盖；v2仅对fallback选择结构使用DontFlatten，最终NIR与native均保留真实域外分支。域内走常量区选择，域外仍执行原完整32位动态索引回退；不能因静态cat6仍为7就判定机制未实现。

| 静态原生成本：原→v2 | 第一FS | 第二FS |
| --- | ---: | ---: |
| non-NOP |1563→1593|1636→1652|
| full GPR /max waves |19/10→19/10|19/10→19/10|
| constlen |120→128|104→112|
| static SY |25→27|25→25|

两份都新增实际分支/jump与范围/选择计算。原目标UBO固定copy为vec4[0,4)与[12,24)，候选合成[0,24)，实际新增逻辑copy区间byte[64,192)，源表本身仅[112,176)。因此采用**192B运行时资格**，不是176，也不假设preamble扩读自动安全。每次preamble/波的动态执行次数未测；128B固定逻辑copy增量不能直接换算DRAM字节或毫秒。

DSP3保留完整旧DSP2及旧特化模块，原selector成功且所需UBO范围合格才选新模块；短范围或新模块驱动拒绝退旧特化，旧特化拒绝才base。见[DSP3范围协议与缓存合同](ANDROID_UNIFORM_RANGE_VARIANT_2026-10-28.zh-CN.md)及[可复现shader派生工具](../tools/experiments/bounded-uniform-prefetch/README.md)。

正常与GCC ASan/UBSan的production loader/原NVN编码器/Lavapipe Core+SYNC各21项通过：6正向组、15真实损坏拒绝，涵盖192→短范围→恢复、原Spec0、hybrid/observer、prewarm4、PL6和两级实际PSO失败回退。记录在`.local/logs/dsp3-range-contract-v1/FINAL-RECEIPT.json`（SHA`954e27e5994e…`）。原prefix10case热夹具独立通过；新UBO小夹具prefix=0，未冒称新range与prefix热分支组合已实测。测试用自有shader，不能据测试数量证明游戏提速。

## 选择覆盖与分母

| 原state snapshot窗口 | BASE | PREFETCH |
| --- | ---: | ---: |
| 时长 /guest提交 |9.23346s /463|9.27741s /466|
| range_selected增量 |0|231136|
| range_fallback增量 |0|0|
| selected /guest |0|496|
| 全部成功Spec0 hits增量 |313063|314550|
| pending /limits增量 |0 /0|0 /0|

候选确实持续选中了新模块，不仅是加载或预热成功。231136相当于同窗全部成功Spec0选择计数的73.4815%；**这是宿主selection attempts，包含retry，不是已绘制draw、fragment/pixel invocation、GPU时间覆盖率或全部游戏路径命中率**。range统计范围内fallback为0，不能外推所有场景。

原厂Realtime是全局指标，成功PSO记录也可能来自预热，不能直接回答宿主guard是否实际选择。因此只补owner本地attempt计数，在原submit边界发布，复用既有1Hz状态；每draw无新atomic、日志、内容读取或扫描。snapshot不完整括住Surface，其计数按463/466 guest归一化，绝不套396/398 Surface分母。

## 整帧、GPU与CPU完整账

| 项目 | BASE | PREFETCH | 变化 |
| --- | ---: | ---: | ---: |
| 官方工具窗FPS |50.133312|50.242624|+0.218%（单对）|
| Surface均值 |19.946817ms|19.903419ms|−.043398ms|
| p95 /max |22.159479 /33.236041ms|22.160737 /33.235833ms|基本相同|
| KGSL主批SOP/EOP |19.713489ms|19.697857ms|−.015632ms|
| present-copy SOP/EOP |.186998ms|.191284ms|+.004286ms|
| 全局总读 /Surface |105.23305MB|105.81417MB|+0.552%|
| 全局纹理读 /Surface |85.88991MB|86.05716MB|+0.195%|
| 全局写 /Surface |44.17733MB|44.21949MB|+0.095%|
| Fragment issued scalar ALU-equivalent /Surface |1.84754B|1.86135B|+0.747%|
| TextureFetchStall |12.15678%|12.37414%|+.21736个百分点|
| owner Running /Surface |16.584560ms|16.268429ms|−.316131ms|
| whole-app Running /Surface |31.220986ms|30.394070ms|−.826916ms|
| owner S /R-R+ |3.007653 /.219348ms|3.241614 /.206301ms|S增加.233960ms|
| owner实际cpufreq Running加权 |2077.766MHz|2060.044MHz|−.853%|

Fragment指令项为官方full-precision scalar ALU-equivalent（含插值/EFU，不含texture fetch），不是静态ISA条数。原厂读写及指令指标都是全局值，含compositor，不能将全部delta精确归因两个FS，也不能用TextureFetchStall百分比乘主批时间得到独占stall毫秒。现有结果没有证明减少实际DRAM带宽；CPU也没有同窗PMU净指令计数，不能把Running减少直接写成计算量下降。

present wall3.163437→3.433795ms，其中S2.703692→2.993379ms；kgsl-events对应唤醒的S/D归属增加约.284ms。Acquire .080575→.077820ms，submit/present队列锁仍约.0017/.0007ms，未出现新大锁。可观测replay Running9.487676→9.379543ms；候选另有约.02856ms/API窗口未由thread_state覆盖，保持unknown，不补成Running或睡眠。

实际submitted线程分别识别owner context40和RenderThread context39；主/复制角色按queued事件对应真实API判定，非按最大批次猜。已知内部SOP/EOP union为99.77855%→99.89601%，包括GPU内部访存/同步stall，**不等于ALUbusy**；前31.39/52.93ms时基覆盖unknown保留。owner睡眠与应用GPU跨度重叠99.80285%→99.82043%，主/copy inflight中位数仍3/4。CPU少运行、呈现等GPU多一些与这些数据相容，但不是单一因果证明，更不能把约3ms等待全部视为可删除帧长。

内部submit/guest仍1，readback及pool上传为0。texture prepares/guest4270.38→4317，descriptor allocation calls .13175→.05365；图像与driver arena gauge稳定，pool backing多2MiB且各窗内不继续增长。动态/缓存工作差异保留，未用这些变化拼凑“未涨帧根因”。

## 频率与后续边界

**GPU物理OPP与gating仍未知。** 本窗SDP GPU Average Frequency499.2905→499.7931与Clocks/s约为同一数值乘1e6；不是两个独立物理频率来源，不能据此声称锁500MHz或已确认GPU升/降频，也不据此归一化GPU耗时。KGSL的19.2MHz是XO时间基准，与GPU核频率无关。独立审查：`.local/logs/gpu-frequency-independent-audit-v1/REPORT.zh-CN.md`。

这轮完成的是安全范围门、真实原生依赖改写、真实游戏选择覆盖及完整成本对照。当前GPU主批没有明显缩短，新增preamble/分支/选择与全局读取/指令增量必须和局部收益一起保留；单对+0.218%不能作为稳定收益。候选保留，后续应看实际动态读取、分歧和重叠成本，不再以静态代码大小或CPU Running单项代替GPU结论。长期画面、散热和跨场景覆盖未由这个短窗证明。

## 最终恢复与有限视觉检查

root的独立`final-restoration.json`（SHA`5537b63ec9fa71567dbfb85b7417aebbf331357a834bd235c42e7398d6f3f212`）已确认：入口22文件逐项精确恢复并由原备份rename返回，用户存档未coldstart；日常c09 APK、12b驱动、cf83 shader、profile/receipt、native table、module manifest和shared preferences均与入口锚点一致；两游戏、13个本次collector、GUI与Xvfb均不存在，系统settings前后一致。原cleanup helper曾因进程正退出时身份查询发生竞态而返回失败，该失败收条完整保留；没有改写成成功，独立最终检查证明实际进程均已退出。

root对两张`before-window.png`作了有限静止检查：同一营地和主角位置，NPC/火焰相位不同，未见新增大片黑块或几何缺失。这不构成逐像素等价、跨场景长期视觉或散热验证。
