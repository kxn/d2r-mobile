# 翻译线程：按整条调用链安排优化

固定绑定 schema、图像请求阶段边界、首次 walk 融合 store index 和构造阶段外提已完成真机对照，尚未证明整帧 CPU 或 FPS 净收益。整池共享对照和完整2,838项 ASTC6 资源的原游戏短测也已完成。已完成共享路径中的旧 Pool 扫描、比较、镜像上传计数为零；不能继续把该路径的 CPU 时间归因于已经消失的拷贝。现有证据不支持阻塞锁是主要原因。

测量验证现已完成：同一74二进制进行了3次DWARF和3次FP独立采样，主要职责稳定，后文保留各阶段原始结果。74组合最初的原游戏短测中，NvN Running约27.20ms/完整Surface间隔，仍约30FPS；没有证明此前微调兑现成整线程收益。现有8秒cpu-clock记录只有621个NvN样本，其中124个缺少完整外层调用链：70个停在f830驱动没有FDE覆盖的PC，46个可能超过原16KiB栈保留范围，8个映射失败。零丢样本不等于调用归因完整。

已固定实际74二进制、同一PID、营地位置、0.60比例、资源及驱动，使用原Simpleperf连续3个30秒99Hz窗口，各自生成官方火焰图。三个新窗口统一保留49152B栈及失败展开调试信息（设备自带Simpleperf严格拒绝65528B；48KiB覆盖此前实际观察到的栈范围缺口）；原16KiB普通入口不改，新旧采样方法不能直接当严格A/A。扩大栈不能修复驱动缺失的展开信息，不补造缺失父节点。每窗同时运行原8秒Perfetto，另取采样前后Perfetto单独窗口，以官方时钟转换证明重叠，并比较采样扰动。

宏观判断先区分四件事：同工作量实际CPU时间是否下降；运行核心/频率是否改变；完整调用路径是否稳定；出图间隔是否受呈现节奏影响。样本比例减少不等于绝对工作减少，频率变化也不能只做线性归一化。相同代码多次结果的观察范围应先于候选收益结论；按完整资源绑定、准入、规划、驱动及其他应用线程归类，保留未归属部分，避免重复相加inclusive子树。现有全应用累计CPU约55ms/呈现间隔，其中NvN约27ms、其他线程约28ms；这些是多核累计CPU，不是55ms关键路径。

同74/PID11031的三轮A/A已经完成，并分别生成官方Inferno及HTML调用树；官方MONOTONIC转换证实三个Perfetto窗口都完整位于各自采样内。三窗PSO184、variants33、位置及比例保持一致，没有修改运行时。样本为2282/2272/2291；看见完整x_thread祖先的比例87.29%/87.46%/87.60%，新记录中没有上沿栈截断病例。仍约11.3%的样本停在没有FDE的driver代码，不能补归父节点。

| 同代码重复窗口 | 1 | 2 | 3 |
|---|---:|---:|---:|
| 宿主叶PC比例 | 76.77% | 77.64% | 77.35% |
| 可见draw子树 | 62.88% | 65.27% | 64.73% |
| 可见绑定子树（含所有子调用） | 19.68% | 22.27% | 21.65% |
| 资源准入子树 | 7.80% | 7.88% | 7.86% |
| 规划子树 | 6.40% | 5.63% | 6.11% |
| 同步Perfetto整NvN CPU/完整Surface间隔 | 27.15ms | 27.33ms | 26.86ms |

宏观热点没有出现完全相反的结论；小叶节点的几个样本不应据此排实施优先级。采样关闭的前后两个Perfetto窗口，NvN CPU却从25.95ms变到27.70ms，相差6.74%；全应用累计CPU从50.02ms到53.93ms，约7.82%。书挡自身有漂移，不能将ON/OFF差额全部解释为采样成本，也不能把历史单次几个百分点的变化认定为候选收益。位置相同也不代表动态场景每次工作完全相同。下一步以同binary独立FP敏感性采样补足driver调用来源；不先重建驱动或继续叶子微调。证据：`.local/logs/native-fast-74e4050-AA-v2-root-findings.json`。原游戏已按生命周期存档、退出并恢复原包，cleanup为空。

后续同一74二进制的官方FP敏感性实验也已完成，三窗2305/2322/2325个NvN样本全部展开到x_thread与pthread；无需重建驱动或伪造CFI。FP本身仍可能遗漏叶帧、尾调用或内联信息，入口可见不等于所有中间帧完全恢复。三个独立官方调用图的draw子树73.67%/74.68%/75.31%、绑定子树20.26%/21.45%/22.15%、准入8.07%/8.44%/8.52%、规划6.42%/5.68%/4.90%。实际同步Perfetto的整NvN CPU仍为26.99/27.67/27.26ms/完整Surface间隔。因此宏观CPU工作并非由DWARF缺栈凭空造成，旧测量缺口修正后，大方向仍稳定。

FP与DWARF是独立运行，位置有1px差且PSO185与184不同，不把跨方法差值当准确采样开销AB，也不将30秒样本比例乘另一个8秒CPU窗口伪造职责耗时。当前优先调查完整draw内资源消费/准入/状态交付能否消除整轮重复；原有pipeline、descriptor set和dynamic state缓存已经存在，不能重复建议再加同类缓存。之前元数据解码、无效遍历等减少仍保留，但不能据此宣称已消除整段20%绑定工作。实际driver caller中PSO/set/draw/vertex记录合计约6.6%～7.9%属于draw实现，另有约1.3%～1.7%属于descriptor创建，不能全算同一个绑定热点。Mali及A650设备保持暂停。证据：`.local/logs/native-fast-74e4050-AA-FP-v1-root-findings.json`。

当前结构候选为默认关闭的 coherent 共享按需窗口，尚未进行 Android 原游戏验收，也没有提速结论。每次实际 Buffer 消费才导入需要的窗口；跨旧窗口的范围新增覆盖别名，既有 Buffer 和起点不变，原有 Pool 生命周期结束后才释放。这样才有条件省掉每 draw 的完整资源预枚举，而不是再增加一层消费缓存。所有真实页/阶段/写入记录、CPU 可见性和资源引用保留。别名模式从启动起禁用基于单个 VkBuffer 的可见性证明，采用精确阶段的全局内存依赖；HOST_COHERENT 不代表可以省掉同步。

实验只允许已验证的 f830、通过启动双向重叠字节检查的 coherent 属性和完整已提交共享 Pool。导入失败、未知范围超预算或不兼容全池参考模式明确中止候选，不偷偷切回镜像。未知长度顶点绑定仍要保守覆盖其合法可用范围。分别记录实际覆盖并集、累计导入量、节点数和重叠节点，驱动可能对重复导入重复计费，不能声称零内存成本。真实 GPU 内容/异步别名检查完成后，冻结源码，经原 Android 入口和原始 NSO 重建，再以同一 APK 的开关交错对照；检查各独立窗口后再综合，不能只挑最好的一次。

准备阶段已拒绝两个重复方案：prepare 到 descriptor 的再次传递已有 source_flow/recipe，native GENERAL prepare 已是无操作；准入到消费的几何交付已有 operation geometry/recording 借用。它们不能省掉全部约20%绑定工作，继续加表只会增加失效与生命周期成本。只对具备可证明工作减少的完整路径实施实验。

## 2026-10-22：完整职责预算与首个按需窗口真机结果

新同一APK `a31a0f0` 的 OFF1 已完成3×30秒FP、5×8秒Perfetto、三段原输入、存档2次、退出及原包恢复。三个窗口 NvN 2292/2340/2294个用户态样本全部保留x_thread祖先。五个宏观窗口的 NvN Running/完整Surface间隔为27.55～28.14ms，Surface平均约33.2ms；位置和PSO185/variants33稳定。排队就绪仅约0.16～0.21ms/间隔，不支持把大部分实跑时间归因于等锁/抢不到CPU。睡眠约4.9～5.4ms与CPU实跑分开看。其他APP累计CPU仍约25～27ms/间隔，跨核并行，不能与NvN相加成帧延迟。

完整draw只作一个大类时约占72～75%；按互斥直属子树拆开后，绑定约21～22%、准入7.4～8.2%、CPU图像来源3.8～5.0%、绘制后图像历史3.4～4.6%、附件/Pass处理5.2～6.1%、图像prepare2.1～2.7%、依赖/请求批次约1.8%、VBO/index消费2.6～3.3%。draw self约4.0～4.6%，含内联状态/Key工作，不能伪分。实际驱动draw约2.8～4.2%、绑定PSO1.0～1.6%、绑定set1.2～1.8%；query仅约0.04～0.17%。各窗原始siblings及未归类孩子完整保留，不将这些比例与外层inclusive重叠相加。

改前预算与研究顺序：

| 方向 | 已有覆盖证据与预算边界 | 决策 |
|---|---|---|
| 取消不必要的整轮资源准入 | 目标整项约8%用户态样本；乐观只约2ms量级预算，实际消费者、引用及别名成本仍在 | 保留默认OFF实验，先修正确性，不能声称可单独达到60FPS |
| 改变每draw绑定表示与工作生成方式 | binding约21～22%中，self仅4.4～4.6%，真正descriptor创建整子树仅1.8～2.0%，其中driver update约1.1% | 优先研究长寿命资源表/每draw轻量参数等架构；不能以替换descriptor更新API就声称省21% |
| 相邻兼容draw批量交付 | 可能同时减少状态Key、绑定构造与驱动调用；但必须先证明同PSO/Pass/状态连续组的规模及合法数据依赖 | 未有可合并组数量和净收益预算前不写大方案；不重排透明/不透明、不改玩法 |
| 去除共享路径遗留上传账本职责 | record_pool_range整项约1.7～1.9%，明确属于旧upload分支的PC仅约0.31～0.38%；实际pages/stage依赖不能省 | 机制有价值但优先级低；不以整段pool_address或sync耗时作为可得收益 |
| 再做命令预解码/已有缓存 | NVN已录成typed p_*包，每Submission已有16B planned_op展开；dynamicUBO/schema/templates/set/history/source/recipe缓存均存在 | 不重复实现，不将大类占比当成缺失功能 |

从27ms到16.7ms需要约38%的时间减少。当前没有证据证明仍有一个可删除的30～40%重复工作坑；上述是职责包络和研究预算，不是已经兑现的收益，也不是可直接相加的百分比。对更大架构先给覆盖量、必须保留的语义、新增GPU/RAM成本及实施规模，再安排代码。

ON1在加载约16秒时触发 `image_bc_copy_decode` 的既有保护：`Mixed compressed copy requires observed BC3 storage with no defined alias`，没有得到任何可比较的ON Profile。原APK/资源已恢复，cleanup为空，根核pidof为空；ON2/OFF2不执行。源审发现T2T的原copy/retile范围准备位于ASTC观察之前，观察要求完整已物化shared backing；跳过通用准入会使这项前置条件缺失。保护条件的具体子项没有逐项日志，不能将源码因果解释伪称已经逐项测量。后继需保留实际拷贝准备/观察，只在完整消费者合同成立的绘制路径取消前置遍历；不能关闭保护、改资源或游戏binary来通过。实验仍在独立分支，未默认启用或合入稳定主线。

本机证据：`shared-lazy-a31a0f0-OFF1-FP-Q735-v1-analysis/wholepath/source-review.json`、`shared-lazy-a31a0f0-OFF-ON-macro-v1/OFF1/run-facts.json`、`shared-lazy-a31a0f0-ON1-root-failure-review.json`（均位于`.local/logs/`）。

以下原实施顺序保留作历史记录，新的具体结构方案按上述完整路径继续调查：

1. 保持1MiB有限窗口，缩减每draw真实资源准入和消费的重复工作。统一版本179c82d复用固定绑定元数据、跳过coherent native分配的无效cache遍历、借用当前recording已有的Pool引用；跨commit前升级独立pin，保留全部物理覆盖与数据依赖。host内容/重入/异步/负控、canonical/fresh raw28及实际游戏短测均完成，未证明净CPU/FPS收益。宿主ThinLTO已完成相同资源的原游戏对照：整线程Running约27.36ms vs27.51ms、频率约1577 vs1583MHz，仍约30FPS。编译选项保留为默认关闭的实验入口，不继续以编译微调为主线。
2. 完整ASTC6混合包已进入原游戏并完成三段跑动、存档和恢复；全6217 native payload从443,588,608降到373,624,832B，减少66.72MiB。实际营地NvN Running约26.68ms/完整Surface间隔，出图约33.52ms，未证明净FPS或进程PSS下降。保留资源表示收益，继续测实际加载范围与持续成本，不把资源大小当DRAM或温度。
3. 进一步评估是否能把Submission/可证明片段中的元数据和几何准入一次交付给多个draw。先查已有walk、source_flow、geometry和recipe边界，不盲加跨draw缓存；低覆盖的分类删除不足以将约27ms直接压到16.7ms。native GENERAL与同值纹理注册免全局失效已合成2fc55c2，完成canonical/raw28、原游戏三段移动、save2与恢复并进入主线。仅减少可证明的元数据/准备工作，实际资源消费和依赖仍执行；未证明净CPU/FPS收益。
4. A650独立查GPU故障，保留1MiB，不切回整池或随机改Shader。现有同run命令已完整解码，描述符范围和对齐未见明确错误；缺输入payload时不推断动态值。Mali依用户要求暂停，帧回调和翻译多线程暂不实施。

整池实验的明确代价是额外739.78MiB PSS，NvN Running约25.75ms vs27.05ms，仍约30FPS；不能把约8%的准入子树视为无成本可删除，更不能作为6GB默认。以下保留各轮原始判断、实施和对照记录。

## 已有官方数据的结论

8480a53、同一 6217 资源、固定 0.60 比例、2750×1260 输出。原 Simpleperf 8 秒记录保留 605 个翻译线程用户态样本，完整保留实际展开结果，没有补造缺失的祖先。按可见调用路径将每个样本只分入一类：

| 职责 | 用户态样本比例 | 当前处理 |
| --- | ---: | --- |
| 描述符绑定构造和真实资源消费 | 23.14% | 固定 schema 提前整理；进一步研究表示复用和实时访问效果分离 |
| 操作资源准入 | 9.42% | 保留生命周期 pin；不把少数重复边界判断当成整项节省 |
| CPU 图像来源准备 | 4.96% | 已有操作内来源传递及 id/epoch 复用，继续复用现有机制 |
| 物理图像准备 | 2.31% | 已有 recipe 和 trivial 提前返回，不另造重复缓存 |
| 绘制后图像访问历史 | 4.30% | 消费追加时已知的读写阶段边界，减少重复过滤 |

以上五类并集为 44.13%，包含必须保留的访问、同步、引用和布局语义，**不是可删除的 44.13%**。提交规划 `mtl_sub_index` 另占约 5.95%，也包含必须执行的工作。驱动、memcpy、锁等底层占比已经包含在调用者中，不能再相加。

另一个 Perfetto 窗口里，同一 234 个完整 Surface 间隔中，整条翻译线程 Running 平均约 27.78ms，replay 内 Running 约 26.64ms。replay 内 Sleep 约 0.96ms/间隔；已标记的队列锁总区间约 0.014ms/间隔。后者不能证明所有资源锁均无竞争，但不支持把剩余二十多毫秒解释成主要在睡眠等锁。不同工具窗口不相乘来伪造精确的子任务毫秒。

## 最初的优先级与成立条件

1. **绑定表示与实际资源消费分离。** 原 Metal 可以依据 stage dirty、shader 和 pass 复用 argument block；Vulkan 当前在最终描述符缓存命中前仍经过完整资源消费和 values 构造。先将固定类型、寄存器、源偏移和动态槽位整理到每个最终 shader schema。只缓存固定元数据，不缓存 GPU ready、布局或脏页结论；当前实际状态仍逐次读取。该第一步不减少绑定数量或实际资源消费次数，不能声称消除了整个 23.14%。
2. **操作内的结果直接交给下一阶段。** 图像请求追加时维护“只读前缀、写入后缀”是否成立，成立时绘制后历史遍历从 `3N` 降到 `N+W`；交错请求保持原三遍算法，所有实际历史更新及原顺序保留。新增总计 128B 批次元数据，单请求大小不变。这一步与既有来源/物理 recipe 复用配合，不添加跨绘制的动态依赖预测。
3. **合并提交规划的整次遍历。** 研究将现有 forward store index 融入原 walk，保留 reverse 深度/查询分析、别名判断及原失败行为。不能按命令 handle 跨提交缓存：handle 是可重录的内存地址，嵌套 CALL/JUMP 目标同样可能变化，缺少完整 generation 证明。
4. **低覆盖叶子优化后置。** 独立的重复边界判断删除只明确覆盖 2/605 个样本，约 0.33%。原理成立可以保留候选，但不抢占以上任务的构建和设备验证。

## 实施与证据边界

固定 schema 候选 `d4e22c9` 与图像阶段候选 `d31e931` 已源审，并组成 `b16184c`。组合桌面 Vulkan 检查通过，包含串行/异步、真实资源内容、重入、替代 schema 和图像升级路径；这不是 Android 游戏验收。Android canonical 和原始 NSO 经原工具 fresh rewrite/verify28 正在单独构建，之后才进行真机同资源、同比例的官方工具对照。

新的 8480a53 原游戏运行已完成三段原输入路径、官方 Simpleperf stat/record、两个独立 PMU stat 窗口和 Perfetto，已存档、退出并恢复原包。PMU 原始结果可用，但 CSV 未包含 enabled/running 时间，不能据此保证完全没有 multiplex；通用 cache 计数也不能冒充 DRAM 字节数。它们不构成候选已经减少整帧 CPU、涨帧或降温的证明。

本机证据：

- `.local/logs/q735-8480-cpu-flamegraphs-v1/high-level-analysis-root.json`
- `.local/logs/actual8480-wholepath-structural-review-v1/result.json`
- `.local/logs/native-official-cpu-pmu-7da96c6-Q735-v1/root-pmu-review.json`
- `.local/logs/shader-binding-consumption-d4e22c9-root-source-review.json`
- `.local/logs/operation-image-phase-artifact-d31e931-root-source-review.json`

Mali 依用户要求暂停。A650 平板独立处理实际 GPU fault：已在同一次 EPROTO 停点导出故障 IB 所在 BO，设备调试器与游戏已撤场，后续使用原 Mesa cffdump 离线分析；不与主线高通的 CPU 候选混测。

## 实际实施结果

三项现均完成，合入主线并备份：固定绑定 schema、图像请求阶段边界、首次 walk 融合 store index。前两项组合为 `9592893`，全部三项组合为 `d14808f`。两个版本分别完成 Android 原游戏进入营地、三段原摇杆跑动、原生命周期存档退出及官方 Simpleperf/Perfetto 采集；实际截图未见营地大黑块或明显几何错误。没有改动资源、驱动、分辨率、游戏逻辑或 Shader 运算来制造提升。

相邻运行保持同一 f830、6217 资源、固定 0.60 和 2750×1260。下表每一行均使用该次 Perfetto 中同一完整游戏 Surface 区间内的 Running，而不是将另一次 Simpleperf 的百分比换算为毫秒：

| 运行 | 完整 Surface 间隔 | 翻译线程 Running / 间隔 | 实际运行时加权频率 | 平均出图间隔 |
| --- | ---: | ---: | ---: | ---: |
| 8480 对照 A | 238 | 26.89ms | 1573.50MHz | 32.90ms |
| 959 前两项 B | 238 | 27.13ms | 1608.02MHz | 32.86ms |
| d148 全三项 C | 236 | 27.68ms | 1561.96MHz | 33.04ms |

**尚未证明整帧 CPU 或 FPS 净收益，三次仍约 30 帧。** 相同旧版本此前也测出 25.13～27.78ms，不能把这个范围内的变化归因于候选；也不能只按频率线性归一化，因为线程还在不同核心间迁移。两次 PMU stat 现使用官方 `--verbose` 保留原始 enabled/running，事件在核心间的实际覆盖合计约 100%，没有发现显著 multiplex；它们仍是独立 8 秒窗口，不是每帧指令数或 DRAM 流量。

保留三项的依据是可核查的重复分类和整次遍历减少、必要的串行/异步与内容检查、真实游戏短测通过，以及未见明显画面或重大资源成本退化；不是将局部减少写成已经涨帧。新增元数据读取、追加分类、边界检查和 walk 栈成本均保留在候选中，没有从对照中隐藏。

下一层表示复用暂不另造缓存：现有 stage-last、shader history、texture recipe 和 operation geometry 已覆盖相当多重定位。最终 descriptor 命中约 93% 不能证明 NVN 输入状态未变，更不能跳过实际资源消费。已有旧捕获的有限子集也没有证明有规模的重复消费者；例如同一 VkBuffer 的不同 offset/range 必须视为不同访问，不能按 handle 伪造去重收益。完整当前 N→U 比例尚缺证据，因此大型消费合并后置。

A650 补充结果：强制 SYSMEM 的第二次普通运行仍发生真实 kernel GPU hang/skip，不能把故障归为纯 GMEM；原有完整 shared-import-windows 双向、重叠和跨窗字节 oracle 在实际 A650/f830 上通过。基础共享能力有实证，实际游戏故障仍未修复。以上不能替代 6GB 持续游玩验收。

本轮真机结果：

- `.local/logs/native-cpu-combined-pair-v1-candidate-B-root-actual-review.json`
- `.local/logs/native-cpu-combined-pair-v1-candidate-C-root-actual-review.json`
- `.local/logs/descriptor-consumer-split-v2-investigation-agent.json`
- `.local/logs/descriptor-consumer-duplicate-audit-v1/result.json`
- `.local/logs/tablet-shared-633-f830-v1/standalone-shared-window-oracle-v1/closure.json`

## 新证据与下一轮优先级

同一 `d14808f` 原游戏另一次完整事务保留了官方 Simpleperf 的两个独立 8 秒窗口，以及原始 enabled/running 输出。前端停顿窗口中，用户态周期约 102.19 亿，L1I 相关前端停顿约 19.03 亿，占 18.63%；全部前端停顿约占 27.99%。另一缓存窗口中的 L1I refill/access 约为 11.03%。事件实际覆盖合计约 100%，未见显著 multiplex。

这些是**整条 NvN 线程的 PMU 事件比例**，包含宿主、Turnip、Bionic 和核心迁移的影响；不是已定位到某个函数的 18.63% 时间，也不是可直接兑现的提速比例。两个窗口不能拼成一条因果计时，前端各分类也不能任意相加。官方采样的具体调用路径与实际机器码用于选择候选，之后仍需真机对照。

1. **优先验证热路径代码布局。** 候选 `53c7d26` 将原有 PSO 准备、driver constant 真正未命中的构造，以及未对齐 Storage 复制阶段，分别留在原条件后调用独立函数。没有跳过资源消费、ready、hazard、pin 或写回，没有新增缓存或改变 Shader。实际原 NDK/O3 ARM64 对象中，绘制和绑定两个主要热函数合计缩小 6,988B，两个完整对象的 text 合计增加 504B；常见 draw/binding 的入口栈帧和减少 2,384B，PSO 准备慢路径的两层帧和增加 4,432B，并增加对应调用/返回。16 个原内容/异步/失败回退 Vulkan 用例通过，候选与同函数体内联参考的画面内容一致；这些不能代替 Android 原游戏验收。canonical 包和原始 NSO/verify28 构建进行中，设备对照使用原 `d14808f` 包。
2. **并行验证完整游戏中的 ASTC6 原生资源。** 实际 f830 bake 的 32 个条目已组成完整 6,217 资源族；其余 6,185 个条目、ID 和 family 字节保持原样，上游 UI 保留。165/166 两种格式均已在真机原 NVN 资源路径通过全部 mip、尾 mip、原图读写、共享页及先前 GPU 写入的内容验证。32 个条目的 payload 从 2,949,120B 降至 1,671,168B；全包只减少 1,277,952B，**不是全游戏减少 43% 内存或带宽**。下一步用原游戏验证 Builder、streaming 和画面，再扩展经过原画质规则批准的范围。
3. **A650 平板独立修复真实 GPU fault。** GMEM 和 SYSMEM 都出现过 kernel hang，基础共享字节 oracle 已通过。现在针对一次实际错误停点，用原 Mesa cffdump/RNN 解码命令和着色器输入，再在同一停点读取有明确 BO 映射的输入及生命周期信息；不把另一次运行的地址或资源拼接成证据，不继续随机改标志重试。
4. **大型消费缓存和去重继续后置。** 尚无当前完整 N→U 数据证明规模，已有多层缓存也已覆盖许多命中；先不引入新的失效协议。帧回调和翻译多线程依用户要求暂不实施，Mali 继续暂停。

本轮新增证据：

- `.local/logs/q735-d148-frontend-official-v1/root-frontend-PMU-review.json`
- `.local/logs/cpu-cold-phase-outline-53c7d26-root-source-review.json`
- `.local/logs/native-qualified-astc6-NVN-165-166-root-actual-review.json`
- `.local/logs/native-qualified-astc6-pilot32-root-asset-review.json`

ASTC6 格式 165 的实际 GPU 终态为 rc=0，内容和清理均通过，但当时控制器进程退出 1：原因是成功 stage 白名单漏掉新格式阶段。原记录保留；`882cca3` 修复了控制器，格式 166 已用其完成实际退出 0 的事务。没有把旧控制器退出码重新标为成功，也没有将独立资源验证记作原游戏通过。

## 构造阶段外提与资源扩展的实际结果

`53c7d26` 已完成 canonical Android 原入口构建、原始 NSO fresh rewrite 和独立 verify28，并执行真实游戏三段跑动、存档退出、官方工具采集和原包恢复。根审实际查看了跑动后画面，未见明显营地黑块或几何错误。相邻 `d14808f` 对照使用同一新工具、新增的独立 raw L1I refill 采样窗口、原 6,217 个 ASTC4 原生资源、f830 和固定 0.60；两个完整事务均无清理错误。

| 运行 | 完整 Surface 间隔 | 翻译线程 Running / 间隔 | 运行时加权频率 | 平均出图间隔 |
| --- | ---: | ---: | ---: | ---: |
| 53c 外提候选 | 234 | 28.09349ms | 1566.29MHz | 33.133708ms |
| d148 同工具对照 | 235 | 27.05099ms | 1609.12MHz | 33.133888ms |

未证明净提速；候选 Running 高约 3.85%，同期频率低约 2.66%，不能只按频率线性修正，也不能删去不利样本。独立前端停顿窗口的 L1I stall/cycles 为 17.57% / 17.53%，没有明显比例改善。取指 refill 和 ITLB 计数下降保留为进一步核验的线索，不能冒充每帧工作量或整帧收益。保留候选的依据限于明确的代码布局和热路径入口栈帧减少、内容/异步/失败回退检查，以及未见明显画面或重大资源成本退化；不把整段约 18% 停顿记作候选可省额。

新增原 Simpleperf raw refill 采样使用 period=1,000,000、DWARF=32,768B，原 cpu-clock 99Hz/16,384B 窗口不变。实际原 ReportLib 和 LLVM 核对两侧 host ELF 与 f830 BuildID 后，refill 样本仍主要落在宿主；53c 为宿主 72.44%、Turnip 14.04%、Bionic 12.65%，d148 为 74.92%、12.29%、12.12%。完整调用职责和实际保留父栈同时归档，嵌套 inclusive 比例不相加，缺失的父栈不补造。仅凭两次样本不能宣称每个宿主子树都下降。

资源准入另有一个小候选 `2327201`：直接复用既有 8B binding schema 的状态偏移、Storage writable 和 bias 槽，仍读取传入的当前或保存状态，不新增缓存、epoch 或常驻字节。它只针对明确观察到的 6/632 个类型/寄存器解码样本，约 0.95%；不能写成消除了整个 7.91% 准入。原范围/内容/异步及 ASan/UBSan 检查通过，错误地读全局 S 的变异负控被检出。Android canonical/raw28 已完成，尚未进行该后继的原游戏资格验证。

调度调查使用同一实际 Perfetto：翻译线程在低 capacity 的 CPU0–2 上不足 0.07% Running，大部分时间位于实际 capacity 873/1024 的核心，ready 等待约每个 Surface 间隔 0.2ms。现有数据不支持被困小核或长时间抢不到 CPU。线程频率确实未持续达到硬件上限，但这不是擅自绑核或强制升频的依据；旧 Q/f830 ADPF 有效 API 对照已经做过，未见可辨收益。实际 priority 的 vendor 编码暂不猜测，下次既有游戏事务读取系统提供的 policy/nice/cgroup/uclamp 补足缺口。

ASTC6 混合 32 项已经完成独立真实游戏三段跑动与存档恢复；画面短测未见明显问题，但不证明这个场景使用了全部 32 个条目。之后原工具、原 producer 和 f830 实际处理了全部 2,838 个已通过原画质规则的候选，23 批全部完成并清理，根审重核 8,560 个输出哈希。实际 native physical bytes 合计从 173,953,024B 降到 103,989,248B，减少 69,963,776B，约 66.7MiB；2,834 项变小、4 项相等，没有变大项。此处仍是资源表示/驱动需求量，**不是已测的游戏 PSS、DRAM、温度或 FPS 收益**。完整资源组合及后续真实游戏验证仍需完成。

下一轮不继续无限外提低频分支：优先验证完整资源扩展、一次共享整池/1MiB 窗口策略对照，并在保持依赖正确的前提下减少实际动态工作。整池模式原本会跳过整个 operation preflight，却也失去 operation geometry 并可能导入更多 Pool 页面；需要测净 CPU 和内存成本，不能直接把约 8% 当成可得收益，更不能直接作为 6GB 默认策略。

新增可核查记录：

- `.local/logs/native-cpu-cold-phase-53c-Q735-v1-root-actual-review.json`
- `.local/logs/cpu-cold-phase-53c-d148-official-comparison-v1/comparison-facts-agent.json`
- `.local/logs/cpu-thread-placement-53c-d148-v1/placement-policy-adpf-findings-agent.json`
- `.local/logs/pool-admission-consumer-2327201-root-canonical-raw-review.json`
- `.local/logs/native-qualified-astc6-game-d148-Q735-v1-root-actual-review.json`
- `.local/logs/native-qualified-astc6-full2838-root-actual-review.json`

## 整池导入对照与按内存属性消除无效维护

原 `d14808f`、f830、原6217 ASTC4、0.60和同一官方工具的整池共享实验已完成真实游戏三段跑动、原生命周期存档及原包恢复。与前述1MiB对照相邻比较：完整Surface内NvN Running/间隔25.75059ms vs27.05099ms，约低4.81%；运行时加权频率1545.52 vs1609.12MHz，约低3.95%；平均出图间隔33.09361 vs33.13389ms，仍约30FPS。一次顺序实验和不同核心/频率不能建立净因果收益。

内存代价：同口径dumpsys TOTAL PSS为2,703,265 vs1,945,728KiB，多757,537KiB（739.78MiB）；Buffer backed bytes从369,098,752升到1,167,065,088，private image cache两侧同为344,166,400B。整池省掉了准入枚举，但失去operation geometry，pool_address/record_pool_range的样本占比和slow lookup也增加。**保留1MiB默认，不把整池作为6GB或省内存优化。** 下一步只借用已经保有的实际生命周期引用，降低有限窗口的重复pin/unpin，不能跳过实际消费者或制造ready证明。

候选 `5669c44` 减少的工作来自完整调用路径 `commit → sync_used_run → shared_pool_cache_locked → native_allocation_cache_visit`。原代码遍历native分配AVL后，对每个相交分配调用shared_cache；HOST_COHERENT分配每次都立即返回。两个import发布入口本来就持有resources锁，强制同Pool全部live Buffer/native import的propertyFlags一致。现在利用同一个Pool属性在native cache遍历入口提前返回。非coherent路径完全保留；逻辑范围验证、完整物理覆盖证明、Buffer维护、资源引用、CPU发布和GPU数据依赖均未省略。不新增缓存、失效协议、常驻RAM或游戏逻辑。

5组现有Core/SYNC Lavapipe原GPU内容验证通过：驻留资源串行/异步/参考布局，有限窗口串行/异步，覆盖同值/无Flush CPU发布、三次无显式hostwait提交、先前GPU写入、Image→Buffer完整payload、native-only后首次Buffer及窗口合并/最终释放。每组5356或10873个原检查无验证错误；这是RGBA语义验证，不能称Android ASTC或真实游戏验收。首次host链接缺少DM_GUEST_HEAP_GPU_TESTS=ON的错误保留；补齐既有选项后构建通过，没有改运行时代码绕过。

实际53c CPU窗口的native cache visit叶子占1.28%，共享缓存职责inclusive约2.24%，两者嵌套；只省其中coherent遍历，不预报整个比例或FPS。候选原游戏构建及实际效果仍待统一版本验证。

- `.local/logs/shared-whole-d148-policy-probe-Q735-v1/result.json`
- `.local/logs/shared-whole-d148-offline-analysis-v1/whole0-control1MiB-facts-agent.json`
- `.local/logs/coherent-native-cache-v1-tests/result.json`
- `.local/logs/native-qualified-astc6-full2838-root-asset-review.json`

## 同一完整ASTC6包上的统一CPU版本结果

179c82d和原d148均实际执行完整6217混合包（三段原摇杆路径、原存档2次、停止并恢复原包）；候选画面短测未见明显营地黑块或缺失几何。主线84df3da的运行时文件与已测试179c82d一致，原APK/NSO产物仍保留真实179构建记录，没有改写版本标签。

| 版本 | 完整Surface间隔 | NvN Running/间隔 | Running加权频率 | 平均出图间隔 |
| --- | ---: | ---: | ---: | ---: |
| 原d148 + full2838 | 232 | 26.67854ms | 1603.23MHz | 33.521829ms |
| 179统一CPU + full2838 | 235 | 27.51098ms | 1583.28MHz | 33.097001ms |

两侧p50/p95均约33.24ms；未证明净提速，不将均值的小幅变化称作涨帧。CPU样本中native cache遍历从4/620到0/618，准入inclusive从9.19%到8.25%，pin inclusive从3.87%到3.40%；绑定仍约20%。局部路径变化成立不等于整条线程已经变快，不相加嵌套比例。不同实际频率/核心及一次顺序运行也不能证明净回归。

准入的实际可见CPU caller均为原draw：179的51个样本中43个从OP_DRAW_IDX、8个从OP_DRAW进入；原d148为48/9。未采到内部read/transfer/dispatch直接caller不表示它们从未调用。原admissions为全类型计数，没有分类；不能从stream.draws倒推它，因为后者在split统计V.draws差、含compute/专用draw且漏尾段。

候选TOTAL PSS1,980,341KiB，对照2,031,850KiB，相差50.30MiB；不能归因于只增加最多4,224B元数据且不减GPU分配的CPU改动。image cache两侧仍344,166,400B。完整ASTC6包的66.72MiB表示量收益和进程PSS继续分别记录。

本次执行argv错误地将report放在logs根层，原控制器按report.parent输出。完整80个实际文件已原字节归档到独占目录，记录original_path/archive_path/sha256/size，原result及metadata路径未改，也没有因此重采。7d54716对后续工具增加执行前report父目录与声明output一致的检查；真实host CLI正确路径通过，错误路径在设备操作前拒绝。该路径错误不改变实际游戏通过、存档恢复或工具数据的结论。

- `.local/logs/native-cpu-pool-pin-coherent-179-full2838-root-actual-review.json`
- `.local/logs/native-cpu-pool-pin-coherent-179-full2838-Q735-v1/archive-map-root.json`
- `.local/logs/cpu-pool-pin-coherent-179-full2838-offline-analysis-v1/facts-and-evidence-agent.json`
- `.local/logs/submission-resource-admission-scope-readonly-v1/result.json`

更大的前置资源计划尚未实施：当前walk只有路径/DS/query/store摘要，提前collect只是搬工作；提前reserve会导入后来被跳过操作的页面，native图像cold create还会实际acquire。只有已backed资源的lazy段lease可能在完整失效/生命周期合同下减少整轮工作，但还缺真实范围重复率与state generation。准入和索引的可见样本并集约12.97%，即便全部删除也不能单独把26～27ms降到16.7ms，不能许诺这一个改动解决60FPS。

## ThinLTO原游戏对照与下一步取舍

候选9225954只改宿主构建，运行时C/C++/ASM与179相同；1331个原动态导出、535个NVN/汇编入口均由实际LLVM符号表核对保留。原NSO重新加工及28产物独立验证完成，真实游戏三段移动、原生命周期save2、停止、原包和资源恢复通过。根代理已查看营地截图，未见明显黑块或几何丢失；这不代表全游戏验收。

| 同full2838/f830/1MiB/.60 | 完整Surface间隔 | NvN Running/间隔 | 运行时加权频率 | 平均出图间隔 |
|---|---:|---:|---:|---:|
| 179 | 235 | 27.510981ms | 1583.28MHz | 33.097001ms |
| 922 ThinLTO | 237 | 27.362077ms | 1577.00MHz | 33.025215ms |

整CPU差约-0.54%、频率差约-0.40%，不能宣称净提速。对应官方8秒PMU窗口指令数约少1.5～1.9%，但core cycles增加2.07%、frontend stall增加5.94%，ITLB等亦有反向变化；不能挑一个计数器宣布收益。LTO会改变内联和物理符号，报告保留真实unwind与LLVM inline源行，不把调用树消失当成工作被删除。可选编译入口进入主线，默认不启用；没有改Shader、游戏逻辑或驱动。

优先级继续聚焦绑定约20%和完整资源准备/依赖责任的批量重复工作。纯mtl_exec解释器self仅12/618≈1.94%，flatten会引入CALL-return/END/checkpoint语义维护，当前不实施。Allocator可归属样本也只约0.65%，不另造大内存池。下一轮优先找能够减少整轮工作、且失效条件可证明的实现，而不是继续调整小函数布局。

证据：`.local/logs/host-thinlto-9225954-root-actual-review.json`；`.local/logs/host-thinlto-922-full2838-offline-analysis-v1/facts-and-evidence-agent.json`。设备已恢复原包并停止，Mali仍按用户要求暂停。

## 资源准备组合2fc：机制合入，未证明整帧提速

179后继2fc55c2增加两项：原生采样图像已固定GENERAL且无mip/格式/Storage准备时跳过无效准备；完整tex_t及view字节相同、有效的重复注册不再改变全局descriptor epoch。两者各自原GPU内容测试通过，新增测试从APP排除；真实变更、CPU写、GPU writer、native/Buffer alias、物理生命周期均保持。

原游戏实际237个完整Surface间隔，整NvN Running27.849208ms/间隔、加权1575.40MHz、平均出图32.954417ms，仍约30FPS。scheduler bracket本身PSO184/184、variants33/33稳定；更早CPU采样窗中PSO177→184，不能称整轮steady。该scheduler bracket的既有recipe view命中94.1103%，历史179为93.1111%；慢路/guest分别625.84与746.93，但场景/PSO/频率未严格匹配，不把差额归因成单项收益。views/prepares是命中计数，不是总调用量，不能当同值注册命中数。

主线运行时代码与2fc相同，另保留默认OFF的ThinLTO构建入口。实际短测截图未见明显缺画面，原生命周期存档2次后已停机、恢复原包与资源；没有长期发热或全游戏验收结论。

后续组合74e4050独立构建：既有logical lookup锁内重核完整当前值后，只延续逻辑未变recipe的epoch；正常Android NVN入口移除共享诊断STADD，显式诊断仍走原计数入口。后者来自全PID已有27/1065入口叶样本及真实LLVM指令，不能全归因给原子写，也不能承诺相同比例提速。增加约16.3KiB静态text/table，新fast ELF符号隐藏；不改变游戏/绘制语义。

证据：`.local/logs/native-preparation-2fc55c2-root-actual-review.json`；`.local/logs/native-preparation-2fc55c2-full2838-offline-analysis-v1/facts-and-evidence-agent.json`。
