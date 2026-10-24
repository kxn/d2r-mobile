# Q735：真实游戏官方 GPU 队列追踪

当前没有达到60FPS。本轮补齐了此前缺少的真实游戏 GPU renderstage 证据：CPU准备继续变快并不保证出图同步变快，必须同时处理实际GPU关键路径。下面的GPU时间均来自开启官方追踪时的执行状态，不能冒充无工具性能基线，也不是纯Shader时间。

## 实际实现与资格

在原f830完整驱动源码上，仅启用Mesa现成Perfetto SDK v56.1，得到独立89e4c682驱动；没有改渲染算法、着色器或内存实现。真实独立transfer验证先通过copy/fill字节和fence检查，并取得正确PID/context下的GPU时间戳。它只证明transfer，不代表游戏。

新驱动的UUID和ELF摘要与旧表不同，严格资源门会拒绝旧表。因此使用原8064生产器、现有压缩输入重新生产全部6217项，第一批128项和余下48批6089项均完成原roundtrip校验并退出。没有重新编码ASTC，也没有改旧recipe的身份。原strict packer重新封装后，6217项payload、CFT、descriptor、thumbnail和family分别逐项相同，保留原CASC；新表只用于独立诊断。

诊断运行时源码为独立分支`codex/turnip-perfetto-resident-v1`的`5d329bd`，基于主线9057。仅在两个启动期精确驱动合同中加入89e：原shared实际字节门通过后保持shared-lazy；原table/UUID/driver/producer/pack门通过后保持native resident。未知驱动没有获得新资格，无sparse、绑核或游戏逻辑修改。源码1034项、六库、UI及签名、原始NSO重建的28项产物均核验；其中24项执行和规则产物与日常相同。辅助库整体文件摘要含新debug/buildID，五个辅助库的所有allocated sections（除buildID）经LLVM readelf比较相同。

实际游戏PID13423进入原角色选择及营地，shared-lazy实际开启，原native表安装门通过，实际mapped驱动inode/device与独立部署一致。固定.60、2750×1260、target60、原950 shader及6217项混合ASTC资源；预热至tick1003，PSO174、variants32。营地截图含原角色、场景及HUD，未见明显黑块或缺几何；这是单场景资格，不代表战斗和长时稳定性。

原system Perfetto工具请求`gpu.renderstages`、`mesa.default`、CPU调度和Surface事件，共8秒。实际trace为25,520,168字节，SHA256 `46d03c6e06f4bd9423ad69bc36f0d1371d36301e02698ab230af1d6c50c90e21`。该运行完成原生命周期fresh save commits=2并退出。原日常APK、f830、旧表及33项资产已实际恢复并独立核对，层设置不变，生产器也已退出；未使用暂停借出的设备。

一次正式采集前的controller失败另存于`qualification-GPU/`：JNI只把stderr重定向至native.log，controller却要求stdout的identity JSON出现在该文件。游戏本身已经成功安装表并运行，尚未开始GPU采集。修正为实际映射身份和原严格表安装门后，第二次实际运行取得上述trace。旧失败及save2收条保留，不冒称两次GPU对照。

## 首次实际队列结果

官方TraceProcessor确认真实游戏UPID1/context2的GPU执行事件。正常执行位于Queue0/hw_queue4，warning轨道不能当第二条执行队列，也没有据此证明并行binning。没有error/data-loss；clock_sync_cache_miss是info，不能当数据丢失。

|稳定Render Pass组|本次次数|GPU区间均值|
|---|---:|---:|
|1650×756、2附件、8bins、641draw|206|13.337ms|
|1650×756、1附件、4bins、641draw|207|4.921ms|
|2750×1260、1附件、10bins、783draw|207|3.544ms|

这些是TBDR pass区间，含阶段及同步边界；不能与其嵌套binning/GMEM子项再相加，也不按单个draw归因。主组原args显示`ubwc=y|y`，不支持简单认定当前主组未启用压缩。

最大组内部按原区间裁剪并排重后，平均Binning为2.259ms、GMEM标记覆盖10.979ms、其余0.100ms，合计才是13.337ms。GMEM覆盖含每tile绘制及clear/store等，不是纯片元着色成本。相同硬件队列的CommandBuffer映射端点存在368处小重叠，最大0.824ms；保留官方时钟/流水边界精度限制，不据此宣称额外硬件并行或微秒级因果顺序。

同SurfaceView首尾203个完整间隔覆盖7.829秒。CommandBuffer/执行阶段包络并集约7.780秒（99.375%），包络外最大内部空隙约0.798ms。这个数字只表示带工具状态下GPU队列执行包络密集，不表示Shader Busy=99%、DRAM带宽饱和或没有流水线内等待。present的2.1756秒睡眠中约2.1706秒与GPU包络重叠，进一步支持检查GPU未完成的原因，不能将它归为单独的CPU锁等待。

KGSL官方追踪自身会等待profiling buffer中的`gpu_ticks_queued`，还增加时间戳/trace工作。本次Surface约38.57ms不能与无工具日常约33ms直接做优化比较，不能猜一个扰动值扣除。原CPU7对照仍是约21ms Running、约33ms Surface；既没有满足16.67ms CPU预算，也没有因CPU缩短而涨帧。

本次同窗owner Running为26.034ms/Surface，不能与未开官方GPU追踪的CPU7对照混算。1654个有GPU阶段且严格关联的提交，从真实CPU queue_submit返回到首个GPU阶段平均37.749ms；这表示带工具队列及依赖延迟，不是API执行耗时。原GPU工具的traceq0和TracingMuxer线程也有实际CPU开销，不能把它们加入owner后称作原游戏日常成本。

## LRZ与更高层CPU批处理

主8bin组显示`lrzWriteDisableReason=Stencil may kill fragments`，第1个draw后禁写；4bin组同原因发生于第539个draw。pass末尾的`lrzStatus=DISABLED`不能证明全程都没用LRZ，也不能将禁写混同禁读。旧同暂停相近主类646draw的实际stencil是EQUAL、compareMask=(2,2)、reference=(0,0)、writeMask=(0,0)，测试有效位，并非mask0恒真；旧类不是本次641draw组的逐对象对应证据。不能为解除warning删掉真实stencil或强制Early-Z。

CPU侧同时审查旧完整2445draw快照的批量表达机会：完整固定状态及几何完全相同的相邻draw组为0；仅Uniform地址变化的92组236draw，排除未知shader variant后51组129draw，需要真实DrawID/UBO表表达，不能直接换一个MultiDraw调用。另有173次相邻变化仅是索引地址，组成80组253draw；若物理VkBuffer相同且query/顺序允许，可能用firstIndex表达而不改Shader。现有快照没有物理span资格，173不等于合法可合并数；不将旧次数比例乘当前采样份额认领毫秒。大规模bindless/DrawID改造目前没有足够覆盖预算。

## 接下来的判断标准

1. CPU按完整职责和净工作量评估，不能把约9%的Buffer父路径或约9%的图像父路径全部称作重复劳动。已有复用命中先扣除，必要publication/hazard/lifetime不能跳过。
2. GPU优先解释最大的稳定pass组：实际Shader与资源读取、binning/GMEM阶段及pass边界，再结合原SDP全局读写/纹理计数；不把pass时间直接称Shader时间或盲目开Early-Z。
3. 只有精确证明输入、生命周期和观察边界相同，才推进整段准备复用或批量draw；目标是取消消费路径，不在它前面再叠一层逐draw检查。
4. 当前没有60FPS、长期温度或新默认性能收益。追踪驱动与两项冷合同保留为独立工具候选，日常驱动和设置已恢复。

## 默认模式与SYSMEM的实际四臂对照

根据上述GPU数据，实际完成当前日常b8/d34/f830/950/6217/.60的default/SYSMEM/SYSMEM/default对照。driver、shader、原资源、分辨率和target60保持一致，没有绑核、ADPF或GPU-stage工具。各臂在冷启动设置原Mesa `debug.mesa.tu.debug`，用本次PID的官方启动日志核实位1（startup）或9（startup,sysmem），在正式测量前关闭logcat；原Simpleperf group read10秒和Perfetto8秒不变。全部按8个distinct稳定发布、tick≥1000、variants≥30预热。默认模式仍允许驱动逐pass选择GMEM或SYSMEM，不等于全帧强制GMEM。

|臂|模式|Surface均值ms|owner Running ms/Surface|用户指令M/Surface|用户周期M/Surface|加权MHz|
|---|---|---:|---:|---:|---:|---:|
|A1，observable|default|33.026|26.635|57.879–58.312|34.432–34.726|1459.95|
|B1|SYSMEM|27.776|21.251|57.794–58.149|33.153–33.403|1770.49|
|B2|SYSMEM|27.744|22.768|58.221–58.577|34.083–34.323|1684.43|
|A2|default|33.032|26.144|57.797–58.220|33.370–33.636|1435.63|

两组Surface间隔分别减少15.90%/16.01%，约30→36FPS；Running分别减少20.21%/12.91%。但指令没有一致下降，cycles第一对减少2.99–4.53%，第二对反而增加1.33–2.85%；CPU频率和驻核也改变，不能按MHz归一或称减少了等量重复准备。B2为variants30→30，其它为32→32，PSO均174；工作形状差异保留。A1完整窗CPU6计数缺边界，覆盖88.103%，不能以其部分51M指令对比B1完整58M；表中A1用预先机械裁剪的231个间隔，计数覆盖100%，其它三臂完整窗也100%。

节奏不是简单的均匀36FPS：A1 observable有8个约22ms、219个约33ms、4个约44ms间隔；B1为138个约22ms、143个约33ms，因此其中位数仍约33ms。这里只报告官方实际间隔，不凭它断定显示刷新率或强制帧率上限，也未实现用户暂缓的帧回调/丢帧方案。

A1测量及save2均成功，空字符串恢复属性被ADB shell丢参导致collector收尾1；原错误保留，root在游戏退出后按owned值CAS恢复并独立核资产，补证收条单列。原B2在正式测量前因没有本次驱动启动日志退出，未产生性能样本；没有覆盖该失败。补官方logcat启动前唯一marker握手后，B2-qualified-v3实际取得本次PID的位9、完成正式工具及保存退出，握手不替代驱动位证明。A2也使用相同握手。实际四窗均save2、进程退出，原属性、APK、33资产和层设置已独立核实恢复。

这是当前负载对渲染模式有明显且复现的整帧敏感性，不是已证CPU删工或60FPS。先不永久强制SYSMEM：它可能改变外部内存流量和功耗，下一步用原官方SDP比较同窗GB/s与每个Surface间隔的实际读写量，再决定取舍。旧scale1.0、旧shader且未共享的模式数据不能代替本轮结果。

四臂原捕获、full/observable界、355项原输入复核及恢复收条见`.local/logs/current-f830-sysmem-abba-v1/`。当前新641draw GPU组缺少graphics pipeline/module关联键；同f830/950/.60的历史官方GFXReconstruct结构帧为607draw，不能把其Shader时间移用。已准备原GFXR单帧入口补当前实际Shader/变体身份，尚未执行，不当作新抓帧结果。

## 当前模式的官方带宽对照

在相同日常APK、f830、950、6217资源和.60下，实际完成default→SYSMEM两次新的官方Snapdragon Profiler九指标窗口；每臂按自己的完整Surface首末边界积分，不用12秒流量除8秒帧数。PSO均174→174、variants均32→32。两张营地截图未见明显缺画面或黑块；仍不代表战斗、其他场景或长时稳定性验证。

|指标|default|SYSMEM|
|---|---:|---:|
|Surface完整间隔数|237|284|
|Surface均值ms|33.036|27.762|
|翻译线程Running ms/间隔|25.144|21.995|
|总读取MB/间隔|132.826|124.203|
|其中纹理读取MB/间隔|100.097|104.114|
|总写入MB/间隔|61.778|65.211|
|片元M/间隔|33.583|33.618|
|总读取GB/s|4.020|4.472|
|总写入GB/s|1.869|2.347|

本次约30→36FPS再次复现；每个完整间隔的总读取下降约6.5%、写入增加约5.6%，两者合计下降约2.7%，而每秒读写量随吞吐增加。纹理读取已包含在总读取中，不能再相加。两个窗口GPU时钟均约500MHz，Shaders Busy分别约66.5%/73.9%；这些全局计数包含合成器，不是游戏独占GPU时间，也不能据此断定带宽饱和。CPU频率和驻核仍有变化，本轮没有PMU指令/周期，不将Running下降写成删除了相同时间的CPU准备。

## 现成的按 Pass 自动选择：测试入口与证据边界

SYSMEM依然由GPU渲染，指绕过GMEM分块、直接访问系统内存中的附件；不是CPU软件绘制，也不是本项目的CPU/GPU共享Pool导入。GMEM通过片上附件存储减少外存往返，同时有分箱、分块重放、装入/写回等成本。少draw短Pass往往更适合SYSMEM，大量混合/多重采样等可能更适合GMEM，不能仅按draw数量或单个draw时间判定。[Mesa官方架构与Autotune](https://docs.mesa3d.org/drivers/freedreno.html#autotune)明确支持按Pass在两种模式间自动选择，不需要我们另写分类器，也不应为中途强切而人为拆Pass。

当前实际f830源码已核对原entire-source-ledger：`tu_autotune.cc`默认BANDWIDTH按估算带宽选择；PROFILED使用实际Pass时间历史选择，PROFILED_IMM只用于少数实例即定案的另一策略。该估算并非完整的执行时间模型，因此全局SYSMEM更快仍可能来自原模型的选择偏差，不能因此认定原渲染在CPU执行。主/secondary原flags没有SIMULTANEOUS_USE，该标记会使驱动禁用复杂调优，核查未发现这条阻断。特殊反馈/FDM/MSRTSS及小Pass等驱动强制/高置信规则仍保留。

另开现成`bandwidth/profiled`对照：同f460 APK、原f830/950/6217资源、.60、source/retained均OFF，分别冷启设置标准`debug.mesa.tu.autotune.algo`（原`os_get_option`Android映射）；原Simpleperf/Perfetto采集和数学不变，仅增加本PID官方启动日志证明及独立CAS恢复。源、delta、配置和原始结果位于 `.local/logs/current-f830-autotune-abba-v1/`。启动声明只证明算法选择，不冒称已逐Pass观察到混合决策。

第一对全核计数覆盖100%，bandwidth/profiled的Running26.650/24.439ms，Surface均值33.118/30.257ms，但中位仍约33.2ms；指令分别58.851–59.277/59.535–59.922M、周期33.917–34.198/32.774–33.018M/Surface。即profiled指令更多0.44–1.82%，周期较低2.65–4.16%，不属于删除了相同CPU操作。频率1434/1511MHz、variants32/30不同，PSO均321；这些差异保留，单对不能声称稳定净收益。报告 `.local/logs/current-f830-autotune-abba-v1/first-pair-review/`。第二个profiled首试在测量前缺本PID启动位日志而拒绝，没有性能窗，原失败单列；后继参考/qualified对照必须独立目录，不覆盖或放松原门。各已完成进程均原save2/stop、33资产与属性恢复。

这不支持“SYSMEM必然以巨量额外每帧流量换帧率”的简单判断，也不支持已经降低长期温度或功耗。短窗顺序、真实绘制形状和其他场景仍有限制，不能据此永久全局强制SYSMEM；应继续定位哪些pass从模式选择获益，并保留默认驱动与完整依赖语义。

两次正式GPU采集和原save2/stop均完成。SYSMEM collector原终态1另存：关闭GUI后，pluginPerfetto PID27901在身份读取期间退出，唯一收尾错误没有覆盖。独立root收条确认13个原collector、两个host工具和游戏均退出，APK、33资产、层设置及空Mesa属性均恢复；仅用该精确收条补证分析。原分析manifest还曾在sealer写末尾控制台输出前记录该日志；原清单和失败保留，最终封装仅允许这两条日志的精确哈希变化，不重采或改原数据。独立最终清单的124项文件已逐项复核，SHA为`8f04e68ef2d3ef1f860a41ffab026d480b67e31faf292a38123a2fba7b4672f3`。捕获、收条和离线结果分别见`.local/logs/current-f830-sysmem-sdp-game-v1/`、`.local/logs/current-f830-sysmem-sdp-game-v1-analysis/finalized/`。

原始采集及实际恢复：`.local/logs/current-game-renderstages-v1/`；官方SQL与队列分析：`.local/logs/current-game-renderstages-v1-analysis/`；生产及严格封装：`.local/logs/perfetto-native-rebake-v1/`、`.local/logs/perfetto-native-pack-89e-v1/`；CPU批处理审查：`.local/logs/adjacent-draw-batching-audit-v1/`。工具配置可用`capture-android-scheduler.py --mesa-renderstages`请求现成数据源；请求成功仍必须核实实际GPU记录。
