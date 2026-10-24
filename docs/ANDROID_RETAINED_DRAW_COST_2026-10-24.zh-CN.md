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
