# 分段录制复用：新增成本、整段资源准备与后继修复

目标仍是降低真实 ARM64 游戏的 NvN 翻译 CPU 开销；没有达到同负载 60 FPS。本轮不因首次净负收益丢弃分段方向，而是将录制节省和新增管理分别归因，再修已证浪费。全部游戏测试仅使用 Q735；暂停借出的 Mali 和 6GB 平板未操作。

## 已完成的运行时修复

隔离实验分支 `codex/persistent-draw-recording-v1`，默认关闭，不替换主线默认行为：

|源码|修复|边界和新增成本|
|---|---|---|
|`3016da9`|不可变 shader/schema 与 program layout 资格只计算一次|最终 shader 身份及完整 layout key；分配失败不永久缓存成能力拒绝|
|`62552f1`|实际完成且忘掉引用旧 set 的 CB 后，保留参数池和 sets；清内容资格并强制重新发布，首次发布合批|不修改在途对象；原 epoch/ABA 合同不取消；保留 allocation 高水位，原 256 blocks/context、全局 descriptor 预算及 idle trim 仍有效|
|`5c3322c`|已失去 prefix 保留机会的尾段使用已经准备好的普通绑定，不再支付 UAB 管理|不重复消费资源，不跳同步；新 job 的 slot 在后面才分配，因此根据 next-slot 预算判断，不能提前读未赋值的 valid 位|
|`b16cc33`|参数 owner 的 schema 查找改为 128 桶索引，替换原链表查询|保留 last 早退、完整 shader 指针判等和独立生命周期链；每 context 加 1024B、每 schema 加 8B，碰撞仍逐项比较|

这些修改没有改游戏 NSO、shader 链、驱动或纹理包。每次 APK 仍走原 canonical 入口，验证 1042 项构建输入、六库、签名和原 UI；实验包没有夹带游戏二进制。真实游戏继续执行原离线改写 ARM64 模块、宿主门/HLE 和 NvN→Vulkan。

`62552f1` 的实际 Android 包为 `c5a23f3003e93bd70e843b5ac389ba70e9652df5e355dbe44a76390d8f55aaa2`，host ELF `0ec0aea99962063eb9d7d367035d767789405f38c04b1d56196cc0320384d7a1`，BuildID `1122613c3b19244c37a956b217ee4c6ebb95dbad`。后继 `b16cc33` 包为 `4ae59db51be947815e51bc8fa9f1e114f572a0764af2943f48fb3542f2c539d1`，ELF `809a629f8215f260236c5b302fc0b25b0da8ba04bd051bce646467f003b8513f`，BuildID `70bbbe73783999b8aac89045f205daa9682250d5`。各包、实际输入及源码另存于 `.local/build/retained-uab-*-v1-final/`，不覆盖旧产物。

## 62552：修掉浪费后仍有净额外工作

同一个 APK，固定 .60、2750×1260 输出、f830 驱动、950 shader 链及原 6217 native 表。系统 Simpleperf 指令/周期组读与原 Perfetto 同窗：

|臂|用户指令 M/Surface|用户周期 M/Surface|NvN Running ms/Surface|Surface 均值 ms|
|---|---:|---:|---:|---:|
|A1 OFF|58.553–58.979|33.724–34.005|26.871|33.079|
|B1 ON|至少 66.239|至少 35.700|26.222|34.351|

OFF PMU 覆盖 100%；ON 缺 CPU2 的 0.138177ms 计数边界，覆盖 99.997668%，所以不提供完整线程有限上界、不补零。即使只取 ON 已覆盖核下界，对 OFF 完整上界，指令仍至少多 12.31%、周期至少多 4.98%。ON Running 较少不能解释为删工：其运行加权频率为 1528.8MHz，OFF 为 1420.8MHz，出图间隔也更长；不按 MHz 归一。

ON 宽状态窗口 PSO473→480、variants30→32，但原 creation-marker SQL 在整个既有 trace 及 Surface 窗均未观察到三类 PSO/module 创建；不能凭宽端点变化声称工具窗在编译。两臂均原同 PID 新存档两次、停止游戏并恢复资产/层/属性。

## 新火焰图：省了录制，新增管理仍超过已观察节省

同源 `62552f1` OFF、ON 各三次官方 30s `cpu-clock:u` 99Hz/FP；每份都生成现成 Simpleperf HTML，并保留真实完整调用栈、精确 ELF/inline 和互斥归因。通过原 TraceProcessor clock mapping 与官方 ReportLib 时间过滤，以下只累计各自完整 Surface 窗内的实际用户态采样 period。

|互斥职责|OFF 三窗，采样 user ms/Surface|ON 三窗，采样 user ms/Surface|
|---|---|---|
|普通交付编码|2.910 / 2.953 / 4.311|0.627 / 0.746 / 1.045|
|缓存段真实重录|0 / 0 / 0|0.836 / 0.678 / 0.906|
|普通 descriptor 物化|1.113 / 1.070 / 1.109|0 / 0 / 0 个命中样本|
|UAB ABI 资格|0 / 0 / 0|0.697 / 0.610 / 0.627|
|UAB 参数管理|0 / 0 / 0|3.204 / 2.712 / 3.065|
|实际 descriptor 更新|0 / 0 / 0|1.115 / 0.746 / 0.766|
|复用其它，含缺少 immediate 祖先的 API|0 / 0 / 0|0.348 / 0.475 / 0.627|
|pending 交付|0.086 / 0 / 0.062|0.209 / 0.407 / 0.488|

ON 编码含重录合计 1.463/1.424/1.951ms；新增 UAB 资格、参数、更新合计 5.016/4.068/4.458ms。当前表示确实降低编码，却增加更大的交付成本。零样本不等于零调用；完整 30s ON 仍有少量普通物化。原 owner 消费及未知祖先单列，没有把整个准备入口都叫新增复制。

这些不是逐调用精确计时或随机交替试验的固定因果差值。ON 三窗频率约 1143/1210/1144MHz、Running35.7–37.5ms，OFF 为 1417/1408/1264MHz、Running26.4–30.9ms，OFF 第三窗也明显变慢，全部保留。三窗工具中每桶往往只有几十样本；独立 PMU 与 30s 百分比没有乘算。全部 bracket 的 variants32稳定，ON PSO482稳定、OFF311稳定；没有用 PSO 个数不同证明逐 draw 工作相同。

新完整栈证明池反复创建/销毁已不再占明显样本，reset_completed 仅 0.11–0.37%；剩余 ABI 约 1.66–1.79%。这不是把旧版约 5% ABI、5–6.6% 池职责全算成已省毫秒。

精确 linked PC `0x315af4–0x315b08` 又证明 schema 链查找有 37/47/46 个样本，占完整 NvN 用户态 1.94/2.51/2.46%，是 selection residual 的约 65–70%。这促成 `b16cc33` 的索引替换；不是认领整个管理 9–10%。

原始与分析：`.local/logs/retained-uab-pool-reuse-callchains-v1/`、`.local/logs/retained-uab-pool-reuse-device-v1/`；根逐项复核六窗清单 334 项哈希。

## 为什么资源准备没有随 CB 一起消失

`vk_shader_prepare_values` 返回的是执行过原消费语义后的值。它仍负责当前 CPU 发布、Buffer/image 别名、读写依赖、layout/history，以及本次 recording 的保活；这些不能由上帧 descriptor 相同自动替代。真正只读且完成资格不变的 UBO/image 子集，现有 ready/group 已经整支跳过，不能再将这些早退叫慢重做。

继续审查找到一个可行但覆盖有限的完整产品：按 NvN stage/slot 保留只读 UBO 的完成事实，避免仅 shader 切换就清 ready。历史完整 OP/schema 的保守逻辑模型中，11325 个合格 UBO 消费从可复用8132增到9119，新增987；整 stage 全 ready 仅1251→1275，增加24。物理对齐、别名、回调和实际变体还会缩小覆盖；这不是当前运行时命中率。

独立旧388三份完整栈中，可识别直接 guest UBO 分支仅整线程 2.01–2.22%，包含必要冷工作；最终 prepared 值输出区域仅0.40–0.56%。这不能用来包装一次能跳过12–14%整线程的大改，也不乘新625毫秒。当前不盲目再加一套资源目录；先消掉实证的新交付管理。该方向保留合同，未否定后续扩大安全覆盖。

完整职责、现有复用与 CPU/GPU writer、setter、ghost、Pool/录制边界失效合同见 `.local/logs/resource-preparation-product-contract-v1/report.md`。

## 后继验证状态

不可复用尾段门控15项、最终索引16项主机真实 Vulkan GPU 用例通过。覆盖变化UBO、实际跨帧重放、混合命中/重录、非交换clear尾序、整段失败回退、完成后同set重新初始化、在途拒绝、普通OFF/reference和强制全schema同桶碰撞。碰撞完整GPU用例189次lookup与原生命周期链严格一致，原像素断言全部保留；生产构建无影子查询或测试统计。它们不是 Android 游戏性能证明。

`b16cc33` 已通过原入口构建并安装，首轮实际采集结束且PSO478/variants32均稳定，完整调用栈和独立离线分析继续进行。未在结果出来前认领 CPU/FPS 改善。源和分段实验保留；默认OFF是隔离已有净额外工作，不是删除方向。

### b16 后测已完成的部分

首个 ON PMU 窗有228个完整Surface，覆盖100%：指令66.184–66.623M、周期35.297–35.619M/Surface，Running25.843ms，Surface均值34.333ms，运行加权1527.8MHz。PSO478/variants32稳定，无lost/trunc；原save2/stop及资产恢复通过。这与旧候选数值相近，不构成稳定净省工或涨帧结论。

另三份精确 b16 官方火焰图已实际完成，PSO475/variants32稳定。原 schema 全链已从机器码替换，新bucket短链只有2/2/0样本，旧625全链37/47/46；selection residual从2.79–3.79%降为1.18/0.86/1.49%，不是换到另一个大查询桶。整项UAB参数仍7.33–7.88%，其中值校验/排写仍4.14–4.37%；updates2.59–3.20%，ABI1.37–1.93%。这证明目标子集消退，不证明整个方案净减少。

新图中的采样user交付编码（含真实重录）约1.16–1.79ms/Surface，UAB资格+参数+updates约2.41–3.71ms，另有retained/pending约0.80–1.51ms。不能把其较低毫秒全部归给索引：频率1425–1496MHz，旧ON1143–1210MHz，且本版第三窗Surface仍慢至48.139ms，全部保留。原source失败断言和离线路径错误也独立保留，没有覆盖成成功。完整原始/新图位于 `.local/logs/retained-uab-schema-index-callchains-v1/ON/`，PMU在 `.local/logs/retained-uab-schema-index-device-v1/`。本版OFF首试在测量前未取得原driver启动位证明而拒绝，未生成性能窗；后继qualified另存，原失败不覆盖。

前置准备审计又纠正了三个推断：shared-lazy的Pool operation入口根本不调用draw_pool_collect；UBO ready已经在VS prerequisite之前生效；image group也在图像预检之前生效。因此不能为这三处再加一层缓存。实际尚可向前交付的是既有ordinary FS group的CPU-no-source事实：`!has_layout`且defer publication关闭时，其CPU preflight本来没有动作，前面的logical resolve仍执行。该受限后继只在独立树研究，native/packaged/target原路、registry/revision/phase回调失效及末尾补偿必须闭合；当前约3% wrapper包络含其它类型，不当作可省预算。报告为 `.local/logs/prepared-resource-front-stage-audit-v1/report.md`。

同包OFF后继qualified现已结束：236个Surface，Running26.488ms、Surface33.024ms，PSO316/variants32稳定；已覆盖核指令58.686–59.110M、周期34.409–34.702M。CPU0/1/2合1.129113ms Running缺计数边界，覆盖99.98194%，因此本次不能重用旧62552的100% OFF口径给严格增幅下界。ON/OFF仍没有证明整线程净省工或出图改善。原A1在正式测量前缺本PID启动位日志的失败单列，qualified不是覆盖重采同一性能窗。486项收口输入经根逐项哈希复核，见 `.local/logs/retained-uab-schema-index-device-v1/current-review.md`。

另完成独立无抓帧/采样工具的b16 ON真实游戏营地画面检查：角色、场景和原控制UI可见，无该画面中明显黑块/几何缺失；仅此场景资格，不代表战斗/全场景。启动瞬间截图的HUD数字不作为稳态FPS。原进程10961 fresh save commits=2后停止，未让游戏空跑。记录在 `.local/logs/retained-uab-schema-index-visual-v1/capture/`。

### ordinary source 前置复用：已实机测试，尚无稳定净收益

独立后继 `f460841` 保留已有 ordinary FS image group 的 CPU-no-source 完成事实，提前跳过其逻辑源解析和空预检；不跳过 native/packaged、目标预检或后续 GPU consume/history/pins。发布或回调使事实失效时，一次关闭优化的原遍历补偿。原 source plan 的消费入口是 Pool collector，当前 shared-lazy 本来绕过该 collector，因此不能将 plan 不完整误称为必然增加一次 View/native 解析。这个候选默认OFF、retained OFF独立测，不将分段录制改变混进对照。

主机原真实 GPU 用例覆盖连续12次绘制跳过10次普通源解析、同ID更换成CPU发布资源、真正提交/完成/重开、状态恢复、nested负例与shader切换；原完整OFF/ON各23,780,782项检查通过。已有宽source-flow组合的失败在OFF/ON相同，单列而未改松断言。这些是功能资格，不是手机命中率或性能证据。APK SHA `33ed525a8d47a558f204a565bcefacdf1636e76db7b725652362d4ca16bd8a87`，ELF SHA `400566496479515d4806d16b0ecf2d26e74ce075b0c23ec639ddbf4791aea0bd`，原入口1043项实际源码和六库归档在 `.local/build/ordinary-source-forward-v1-final/`；游戏模块、f830、shader链、6217项资源与.60输出不变。

同包A1 OFF / B1 ON均PSO321、variants32稳定，原存档2次并停止、33项资产及属性恢复。各自同窗Running为26.560 / 26.237ms，Surface33.028 / 33.083ms；仅单对，没有稳定涨帧证据。已覆盖CPU指令区间59.576–60.000 / 58.809–59.234M，周期34.506–34.783 / 34.655–34.952M/Surface；OFF缺8.698385ms计数边界、覆盖99.86123%，ON缺0.128073ms、覆盖99.997923%，因此不能把covered-only约0.57–1.99%的指令差声称完整线程严格净界，周期区间也重叠。完整报告在 `.local/logs/ordinary-source-forward-device-v1/comparison-A1-B1/`，根已逐项核对B1原142项输入/输出哈希。

独立ON三份官方完整栈中，CPU source仍5.27–6.68%，image prepare/View8.52–9.72%，Buffer准备8.49–8.81%，绑定组装10.58–11.47%。新static body在多数样本中不再保留旧public父帧，已用实际符号和LLVM inline共同归类，不能靠旧名消失认领收益。新组资格可见0/3/5样本；循环和entry metadata仍在。OFF三份原采集已正式完成并原save2/stop，六窗对照继续离线收口；报告不以ON单边或重命名宣称整段准备已复用。目录 `.local/logs/ordinary-source-forward-callchains-v1/`。

六窗现已完成并由根逐项核对361项哈希，报告为该目录 `six-window-review/report.md`。全部正式窗PSO321/variants32稳定、lost/truncated0，OFF同样执行新static body。整30s source resolve+preflight份额3.554%→2.954%，控制residual1.890%→2.288%，显式组资格0→0.121%；总source6.139%→5.984%，不能将小差称稳定净收益。各自Surface重叠窗合并的source统计估计反而1.412→1.533ms，只有99/107个样本，抽样/时间窗边界均保留。View父桶8.740%→9.165%，但显式native消费/保活32→28样本，View内descriptor lookup10→7；没有支持新增native补解析的证据。原skip机制和正确性实验保留，不删除省工；同时承认循环/资格成本抵消大部分收益，不围绕小差再新增一套缓存或认领数毫秒。

### 标准 descriptor buffer 后继：查询和主机功能资格

对UAB剩余参数/更新管理，独立原型使用标准 `VK_EXT_descriptor_buffer`，不是私有descriptor字节或改写shader地址。主机原GPU图形/compute与跨提交secondary重放、两份值版本/ordinal、混合image/sampler/texel/UBO/SSBO、pending覆盖拒绝和原回退等3组通过；这不代表Android游戏功能或性能已通过。源码冻结 `ab172d69ce992db0f166add3409b6965a10044ca`，证据 `.local/logs/descriptor-buffer-host-contract-v1/`。

Q735独立cap-only v3在实际f830下完成标准能力查询：SHA、实际映射文件dev/inode及driver UUID均核对；descriptorBuffer/BDA=true，single-array=true，offset alignment64，UBO descriptor64、SSBO128、combined image sampler128字节。未创建VkDevice、Buffer或GPU工作，不能据此称手机GPU验证或认领收益。此前loader trampoline误判、v2目录准备错误及Java拒绝结果覆盖/退出顺序问题均保留原失败，修复后用独立v3 namespace，未覆盖旧结果。实际记录 `.local/logs/descriptor-buffer-capability-v3/device-query/`。后继仍需完整pipeline/layout、BDA分配/导入、texel原consumer地址交付和完成后arena覆盖合同，原资源准备不因此自动消失。

### 标准 descriptor buffer 已实际运行游戏，但仍有净额外工作

完整后继冻结在实验分支`a46e105`，标准descriptor buffer与retained仍默认OFF。canonical APK SHA `87d31114fa2d963a0f2158cc1d46ab452862507270c8e41ffac06bf86bbe958e`，ELF SHA `810d8ea115fbf9a30d5e3e2b4c5f82698d0d0d393b921df807c886aadb2b9504`，BuildID `062d925695cab7df7684f364fdcc49f1bee84630`。原入口1046项输入、六库与原UI冻结于 `.local/build/retained-descriptor-buffer-v1-final/`。原资源准备、CPU发布、hazard与保活未跳过；已完成context的独立descriptor arena替代UAB set更新，不修改在途字节。主机14项真实GPU与17项launcher检查通过，包含texel实际地址/格式、跨帧值改变、pending拒绝及整段失败回退。

真机在f830/950/6217/.60下进入实际ARM64游戏，营地角色、场景和原UI检查未见该画面的明显黑块/几何缺失；不代表战斗或全场景资格。无采样截图的HUD瞬时数不计稳态收益。独立PMU完整计数对照：ordinary A1指令58.681–59.106M、周期34.156–34.463M/Surface；稳定DB+retained B2指令63.628–64.071M、周期34.568–34.853M，分别多7.65–9.19%与0.305–2.040%。Running26.793→26.137ms但Surface33.126→34.441ms，驻核和频率改变，不能因Running小差就称净省工。B1的PSO/variants漂移保留；C1-UAB两次在正式测量前因配置/启动日志资格拒绝，没有有效UAB性能窗。原始在 `.local/logs/retained-descriptor-buffer-device-v1/`。

同一精确新ELF的ON/OFF各三份官方30s完整调用栈已完成：PSO/variants各自稳定、lost/truncated0，原save2后停止，全部正式工具结束。各自完整Surface重叠窗的互斥职责合并如下（统计估计，不是逐调用精确计时或随机配对因果差值）：

|交付职责|ordinary OFF ms/Surface|DB+retained ON ms/Surface|
|---|---:|---:|
|实际录制编码，含新路径首录/重录|2.561|1.383|
|普通descriptor物化|0.944|0.149|
|descriptor buffer及参数管理|0|2.142|
|pending和retained管理|0.043|1.071|
|以上互斥职责合计|3.548|4.746|

这解释了为什么录制减少却没降低整线程：可观察的编码与普通物化共少约1.97ms，新增管理超过它，交付合包反而多约1.20ms的采样估计。全线程其它桶、频率与驻核也变化，不能要求总Running等幅增加。原资源准备/绑定消费仍约8–9ms/Surface，不能把它们在未修改路径中的波动认领为准备复用收益。新路径中标准descriptor编码约2.12–2.20%、schema/layout约1.70–1.81%、校验/比较/typed交付约2.00–2.34%；不把这些全叫驱动锁。完整六窗与分类勘误保留于 `.local/logs/retained-descriptor-buffer-callchains-v1/six-window-review/`。独立证据审核逐项验证六窗及A1/B2共634项哈希与分母：差额描述性噪声尺度约0.348ms，不是置信区间；第一窗差额仅0.560ms，保留逐窗差异。复核记录 `.local/logs/retained-descriptor-buffer-evidence-audit-v1/review.md`。

其中确认一处可删除的工作：DB块不产生UAB更新，提交末尾却仍遍历所有schema/block执行UAB seal检查。独立修复`f90784b`仅将该遍历限制到UAB，保留公共serial、active与pending保护；12项实际主机GPU用例通过，Android性能尚未验证。新图该末尾职责约0.643%用户态采样、同窗约0.149ms估计，不冒称整个begin/end或数毫秒都可删除。

### 用现成API工具数真实复用覆盖：尚未取得新有效捕获

既有官方GFXReconstruct连续六帧已用于验证按command buffer分配寿命、recording generation、pool/reset/free和实际Execute/Submit展开的结构计数；不以首次replay日志当覆盖率。当前a46的新捕获在进入游戏前被共享导入资格门拒绝，未产生有效六帧：descriptor buffer启用后，现成抓取层会为BDA/descriptor replay增强对象flags，而本次f830外部host-import路径拒绝非零BufferCreate flags。已核对实际安装layer机器码与驱动源码；失败调用的完整实际参数未捕获，因此不把推断写成参数跟踪结论。

此次是抓取层与共享导入的工具兼容阻塞，不是无层游戏DB路径崩溃。原失败、进程退出与资产/层/属性恢复保留在 `.local/logs/retained-descriptor-buffer-six-api-v1/actual/`。正在独立修正现成GFXReconstruct工具的可选结构捕获模式，保留应用明确请求和官方编码/转换，不修改日常驱动或游戏，也不以工具中FPS认领性能。修复后的工具本身、资源replay资格与实际复用覆盖仍需分别验证。

### 连续六帧结构捕获已成功：大多数draw仍在新录制

独立官方280a工具后继`ee010f5`保留原编码/转换，只增加默认OFF的结构捕获选项，不自动增强BDA/descriptor capture-replay features与object flags。实际APK/ELF、源码和依赖指纹在 `.local/logs/gfxr-api-structure-layer-v1/ready.json`。第一次新工具启动因Android包可见性而`vkCreateInstance=-6`，未进入游戏，失败保留；相同工具APK用标准安装选项`--force-queryable`后可见，未修改a46游戏APK。原FrameAdvisor未被覆盖。

新独立事务 `.local/logs/retained-descriptor-buffer-six-api-structure-v2/actual-queryable/`已终态成功：实际工具模式WARNING与assisted选择均核实，原a46/f830/.60/950/6217、retained及DB ON、source OFF。预热到tick1004、PSO488/variants32稳定后触发连续六帧。原PID4308 fresh save commits=2后退出，再拉取524941006字节捕获；33资产、层和属性均恢复，手机私有及临时trace副本在哈希核对后删除。raw SHA `c052550f2b7f55fe47814af19ee72eb1fde451ac6090013fe0049b77913eb06d`，原官方converter的JSON SHA `79ef394eb1ccae9ac8dbc7b798ab50a69e570283130ec3df8104319d22e3b86e`。资源payload replay未验证，抓取中帧率不作性能证据。

原已校验的generation适配器处理这份官方JSON，按分配寿命、reset/free、recording generation与真正Submit展开，原遍历交叉核对通过、unknown为空。六帧各2391个实际执行draw，其中每帧1个primary；secondary共14340次draw执行，12868对应窗口内新录制、1280来自trim seed的首次观察执行、192是本窗已观察generation再次执行。即约89.735%仍对应窗内新录制；其余10.265%没有对应窗内新录制，其中seed原录制时间未知，不能将1472全部叫本窗已证明的重复执行，更不能把比例叫CPU节省率。尾部不完整帧单列不加入分母。结果在 `.local/logs/retained-descriptor-buffer-six-api-structure-v2/generation-analysis-queryable/`。

这把后继优先级进一步收窄：需要解释实际重录输入差异和准入/兼容合同，不能因首次出现replay日志就认领大多数绘制已复用。现有工具看得到实际编码、framebuffer/继承及descriptor绑定等变化，看不到运行时私有key/epoch的拒绝原因；在未证明变化可忽略前，不删除同步、生命周期或兼容性条件。

### 已完成 context 的 typed descriptor 更新：覆盖改善，尚无整线程净收益

独立后继 `7dc9a610` 包含前述 UAB 空 seal 遍历修复，并删除标准 descriptor-buffer 路径中沿袭 UAB 的一个过严条件：已初始化槽的非 Buffer 描述符只要内容改变就拒绝复用。标准 descriptor buffer 不要求已完成提交的独占槽永远保持旧 image/sampler/texel 内容；原 pending、当前 ordinal、schema/类型、arena 地址、完整参数校验、资源 consume/hazard/pins 与 object epoch 保护仍保留。UAB 路径未放宽，不能修改在途 descriptor 字节。原理依据是标准 host descriptor 更新在后继提交前完成的合同，而不是预测材质或场景不变。

主机实际 GPU 用例用一次 secondary 录制执行八次，固定两个 descriptor 地址，改变 image、sampler、texel 格式/范围与 UBO 值，结果正确；pending 覆盖和错误尾项拒绝用例通过。初始十四项与最终 fixture 输出调整后的三项复测分别归档，不混作同一 binary。冻结证据 `.local/logs/descriptor-buffer-completed-mutation-v1/`，独立合同审查 `.local/logs/descriptor-buffer-completed-mutation-review-v1/`。canonical APK SHA `19d1b0795bf08a110b8000f507c00251122ab1e855ec0b1ee80ff8c82f15ccbb`，ELF SHA `2036c254251b75c983a135a58c94596473559ef777e67e358eb14c11140e75e2`，BuildID `085cf1f53eac752c446754f2d767b0825fe341d9`；原入口 1046 项源码、六库和原 UI 记录在 `.local/build/descriptor-buffer-completed-mutation-v1-final/`。真实游戏无抓帧营地画面检查通过该场景资格，原进程 fresh save=2 后退出；启动截图 HUD 数字不计稳态收益。

同一新 APK 的 ordinary OFF / DB+retained ON / ON / OFF 四臂已完成，f830/950/6217/.60、source OFF 不变。每臂原存档两次后停止，资产和工具设置恢复。完整 Surface 重叠窗结果如下：

|臂|用户指令 M/Surface|用户周期 M/Surface|Running ms/Surface|Surface 均值 ms|
|---|---:|---:|---:|---:|
|A1 OFF|59.198–59.620，covered-only|35.419–35.718，covered-only|26.126|33.052|
|B1 ON|62.775–63.216|35.037–35.369|25.818|34.366|
|B2 ON|61.867–62.311|34.896–35.265|24.802|34.617|
|A2 OFF|59.873–60.297，covered-only|35.032–35.319，covered-only|26.415|33.003|

A1 缺 CPU0 的 0.251094ms 计数边界，覆盖99.995945%；A2 缺 CPU1 的0.243230ms，覆盖99.996098%。因此不能把 covered-only 算术差称为完整线程的严格上下界。B1/B2 为100%覆盖；频率和驻核不同，不能把 Running 降低直接解释成删掉 CPU 工作。A1/B1/B2 variants32，A2 variants30，各自稳定但第二对工作形状并不完全一致。ON 两臂 Surface 都更慢，目前未证明整线程净省工或涨帧；与历史 a46 的约64M指令比较也不是同包因果对照。独立报告 `.local/logs/descriptor-buffer-completed-mutation-device-v1/four-arm-independent-review/report.md`。

相同结构工具的后继六帧实际捕获成功，原PID23493 save=2/stop、资产/设置恢复。raw SHA `bdeb906a754b727d178f1d0aca76e15bf80ae9591228baea4d1a8aa779f76df2`，官方 JSON SHA `e7d8f164ff33b047f207c5934799bc64742b02ff106b44c9acb527741c031973`。相同 generation 展开资格通过，unknown为空；两版本均执行14346个 draw、其中14340个 secondary draw，pass300、dispatch90、barrier1050、submit48相同，但部分绑定序列和预热 PSO 不同，不能宣称逐调用工作完全一致。

新版本 secondary 中9604次对应窗内新录制、1664次是seed首次观察、3072次是本窗已观察generation再次执行。无对应窗内新录制覆盖由10.265%升到33.026%；真实 Draw API 录制数从12874降到9610，`vkGetDescriptorEXT` 从143163降到68460，BDA查询从92415降到72387。它们是工具证实的工作量变化，不是 CPU 时间或帧率收益。仍有约三分之二执行对应窗口内新录制，原资源准备也没有因 secondary 复用而自动跳过。

独立结构报告 `.local/logs/descriptor-buffer-completed-mutation-six-api-v1/independent-structure-review/report.md`。分析时一次派生输出路径错误暂时写入旧派生目录，原始捕获和官方 JSON 未改；旧输出已用原脚本/原输入恢复到原 SHA，旧22项封存全部再次核对，事故与恢复记录单列 `output-path-incident-recovery.json`，未修改旧 manifest 接受漂移。

下一步按新的精确 ELF 重新采集 ON/OFF 官方完整调用栈，分开核算省掉的录制和新增管理。对于 API/采样工具看不到的私有首个拒绝条件，单独编译默认OFF的 `DM_VULKAN_RETAINED_REASONS`：只累计现有分支原因，每60次原 guest 提交发布一次，不另写计时器或采样器，不将该诊断包用于性能结论。源码 `e426d2b`、主机 ON14/OFF2项和 canonical 构建已归档，实际手机原因尚待采集。生产默认行为及 retained/descriptor-buffer/source 实验默认OFF均未改变。

### 后继完整六窗与私有原因：区分录制收益和仍在执行的资源准备

7dc 精确 ELF 的 ON/OFF 各三份官方30秒完整调用栈现已结束，原进程各 fresh save=2 后停止。六窗PSO/variants端点稳定、lost/truncated=0；新linked调用点与LLVM inline重新核对，未复用旧a46 PC归属。原工具重复数据、官方HTML、逐窗频率/驻核及未知父栈封存在 `.local/logs/descriptor-buffer-completed-mutation-callchains-v1/six-window-review/`，根逐项核验336文件。

各自完整Surface重叠窗的统计估计：实际录制/重录 OFF3.009→ON1.575ms，普通descriptor物化1.032→0.045ms；两者共少约2.42ms。DB+参数新增1.649ms，packet+retained管理0.072→1.025ms，共多约2.60ms；互斥关联交付合计4.112→4.293ms，差+0.181ms小于描述性噪声尺度0.350ms（不是置信区间），不足判净正/负。Running26.113→26.196ms、Surface约33.004→34.398ms，没有涨帧证据。原资源准备/consumer约8.425→8.155ms；该路径没有被此改动跳过，其波动不认领为准备复用。与旧版本历史差额改善也不能当跨版本的干净因果收益。

随后单独安装 `e426d2b` 诊断包，确认无GPU层/采样工具。预热后guest serial1560→1680的120次原提交，4200次prefix chunk比较的互斥首因：1235次相同（29.405%）、1688次RenderPass句柄不同（40.190%）、1277次vertex Buffer或offset不同（30.405%）；其它首因均0。published capture2965、replay1235，本窗口未观察到任何forget/cleared/discard事件。此窗口没有支持“整池epoch重置导致主要重录”的证据，不应据此删除epoch保护。

这是首失败分布：pass失败后其它字段尚未比较，不能说这些chunk仅有pass差异；vertex也尚未细分Buffer和offset。分母是受限prefix chunk比较，不能当全游戏draw命中率或CPU时间占比，更不能据40%直接计算可省毫秒。下一步先核对Vulkan继承所需的真正RenderPass兼容性、以及已观察顶点绑定变化的来源，避免只是增加一层每draw判断。实际记录 `.local/logs/retained-private-reasons-device-v1/actual/`，严格差分 `private-reason-deltas.json`；原PID30675 fresh save=2后停止，33项资产与设置/属性不变。诊断后已恢复无诊断7dc APK并核对SHA，游戏保持停止，未把诊断运行作为FPS/热量测试。

独立合同复核现已完成，报告 `.local/logs/retained-private-reasons-actual-review-v1/report.md`，根逐项核验31个封存引用。另一六帧窗口中，同一CB寿命的136对重录有74对pass变化，实际为D16深度与RGBA8颜色切换，或HDR颜色+深度模板与纯深度模板切换，并非仅load/store变体；FB也不同。另62对同继承信息的重录全部为2750×1260、四顶点、stride20的绘制，真实VBO/offset改变；全部至少一个偏移差不能被20整除，不能直接改成firstVertex或忽略绑定。两窗口分母不同，不把这些比例套到4200次诊断决策。当前同pool更早完整命令序列也没有可直接替换的精确匹配，单换slot目录不能认领命中。

因此本轮没有可安全实施的“放宽pass/vertex比较”补丁。可研究的结构是稳定的pass角色与参数槽共同对应，或在完成context内把合格小几何内容交付到稳定地址；后者必须先证明实际源字节范围、CPU/GPU写入与同步合同，计入新增copy成本。它们都只可能提高编码复用，不自动消除仍约8ms的资源准备。当前没有已证数毫秒可删的新预算，不能把首因次数写成承诺收益；应把准备、编码、管理与GPU路径分别立账。
