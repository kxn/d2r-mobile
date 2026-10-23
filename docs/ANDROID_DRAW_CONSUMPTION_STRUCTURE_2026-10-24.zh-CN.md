# Android 绘制消费结构：整包复用与连续附件访问

目标仍是将高通翻译线程降至可支撑60FPS的运行时间。前轮组合复用已证明用户指令减少18%–19%、用户周期减少8%–10%，但Running仍约26.8ms；没有完成目标。本轮依据完整命令身份数据检查是否还能绕过整段准备，不把已完成的UBO、普通图像组、附件表示及PSO缓存再次认领。

## 整个上次draw并不是高覆盖复用单位

输入为同一次成功停止捕获的完整原命令、入口状态、shader/schema及资源身份：native-image-coverage-snapshot-v3，SHA `5bcbd37463289643d5c1e79fac98ca748bb1df36d9c6fe3bd8c7c18ca893ff6b`、ELF `1720393e8c4f316530ba795a6a65b0723be3647a`。45,257条OP、2,445个draw、21个handle，487页验证及完整19,800字节入口S。它是历史同暂停的逻辑身份，不是当前运行时命中率或同步合法性证明。

保守边界内2,387对相邻draw，固定部分相同、只允许只读UBO地址变化的仅144对（约6.0%）；没有固定部分及UBO也完全相同的相邻对。2,243对含其它变化，其中2,241对有几何变化。最长连续整包组11个draw；不相邻的旧包复访约500次，不能据此直接引入多项缓存：物理owner、writer、CPU效果、shader变体、录制代数仍须兑现。

另外1,365对仅几何与允许的UBO地址变化，其余部分相同；加上144对，除几何与只读UBO地址之外稳定的共1,509/2,387（63.22%）。几何分类存在重叠：使用中的vertex地址变化1,845次、range size变化875次、index地址变化1,411次，vertex格式和stream约百余次；索引类型、primitive、basevertex与instance参数同段相邻变化均为0，允许basevertex作为动态参数也不改变144的结果。这说明应区分固定产物与每次实际变化的几何，而不是让一项地址变动拖累整包重建；同时也说明不能简单跳过每次真实Buffer消费。

最终审计清理了bitfield未定义高位，RT按捕获serial和有效view比较，并以VS实际输入槽再次计算，三种口径的相邻结果一致。23个实际使用RT中3个未完整捕获，其保守指针身份保留，SET_RT均切硬段；不以这些未知对象证明物理复用。数据还包含675个已有specialization模块和32个pixel-local候选draw，当前逻辑覆盖不是动态变体选择资格。

原代码已经让UBO计划与普通FS图像组共用prepared_values并合并skip mask，附件借原job数组，PSO命中跳完整key/hash。只把四者包成一个新结构不会再省这些工作。完整解析、逐draw状态差分及边界见 `.local/logs/draw-packet-coverage-v1/`，各后续审计以目录最终冻结报告为准，不能用本段静态次数预测毫秒。

## 选择连续附件访问作为更深的一层实验

现有附件复用仅借用表示，热draw仍执行attachment_access的owner/range恢复、规范化请求构造，然后做依赖查询及访问历史记录。相同实际Render Pass中的固定功能访问，现有dependency代码本来就有raster-pass豁免；寻找这个豁免本身仍有逐项工作。

受限候选让原job持有固定的规范化附件请求，首次真实draw完整执行依赖及记录，后续相同角色的连续访问作为一个run消费。进一步尝试按原事件位置推进精确逻辑clock与generation，把成员字段的重复散写延迟到观察前完成；不是忽略写历史或只保存第一draw。

开始实现前的合同要求：普通非native、不同物理附件owner、同实际pass与固定range/角色；storage writer、query/ghost/局部渲染及未知边界保守排除。原history包括预清与最终record的generation递增、每image use与每cell write时钟、reader清理、layout visibility、zero、readproof、shared alias发布，均须保持等价。

闭包不能只放在image_get/dependency_source：layout whole/range入口会更早读cell，mip admission直接查bucket和alias clock，pixel-local与readproof有直接观察，render_end须在ghost及STORE之前物化。相反，不相交UBO的shared-linear访问不应无条件关闭run，否则会使高频动态几何/常量重新破坏整段收益。具体入口、所有权及字段审计见 `.local/logs/draw-packet-contract-v1/`。

独立工作树 `.local/worktrees/attachment-raster-run-v1`，基线 `c30662c`，默认关闭，尚在实现；没有Android部署或性能结论。验收须包含完整CPU可见image/cell/clock/alias/zero状态的ON/OFF差分，以及中途采样、清屏、barrier、mip/readproof、pixel-local、ghost和角色变化，不只比较最终像素。若完整闭包不能成立，则保留待修实验，不能以少同步换帧率。

### 首批实际路径验证与保留的失败

初版完整fixture两臂exit0，却没有新路径计数；v2/v3的统计曾错误地放在原测试 `_exit(0)` 之后，不可达。这些结果只证明旧回归，没有当作run验收。v4把实际门放到退出前，ON捕获1103次、hot3次、scatter3次、原算法状态oracle3次，OFF全0；两臂完整suite通过。该覆盖仍薄，因此继续短专项，没有用旧两千多万检查数冒充新路径覆盖。

targeted-v3的8项同包主机ON/OFF已退出0。连续8draw实际hot7次；中间真实NVN barrier后hot6次/两次scatter；扩大scissor仍被现有job包含，hot7次，但不能称为换pass验证；第5draw前真实render_end callback使hot6次并分两段。另有hot之后直接readproof/cell观察、cold交错请求拒绝、依赖函数已借旧请求指针后强制填满并扩容的负控；像素与完整原history差分通过。关闭组hot及oracle为0。

独审指出冷请求中若附件后还有普通read，则按首附件use_clock推stride会错计其它read时钟。实现已加冷资格拒绝，热路径不再扫描；targeted-v2仍真实失败于交错负例，因为测试没有先物化既有FS借组，未产生预期的真实交错。修正触发后targeted-v3保留原严格“该case不能hot”断言并通过，没有删掉负控。参考输出为 `.local/logs/attachment-raster-run-v1/{v4-results.json,targeted-v3-results.json}`，独审为同目录`independent-review/`。

这些结果尚未覆盖最终新增DS/平台启动桥，也不代替Android原生游戏或性能结果。候选未冻结、未合并，下一步为完整差分覆盖、最终源码独审及canonical平台构建；不会把主机频度当真机运行频度。

## 已排除的周期猜测

额外检查了旧ADPF：同一metadata区间reports增加250、guest submissions也增加250；ON trace中replay238、present239，两轮OFF为239/239。physical fence编号的步长不能当guest submission数量，因此“三个小任务各按一整帧预算汇报”不是该营地的已证错误。没有按这个猜测改ADPF或重新采集，证据为 `.local/logs/adpf-work-period-audit-v1/`。一般多guest/frame的接口契约仍可改进，但不能拿它解释本场景。

本轮未操作暂停的Mali或平板，也没有为静态覆盖检查启动手机。主线没有加入整包缓存；新附件run仅作为隔离实验推进。直到真机同窗CPU、指令/周期和实际出图得到验证之前，不声称已提升帧率或达到60FPS。

## 后续真机四轮：重复工作成立，尚未缩短线程运行时间

本节更新前面的“尚在实现／未部署”状态。候选已冻结为独立分支 `761b4b8e9b5f5f4d756903cb49396b9a977ac535`，仍默认关闭、未合并运行时代码。最终同一主机二进制完成10个实际专项／完整／共享回归进程，均退出0；专项包含原完整访问历史差分及像素验证。Android另经canonical构建、原始NSO重新生成及verify-only；新生成24项执行／规则文件与已部署原记录一致，部署保留原有已验证模块与provenance，没有叠改游戏二进制。

高通Q735实际安装同一个候选APK，固定0.60、2750×1260输出、同Turnip／native资源／shared heap，依次OFF、ON、ON、OFF。原三项组合复用保持ON；仅新附件run改变。四轮Simpleperf指令组读与原Perfetto同窗，原dump与ReportLib逐项一致，零丢失／截栈，trace health无异常。

| 轮次 | 工具窗完整Surface | NvN Running ms/Surface | 可用用户指令 M/Surface | 用户周期 M/Surface |
| --- | ---: | ---: | ---: | ---: |
| A1 OFF | 236 | 26.484 | 60.551–60.975 | 35.436–35.707 |
| B1 ON | 237 | 26.342 | 57.689–58.178（机械裁剪225区间） | 33.628–33.976（同裁剪） |
| B2 ON | 235 | 26.815 | 57.447–57.872 | 33.915–34.203 |
| A2 OFF | 235 | 26.431 | 已覆盖CPU子集60.681–61.106 | 已覆盖CPU子集35.728–36.009 |

B1原完整窗仅覆盖89.886% Running计数，不用它的52.6M子集伪称省工；原机械裁剪后计数覆盖100%，裁剪Running为26.504ms。A2极短CPU1驻留无counter，覆盖99.99731%，仍没有整TID有限上界；未将缺口补零。A1、B2完整计数覆盖100%。故A1→B1可证明指令减少3.92%–5.39%、用户周期至少减少4.12%；A2→B2仍可证明指令至少减少4.63%、周期至少减少4.27%，不能给减少比例上界。有效窗Running分别变化+0.0202／+0.3839ms，没有稳定缩短，更未证明FPS收益。ON运行加权频率约1440MHz，OFF约1502／1524MHz；不按MHz归一，不据此声称原因已是调频。

独立宽状态窗按自己的原guest submit归一：两次ON每guest约2397／2395个draw，新路径hot约2343／2342次，cold capture约28次，scatter约11次，测量段expand为0；OFF全0。说明实测确实压缩了高频重复附件准备，不是只看源码或first-hit。该宽窗不是工具窗，不能用其调用次数除内层Surface数；热覆盖也不等于CPU节省比例。四轮均原Android生命周期新存档2次、停止进程并清理采集工具。

### 关于“重复准备到底占多少CPU时间”

需要分开调用频度、完整职责时间与可删除子集。本轮上述record采用 `instructions:u` 周期组读，没有调用栈；它能验证整线程指令／周期净减少，不能恢复完整CPU时间父链，也不能把指令样本比例叫时间比例。

已有较早source-partition OFF的两份官方cpu-clock调用栈中，附件实际访问resolver inclusive为1.73%–2.39%；所有图像的请求构造为0.67%–0.78%、依赖处理1.55%–1.73%、draw history为4.44%–4.92%。各inclusive行有父子重叠，不能相加；后面三项还服务native采样等其它资源，不能全归入可复用附件。旧附件总职责5.6%–6.5%同样不是当前可省份额，更不能乘当前26ms得到候选收益。可核查互斥分配、原样本／ELF哈希在 `.local/logs/attachment-raster-device-v1/prior-callchain-budget.{md,json}`。

当前结论仅是：本候选确实去掉大量相同附件的重复解析／请求／依赖查找／历史散写，整线程指令和用户周期净减少约数个百分点；尚未找到能解释十多毫秒差额的大块可删除工作。为给当前剩余职责排优先级，正在复用原官方Simpleperf三次30秒cpu-clock／FP调用栈与Perfetto流程补这个缺口，不新增自定义计时器。四轮原始与分析证据为 `.local/logs/attachment-raster-device-v1/`，独立分析总表为 `four-arm-review.{md,json}`。新调用栈结论须按实际完成状态另记，不能拿旧PC套新ELF。

### 当前版本调用栈缺口已补齐

原工具三次30秒 `cpu-clock:u` 99Hz／FP已实际完成，均同PID21592、同761b4b8 ELF，raster OFF。NvN分别有2254／2255／2216个样本，原record零丢失／截栈；精确BuildID、原数据与ELF哈希已核验。每个真实保留调用栈只归入一个宏观职责，完整样本数和event period分区和分别吻合；不合成尾调用父链。它衡量用户态CPU时间采样比例，不能当逐调用精确计时或kernel／GPU时间。

| 可识别职责（互斥切分） | 三次采样占NvN用户CPU时间的范围 |
| --- | ---: |
| 附件准备／表示 | 1.13%–1.33% |
| 附件实际访问解析，扣除下面公共子项 | 0.80%–1.91% |
| 请求构造，所有图像 | 0.49%–0.77% |
| 依赖处理，所有图像 | 1.51%–1.81% |
| draw访问历史，所有图像 | 3.99%–4.42% |

这五项已去父子重叠，逐窗合计8.29%–9.23%，但绝不是可省份额：公共请求／依赖／history还服务采样、storage和native图像；调用栈不带对象身份。明确native后代已单独标记为不合格，其余未知类型不能自动归为附件。本次variants稳定32，四轮对照主要30；不把独立窗口相乘制造精确每帧毫秒，也不将OFF采样称作ON剩余预算。表中范围是三次观测范围，非统计置信区间。

宏观互斥分区另显示：图像准备／View为8.21%–9.25%、Buffer Pool／依赖／plan为8.08%–8.87%、尚未归入前述子项的绑定装配为9.09%–10.06%、CPU source为5.46%–6.83%；剩余draw桶仍22.52%–24.84%，是尚需下钻的混合职责，不是现成可删阶段。研究优先继续落在完整资源消费、绑定与剩余draw的结构性工作，而不是凭98%左右热覆盖把附件优化宣传成能删98%的CPU。当前附件机制保留为独立已验证省工候选，尚未证明时间／FPS收益。

最终CPU归因在 `.local/logs/attachment-raster-device-v1/current-off-callchain-analysis/attribution.{md,json}`，逐栈分配及原ReportLib导出同目录；原工具采集与独立OFF画面在 `current-off-callchain/`。另外完成一次独立ON营地截图，未见明显黑块／几何缺失，仅代表该画面；不使用截图即时HUD数值证明性能。两次检查均原生命周期新存档2次并停止。最后恢复日常08b APK并核对原执行文件，未操作Mali或平板。

## 已合入并默认开启；剩余draw的宏观判断已修正

主线 `b05c49b` 合入受测附件run，`5bfa5c0` 将Java、JNI和后端冷启动默认统一ON，保留明确OFF。该优化不是因为未即时涨帧而撤回。独立包源 `1bb086a` 的实际运行源码与主线一致，canonical APK为 `b8f4020f77d9c82de22dd51ba2a5ed0a09c1a14531402b7412c2c090ad536f82`。原始ExeFS另经完整重写及verify-only，24项执行／规则哈希仍同原部署；六库、私有include、签名、全UI词典、1340导出及527NVN入口已经核验。

真机默认不传run开关，PID26957进入原游戏营地并实际累计hot307623；显式OFF新PID27743的enabled=false且四项计数全零。两次均原生命周期新存档2次并停止。证据为 `.local/logs/attachment-raster-default-on-device-v1/{default,off}/`；这只是默认桥接与基本真机资格，不追加FPS收益或持续游玩结论。

进一步将旧OFF采样的remaining draw 22.52%–24.84%逐栈拆分：附件角色／访问／pass为3.81%–5.01%、几何1.90%–2.17%、shader变体及可选策略2.66%–3.82%、实际绑定／状态交付3.20%–3.59%、consumer作用域／准入1.81%–2.13%，其余分散于物理span、PSO上下文、query和仍未知的3.15%–3.34%。它不是一个20%以上尚未缓存的大循环，不能以该父桶预算启动又一套缓存。

精确新ELF反汇编还核对了 `vkCmdBindPipeline`、绑定入口及PSO入口的实际调用点：部分memmove/memcmp在API调用范围内，中间驱动／helper尾调用框消失，不能误叫宿主临时结构初始化。没有补造驱动栈。原始source／ELF／逐栈归属与调用点证据为 `.local/logs/remaining-draw-761-drill-v1/`。

native资源加入现有FS只读组亦降级：有明确native消费者祖先的样本并集仅0.84%–1.44%，非全部native成本上界；已有per-recording stage/page登记去重。直接放开资格还会让变化的native材质槽失效整个普通纹理组，破坏当前约6400–6500 skipped items/guest。避免此问题需要独立子集及Pool保活／GPU别名写事件合同，不能只改eligible条件；当前没有高收益预算支持实施。合同及互斥成本在 `.local/logs/native-read-group-budget-v1/`。

### 必要代码执行成本的独立实验

并行完成两TU `-Os` 候选 `70ccdfd`：仅 `nvn_vk_draw.c`、`nvn_vk_shader.c` 追加源级选项，其余135项宿主编译政策核对不变，浮点规则和游戏逻辑不变，13个builtin SPIR-V逐字节相同。10个主机回归及15项launcher测试通过，canonical APK和原始NSO重写／verify完成；只在独立分支保留构建开关，主线仍O3。

本次实际两个对象text为165332→131996B，减20.16%；完整linked host text仅2840076→2806772B，减1.17%。这些静态尺寸不能推为动态指令、取指等待或时间收益。现有前端PMU压力提供实验依据，不证明当前瓶颈就是代码体积；原PGO、冷路径外提和布局研究的无稳定收益结果仍保留。实际两APK O3／Os／Os／O3四轮正在沿原Simpleperf组读与Perfetto同窗执行，首O3为Running26.488ms／Surface；最终结论待反序对照完成，不能因编译成功宣称有效。
