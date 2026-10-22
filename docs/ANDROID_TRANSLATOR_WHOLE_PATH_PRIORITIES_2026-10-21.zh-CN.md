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

### 更高层次：先量化数据表示与合批规模

用户要求改前给预算，而不继续以叶子调用次数决定优先级。当前准入候选消除的是每draw完整资源枚举这一阶段，不是再套查找缓存；覆盖约8%的用户态样本是整个职责的包络，实际消费者、引用和同步仍要执行。绑定约21～22%也不是可删除的重复工作。仅替换descriptor更新API触及约1.1%的driver update self；即便乐观地把绑定self、descriptor构造及相关驱动交付都视为目标，预算也只有约7～8%，不是已经测得可省同等时间。

比减少函数调用更大的方向，是分离长寿命的资源表示与每draw变化的参数，让一段合法绘制共享资源解析/状态交付；实际GPU依赖仍按读写发生处维护。现有dynamic UBO、schema、descriptor history、source_flow及recipe已经解决其中一部分，不能重新把它们当缺失能力。是否需要进一步改变shader输入/每draw数据表，应由真实连续组及新增GPU访存、参数构造、生命周期成本决定。

已用既有完整GFXReconstruct捕获及官方JSONL检查历史Mali两个独立帧，没有新增设备采集。2425draw帧中，完整记录绑定状态严格相同的连续组为0；仅忽略Buffer偏移有134个长度至少2的候选组，覆盖1028draw，最长338。另一2351draw帧同样严格组为0，忽略偏移后154组、最长327。这只是改变数据表示后的结构包络，不是合法可合并数，更不是当前Q线程时间比例。

最长338组在同Pass/PSO/descriptor/动态状态下只变VBO偏移，但256B分配步距与20B顶点stride不整除，不能直接用firstVertex替代重绑定。通用vertex pulling/每draw base表或紧凑顶点表示才可能处理，均有新增GPU读取或拷贝成本；triangle strip边界、实例/builtin、CPU发布与alias依赖必须保留。338/2425约14%仅是绘制数量，不能预估省14%CPU或10ms。优先补齐当前Q候选组证据，再决定这种较大改造；不凭历史Mali计数开写大方案。

等待不是当前已证实的大头：NvN实际Running约27ms/Surface间隔，ready仅约0.2ms；present/fence跨度不能与Running或彼此相加。先减少同一工作量的整线程Running，不能靠隐藏线程、强制升频或更换计时口径宣称优化。按需窗口仍默认关闭；先修实际拷贝准入缺失，再做同APK开关对照，不能拿失败ON结果评价性能。

只读证据：`.local/logs/nvn-adjacent-draw-existing-frame-v1/conclusion-and-budget.json`（SHA256 `19f85c46fff938bf378f788cbe0da60eb31895eed98f863a7e93ae2cc9f473c0`）、`.local/logs/nvn-resource-representation-a31-readonly-v1/research-conclusion.json`。前者保留原捕获、官方转换工具、shader反汇编与聚合输出指纹及所有资格限制；不是新增计时工具。

### e0b实际加载：复制合同修好，空绑定语义仍须区分

后继`e0b40235`恢复generic copy/dispatch准入，只有draw走按需准备；真正T2T collector还为registered UNBACKED目标建立完整范围，原观察函数不导入、不放宽scope/stamp/alias条件。8项必要host用例（7项正常及1项明确distinct Buffer别名重叠拒绝）完成，原Android canonical及三个原始NSO fresh rewrite/独立verify28完成。源码、编译选项、六库及原始产物的根审记录位于`.local/logs/shared-lazy-copy-e0b4023-root-canonical-raw-review.json`。

新ON1原游戏实际运行已越过原mixed compressed copy，并记录原保留转换，但在进入场景时触发实验导入预算。事务终态1，原APK/资源恢复，cleanup为空、后核游戏PID为空，没有开始任何官方性能窗口；同包OFF1/OFF2/ON2因此不执行。此结果不能说明CPU/FPS收益，也不能以加载过程RSS代替稳态PSS。

官方Android崩溃栈的新PID16945、NvN17012、ELF BuildID`98200357e17e545b0e2cf585c14b13bf981b8c23`与精确LLVM反汇编共同定位到indexed buffer绑定：`idx.count * element_size`传给`pool_address`。不能因为函数共用或源码行号模糊就把这次实际调用称为VBO。失败处ensure取整范围为`[0x0a700000,0x2c600000)`，543MiB超过512MiB单Pool累计导入上限；未在错误信息中直接保存的count仍不冒称实测值。

源码合同揭示应修正的语义混用：原准入对empty indexed draw仅需1byte地址证明，执行绑定却可能传0；lazy通用解析把0解释为未知长度VBO并覆盖Pool尾。VS UBO/SSBO的非零地址、零长度同样应保留原descriptor零缓冲回退，不能制造整尾读取。最小后继在typed调用点区分空索引/零绑定和真正未知VBO，保留后者完整范围/RAW合同，保持预算上限；不通过抬高内存限额或缩小未知读取范围过关。机制仍只是约8%准入职责包络的实验，不宣称解决27→16.7ms差距。

失败证据：`.local/logs/shared-lazy-copy-e0b4023-ON1-root-failure-review.json`，包括原事务、真实日志、含历史及当前PID的官方crash-logcat和LLVM source定位输出。源审及host通过仅证明相应用例，不代替Android原游戏验收。

### 533：四次原游戏通过，准入减少并未兑现成整线程提速

`533820e6`在typed调用处区分空索引及可选零绑定，未知长度VBO和原导入上限不变。原host八例、独立源码审查、Android canonical及三个原始NSO fresh rebuild/独立verify28均完成。随后同一533 APK/f830/full2838/.60/1MiB，按ON1→OFF1→OFF2→ON2四次冷启动，各完成原三段跑动、官方3×30秒FP及5×8秒Perfetto、存档退出和原包恢复。根代理查看ON1前后截图，营地短测未见明显黑块或缺失几何；不外推全游戏。最终Q进程已停止，没有留游戏空跑。

| 实际顺序 | 5个独立Perfetto窗的NvN Running/完整Surface间隔（ms） | 后续PSS快照（MiB） |
|---|---|---:|
| ON1 | 27.60 / 27.04 / 27.60 / 27.55 / 27.54 | 2000.8 |
| OFF1 | 24.09 / 27.09 / 26.92 / 27.05 / 26.87 | 1931.3 |
| OFF2 | 25.29 / 26.49 / 27.92 / 27.37 / 27.49 | 1929.7 |
| ON2 | 25.75 / 25.30 / 27.83 / 27.78 / 27.85 | 2064.6 |

12个FP窗合计27,306个NvN样本全部保留线程外层祖先；原官方图和时钟重叠验证通过，20个Perfetto窗无报告丢失/错误。**宏观职责结论可重复，但没有证明净CPU/FPS收益。** 全部窗口保留，不能删除OFF1的24ms或ON2的28ms来凑结论。运行核心分布会变化，特别是CPU7占比；只看MHz不能认为执行能力相同。各轮整数位置与NPC工作量也不完全一致，OFF1/OFF2/ON2首个FP窗的外围PSO bracket有增长，均明确标记。Surface均值仍约33ms。所有这些对照沿用公开performance_hint reference=true，不混称默认长期游玩测试。

两次ON均复现职责转移：draw准入约0.04～0.26%用户态样本，而两次OFF为8.32～9.76%；但物理union覆盖证明self变成2.46～2.91%，OFF仅0.22～0.70%。实际Buffer消费的`pool_address` inclusive也升高，self范围却重叠，说明增加的是完整消费子链中的工作，不能只给lookup叶子贴“慢”标签。新增覆盖成本明确位于commit→sync_used_run→shared_pool_cache_locked；lazy分支每次推进union frontier都重新扫两条物理Buffer链。前置准入原来还提供可复用几何和引用，删去它并不是无成本删除8%计算。

ON两次逻辑Buffer并集约363MB，累计import约600～610MB，后者包括重叠别名，不能当新增物理RAM。两次ON的报告PSS和Graphics/mtrack高于OFF；showmap的guest heap PSS大致相近，KGSL映射与mtrack仍须分别记录，不能用不同时间的snapshot强行对平。该资源成本与未完成的长时生命周期验收共同构成保持实验默认OFF的理由，不能仅用“没有即时涨帧”判定机制无效。

下一项已授权为**真实物理Buffer覆盖索引**，独立后继实现，不再加命中预测缓存。复用已有AVL算法，在真实import/销毁边界维护typed Buffer索引；保留native import的alive树，用二者推进原覆盖证明，不改变single-Buffer解析、句柄、origin或在途寿命。预算只限上述约2.8% self中的一部分，低于1ms量级，不许诺10ms；代价为每span约64B及Pool根字段，当前338个Buffer约21KiB，另计内嵌whole节点成本。较大绑定表示改造的直接self只有约4.1%，仍需先证明真实状态变化和合法批量规模，不能再把整个22%绑定子树当可得收益。

完整根审：`.local/logs/shared-lazy-empty-533820e-root-four-round-review.json`（SHA256 `0a7b142430431c6b195c43ab8f3c36fc34c2ecbff3a9937af54221641c50e8f5`）。四轮所有12窗调用链及20窗宏观表、内存分类与限制均由该档引用。高层状态表示的只读审计另见`.local/logs/nvn-resource-state-533-readonly-v1/conclusion.json`。仍未完成持续散热或6GB真机验收，不能宣称目标完成。

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


## 533四轮对照：从局部去重转向减少整条路径工作

同一533 APK完成ON1/OFF1/OFF2/ON2四次真实游戏事务、12个官方Simpleperf窗口和20个Perfetto窗口，均执行原三段移动、存档、停止和原包恢复。准入采样占比ON为0.04–0.26%，OFF为8.32–9.76%；覆盖检查self反而由OFF的0.22–0.70%增至ON的2.46–2.91%，实际消费地址解析的inclusive也上升。后续多数窗口的整NvN Running仍约27–28ms/Surface间隔，未证明净CPU或FPS收益。不能以一个调用子树消失宣称整线程减少9%。

20个有效窗口全部保留，包含24ms和28ms附近的两端；核心驻留、频率、动态场景和首窗PSO增长分别记录。采样的cpu-clock:user职责比例与另一个8秒窗口的scheduler Running不能直接相乘成准确的职责毫秒。ON快照PSS相较OFF高约69–135MiB，导入字节又包含物理页重复映射，不能当新增等量DRAM。lazy仍为实验默认OFF，除了未证净收益，还存在实际资源代价和长期导入生命周期待验证。

后续优先级及改动前预算：

| 优先级 | 更高层次的问题 | 现有证据与预算 | 决策 |
|---|---|---|---|
| 1 | 为什么每次draw都要重建绑定表示 | 绑定inclusive约21–23%，包含必须保留的真实资源消费；表示构造self约4.1%，其中也有必要工作。全删的乐观量级约1ms，实际只能其中一部分；不能许诺省整个22% | 先用完整命令记录核实槽值、偏移和物理代数的实际变化，复用已有last-values，避免另造大型版本缓存 |
| 2 | 物理覆盖证明的数据结构不适合重叠导入 | 每推进覆盖边界都扫描两条Buffer链，self约2.5–2.9%；改区间索引是同一职责更便宜，不是消除整轮职责。乐观量级不足1ms，非实测时间 | 独立4422候选已实现原AVL复用，64B/节点、72B/Pool，canonical/raw资格进行中，尚无真机收益 |
| 3 | 每draw状态和命令数量是否可以成组表达 | 历史帧存在338个仅Buffer偏移变化的相邻draw，但严格全状态相同组为0；stride和偏移不能直接用firstVertex折叠。历史draw数量比例不是当前CPU占比 | 先补当前Q/f830完整帧证据，再决定通用数据表示/批量发射；未预支毫秒预算，不改游戏渲染顺序 |

方向不是无止境地优化小函数，而是分清：哪些数据应在状态变化时构造、哪些物理拓扑应在导入时维护、哪些真实依赖只能在消费时执行。绑定时提前取得一个句柄不能替代draw时的CPU发布、GPU writer、layout和资源生命周期判断。现有缓存已相当多，新增一层缓存必须证明它消除的完整工作以及失效成本。

27ms降至16.7ms需要约38%的CPU时间下降。当前没有证明一个可安全删除的38%重复块；上述局部预算不能相加成已经获得的10ms。以整条翻译线程Running、用户态职责、实际工作量和内存代价四个结果共同决定下一步。

证据：`.local/logs/shared-lazy-empty-533820e-root-four-round-review.json`；`.local/logs/nvn-resource-state-533-readonly-v1/conclusion.json`；`.local/logs/nvn-adjacent-draw-existing-frame-v1/conclusion-and-budget.json`；`.local/logs/shared-lazy-buffer-index-4422f76-root-source-review.json`。


## 4422覆盖索引：局部下降与不利整线程结果并列

4422已通过原Android构建、原NSO fresh rewrite/独立28产物验证，并完成真实游戏原三段移动、3×30秒官方FP和5×8秒Perfetto、原存档及恢复。根审实际查看营地画面，未见明显缺失；没有长期游玩、温度或6GB验收。

完整覆盖成本族（旧函数、新Buffer树函数、native查询及实际LLVM内联来源去重）占NvN用户态采样，从533六窗3.83–4.13%降至4422三窗1.34–1.71%。这支持减少局部查询工作；不能因旧符号self归零称全部消除，也不能直接换算另一个scheduler窗口的准确毫秒。

不利整帧结果必须保留：4422首轮五窗NvN Running34.7–35.9ms/Surface，Surface43.4–44.8ms。紧邻相同旧533ON3回到26.0–27.8ms/33.1–33.6ms。4422主要运行1.34–1.38GHz、CPU7约0–1.1%，533ON3约1.56–1.60GHz、CPU7约7–31%；这不能用MHz线性校正，也暂不作单一索引因果归因。后继同4422重复事务已启动，用原工具判定运行相位的可重复性，不因一次坏结果改掉代码或筛除样本。

4422首次每Surface约有8.7–9.1ms队列完成等待；FP真实保留的父链为draw→shader_cpu_sources→texture_cpu_preflight→shared_cpu_observe_prepare→internal_wait。marker墙钟含GPU执行、驱动、worker和CPU唤醒，不是纯GPU时间。它是后续高优先级的数据依赖调查入口，先确认具体资源及CPU读的必要性，而非先删除等待。实际CPU image upload调用子树只占约0.09–0.28%，不能把约9ms等待全部预支给GPU解排布。

上游Metal已存在nvn_deswz_gpu_encode/encode_m4；Vulkan目前只有反向retile，缺对应正向GPU解排布。候选方向是共享NVN源→GPU线性临时Buffer→原CopyBufferToImage，保持原纹理格式/mip和通用语义。仍需证明该等待的具体源属于此路径；真实queued CPU effect、CPU写的排他/发布和dirty stamp不可用GPU barrier代替，新增dispatch/临时存储/带宽也必须计入。

内存另有独立约束：lazy每个未被单VkBuffer完整覆盖的请求都可新建重叠Buffer/Memory，旧句柄保到原Pool关闭。533 ON1在unique不变时仍新增5covers/10MiB累计导入，ON2新增3covers/6MiB；这是实际对象增长，累计导入不是同量新增DRAM。4422的64B/span索引不改变此机制。几何增长cover或完整退休合同需要实际请求序列和生命周期证据，单加一个VkDeviceMemory arena包装不会省掉原本已可复用的allocation。默认仍保留已测常驻更低的非lazy有限窗口，不用整池额外约740MiB PSS换取省准入。

证据：`.local/logs/shared-lazy-buffer-index-4422f76-root-canonical-raw-review.json`；`.local/logs/shared-lazy-buffer-index-4422f76-ON1-root-functional-review.json`；`.local/logs/shared-lazy-buffer-index-4422f76-ON1-FP-Q735-v1-analysis/coverage-family-comparison.json`；`.local/logs/shared-lazy-buffer-index-4422f76-macro-v1/533-ON1-ON2-4422-ON1-533-ON3-completed-facts.json`；`.local/logs/shared-lazy-import-residency-readonly-v1/findings.json`。


4422第二轮已完成，同APK没有重现慢相位：五窗NvN Running27.6–28.0ms/Surface、Surface33.2–33.7ms，queue-completion wait均0；CPU约1.55–1.57GHz。覆盖查询族第二轮仍低于紧邻533ON3。这证明“局部工作减少”与“第一次35ms慢相位”必须分开，不能把第一轮全部归因于固定索引成本，也不能删去该不利运行。全部25个宏窗口和15个官方FP窗口保留；尚未证明净CPU/FPS/温度收益。

正向GPU解排布的合同复核还须避免反向推理：旧CPU staging测试保证观察结束后的CPU改写不会改变本次独立staging快照；它应继续检验CPU fallback。原MetalGPU解排布和当前coherent Buffer/native image本就执行时读源，未同步CPU覆盖属于原资源同步合同，不能据CPU实现附带的快照行为强加新的长期CPU读锁。真正需要保留的是配对FS lease/PG_BUSY完成、不能吞掉后续写的代际与原GPU读依赖，以及真实CPU effect写的完成，而不是对全部纯GPU writer强制CPU等待。

证据更新：`.local/logs/shared-lazy-buffer-index-4422f76-macro-v1/533-ON1-ON2-4422-ON1-533-ON3-4422-ON2-completed-facts.json`；`.local/logs/shared-gpu-deswizzle-readonly-v1/findings-corrected-gpu-read-contract.json`。当前单独的官方API帧事务使用显式assisted避免工具shadow替换导入指针，只分析结构/状态，不作为普通性能数据或正确回放证明。


## 当前Q/f830完整API帧补足高层预算

官方GFXReconstruct现已在真实533/f830/shared/nativefull2838/.60配置取得frame1049：一非blank帧，assisted由启动属性、官方层日志及捕获annotation共同确认。原三段移动、稳定准入、存档退出、9属性/5设置及原包/资源恢复全部完成。原始文件501,120,859B已在停止游戏后传回并重核哈希；它不是普通FPS数据，也不证明coherent/noFlush的HOSTPTR payload可重放。

原parser明确剔除initial-state里的2312个旧draw定义和2次重建submit，只按边界后8次真实submit及secondary执行序列枚举。本帧实际2312draw，其中749非indexed、1563indexed，执行50个renderpass。最终输出2750×1260，主3D renderArea1650×756。不能将整个JSONL的4624个draw定义直接当成本帧绘制量。

严格保持所记录全部状态的连续组仍为单draw；只放松Buffer偏移后最大342组是4vertex TRIANGLE_STRIP，stride20而offset步长256。它处于748draw的同一pass内（前148、后258），存在原secondary→primary延迟出pass机制，但不能直接firstVertex重基或把提前copy越过更早writer。shader静态属性和源生命周期仍须证明。

进一步用现有FP真实mtl_exec两个BL位置区分父职责：全部nonindexed仅占NvN用户采样8.18–9.13%，indexed约62.8–64.8%。因此342组即便资格成立，也不能用342/2312当CPU份额，更不能扛整个draw70%的预算。

更简单的indexed候选也有实际数据：仅放松IBO offset、其他记录状态保持，79个>=2组覆盖253draw、最大8；最多省174次入口只是算术资格，不能乘indexed63%换算CPU收益。各组均TRIANGLE_LIST、无restart、相同IBO/type及其他绑定，实际index区间顺序首尾相接，counts均3的倍数、无active query。部分shader具有subgroup/storage等行为，不能全部盲合。下一步优先核原NVN replay中是否真正相邻、能否在完整setup前合并，维持effects/CPUpublication/alias/lifetime/查询与早提交边界；只在已录好的secondary末尾合并GPU命令，省不到之前已经做完的NVN准备。

证据：`.local/logs/q735-f830-official-api-frame-d8f7bb9-root-actual-review.json`；`.local/logs/q735-f830-533-official-api-frame-short-path-v3-analysis/analysis.json`；`.local/logs/nonindexed-parent-envelope-533-4422-readonly-v1/result.json`。

## 09a9 四轮后的高层判断与改前预算

本节取代把“继续找重复小调用”作为主线的理解。09a9 同一 APK 四轮共12个独立官方 FP 窗口、20个 Perfetto 窗口均已完成；19个窗口没有 NvN queue-completion wait，普通窗口的翻译线程仍约27ms CPU/Surface。此前实际发现的 false-page dependency 有语义修正价值，但本轮未重现旧约9ms等待，也未证明稳定整线程/FPS收益，不能继续拿旧等待作为当前优化预算。

先区分三种不同的改法：取消没有职责的整段处理；改变数据表示/生产时机，使必要工作变便宜；减少进入完整绘制流程的次数。查找重复函数只是其中一种手段。当前优先级如下，所有采样占比均为职责上限，不能与另一个8秒窗口相乘成精确毫秒，也不能彼此相加成已得收益。

| 优先级 | 高层改法 | 改动前证据、预算与成本 | 本轮决定 |
| --- | --- | --- | --- |
| 1 | 已可证明没有CPU读取的图像，不再每draw完成整轮CPU源预检 | 12个当前FP窗口中 `vk_shader_cpu_sources` inclusive为4.27–5.39%。驱动原生marker和部分无packaged CPU源图像按原函数直接返回；只有这部分有资格。实际省量小于整项，还需比较真实绑定、注册epoch，并保留RT预检；缓存建立/失效和前后重核有新增成本 | bf351f8已通过host验证，并与通知修改组合为e2d24a7完成Android/原NSO构建；真机对照进行中。复用现有source plan，只保存逻辑空预检证明，不缓存GPU ready，不跳上传、真实consumer或引用 |
| 2 | 长寿命资源表示与每draw变化参数分离 | 更早6个独立FP窗口的相关宽包络约7.04–8.29%，其中包含必要工作；不是整个binding的21–23%。现有dynamic UBO、schema、recipe、source flow和descriptor历史已覆盖部分效果。新shader ABI/资源表会增加GPU读取、失效与生命周期成本 | 继续研究尚未覆盖的表示变化，尚不启动大规模bindless改写，不预支10ms |
| 3 | 合法相邻draw在完整setup之前合并 | 当前Q完整API帧2312draw，严格相同状态连续组为0；仅放松IBO偏移有79组/253draw，理论最多少174入口。原NVN邻接、shader副作用、CPU发布及alias等资格尚未全部成立 | 保留高层方向；draw数量不是CPU占比，未给出合法覆盖量前不实施。不能只合并最后的Vulkan命令，留下前面全部准备 |
| 小修并行 | 按真实完成谓词通知guest waiter | 20个原trace中整个guest wait循环实跑仅0.308–0.403ms/Surface；一个237次guest等待窗口内有2363次记录到的唤醒。仅guest序号推进才需要通知guest，internal queue/reset通知仍有其他职责 | 独立候选44f2d98已通过11项host GPU/同步验证；保留同一mutex/CPU effects发布/有限与无限等待。增加一个CV，不新增线程；尚无Android原游戏收益结论 |

另一个“同pass重复纯采样合并”大方案被否决：现有page/stage/read并集、image read proof、native stage ledger和submission引用已完成主要归并。剩余native use/lifetime元数据宽并集仅0.44–0.91%用户CPU，还包含必要首次使用与重入保活。不能再将7–8%真实view消费当作可整段删除的重复工作。

全应用也已复核，避免只盯翻译线程叶子：其他线程累计25.48–31.19ms CPU/Surface，其中原游戏入口与guest worker占20.90–25.12ms，约80–82%；这是跨核累计CPU，不可加到27ms作为帧关键路径，也不能擅自改游戏算法。dirty/signal/CPU-write-lease用户栈并集最多0.0264%全应用用户CPU，现有数据不支持把mprotect异常重写列为大头；现有trace没有内核缺页/系统调用归因，不能据此说其成本为零。

27ms降到16.7ms需要约38%减少。当前没有证据证明还藏着一个可以安全删除的38%重复块。以上分别说明可触及范围、实际必须保留的工作和新增成本；候选完成后仍用原工具复查整线程CPU和各独立调用树，不以某个旧符号消失作为成功。

证据：`.local/logs/shared-image-observe-09a9ad0-analysis-v1/result.json`；`.local/logs/shared-image-observe-09a9ad0-whole-app-readonly-v1/result.json`；`.local/logs/read-only-consumer-scope-readonly-v1/result.json`；`.local/logs/long-lived-binding-representation-readonly-v2/result.json`；`.local/logs/guest-fence-notify-44f2d98-root-source-review.json`。Mali保持用户暂停；A650不新增设备实验。以上不是持续温度或6GB设备验收。

### e2d24a7：实现成本与当前验收边界

CPU预检候选成功时省去完整binding iterator、plan重建和逐源resolver/preflight调用；原resolver在ID/epoch未变时本来就不查registry、不复制tex_t，不能再把这些算为新收益。新路径在RT预检前后各比较一次6阶段及真实ID；失效路径仍走原遍历，并增加短路的空预检证明扫描。后续consumer若把槽刷新为另一已注册ID，即使全局epoch没变，也会撤销旧证明。普通有CPU源的packaged图像、Storage/array/不完整schema和deferred-publication均保留原路径；真正上传、alias、layout、GPU引用及CPU effects未删除。

两候选合并后，normal及ASan/UBSan共10项组合GPU/同步案例通过，覆盖同epoch换成真实CPU源、RT重入、上传/回读、有限/无限guest等待、CPU effects发布及command reset；各候选自己的必要验证也保留。此前把新GPU回读案例放进旧native-scope同进程夹具引起的unknown-transfer断言失败保留，新案例改为独立进程，原断言及生产同步没有放宽。

原Android入口构建、535入口/524隐藏fast符号、1333导出与09a9严格相同、原UI/签名/16KiB对齐以及原6+新增1个实际APP预处理闭包均已核验。三个原NSO重新加工并独立verify28，23项稳定模块/规则产物字节与09a9一致。根独立核对5,699项哈希；实际Android ELF确认新增64B逻辑元数据和48B条件变量，无新增GPU缓冲或线程。Q已进入真实游戏营地并开始原工具对照，尚未形成整轮CPU/FPS/内存或持续温度结论；Mali和A650不操作。

证据：`.local/logs/cpu-source-completion-e2d24a7-source-ready-agent.json`、`cpu-source-completion-e2d24a7-root-canonical-raw-review.json`、`cpu-source-completion-e2d24a7-combined-tests/results.json`（后两项同位于`.local/logs/`）。实现备份分支为`codex/cpu-source-completion-combined-v1`，尚未合入稳定运行时。

### e2d 与紧邻09a9实际对照：通知减负成立，预检没有整体下降

两次真实游戏事务均终态0，原三段移动、save2、停止、原包与资源恢复完成。根实际查看两版前后四张营地图，未见明显缺失，并独立核实最终原APK哈希及PID为空。原工具6份30秒FP与10份8秒Perfetto全部保留，各自生成官方图；根复核327份输出和记录引用共427项哈希、10窗trace health为空、6窗官方时钟重叠成立。不再为这个候选重采基线。

| 同口径结果 | e2d候选 | 新紧邻09a9对照 | 解释 |
| --- | --- | --- | --- |
| guest wait循环实际CPU/Surface（5窗） | 0.080–0.097ms | 0.299–0.352ms | 每个窗口均有局部CPU工作减少；这是guest等待循环，不是NvN/GPU关键路径 |
| 等待内显式非IRQ唤醒（每原8秒窗口） | 253–307 | 2498–2708 | 无谓唤醒显著减少，仍保留必要最终通知；不是精确condvar返回次数 |
| 整NvN Running/Surface（全部5窗） | 25.488/25.898/28.013/27.600/28.029ms | 27.065/28.034/27.788/27.239/28.102ms | 保留早窗不同频率/工作量，未证明稳定整线程提速 |
| CPU源预检完整职责（各3窗用户态样本） | 5.161/5.591/5.548% | 4.792/4.846/4.378% | 没有表现下降，新增helper和内联finish证明均计入，不能声称省掉改前5%包络 |
| 平均完整Surface间隔 | 约33.13–33.14ms | 约33.18–33.24ms | 仍约30FPS，不能称已到16.7ms |
| 单次端点TOTAL PSS | 1,968,546KiB | 2,006,236KiB | 两个独立进程的端点差，不能据此宣称内存优化；两侧private image均344,166,400B |

预检的进一步归属改变了实施顺序：候选采样源preflight枝仍占NvN的1.985/2.134/2.118%；其中71/90/80%的样本落在源码可证明为普通packaged源的backing、bucket/dirty与CPU-effect路径，而非可直接返回的native marker。`nvn_dirty_since`、逐图像状态及真实CPU源契约仍在；已有pending-effect零计数早退，不能重复提议加同类计数器。source resolve仍约1.63–1.67%。新空证明helper约0.13–0.17%，finish新增证明约0–0.13%，是实际新增工作，不按独立符号消失记成零。样本不能给出缓存命中率、失败次数或资格比例。

保留通知修改的机制和局部实跑收益；空预检候选的正确性合同、实现及新增成本保留，不继续扩大到普通CPU源。下一轮将空预检从主要提速方向降级，回到真实绑定/消费的数据表示和跨层职责预算；当前约24%的binding子树依然含必要资源操作，不能承诺整段可删。当前没有稳定FPS、内存或持续温度改善结论，也没有6GB设备验收。Q已停，Mali仍暂停，A650不新增测试。

证据：`.local/logs/cpu-source-completion-e2d24a7-analysis-v1/result.json`、`.local/logs/cpu-source-completion-e2d24a7-root-official-analysis-review.json`、`.local/logs/cpu-source-completion-e2d24a7-Q735-two-round-root-functional-review.json`。


## 2026-10-22：改动前预算与当前落实

用户要求先给完整职责占比、可删除工作与新增成本，再修改；高频叶函数不能独立决定优先级。最新 BASE1 三窗绑定职责为 23.45–24.09% NvN 用户采样；逐层互斥分解后，表示本体、descriptor 构造与独立 driver set bind 的过宽包络只有 7.55–8.99%，还包括必要的 Buffer、动态 offset 和常量工作。本体中已有 0.91–1.24% 可明确归于内联常量／键构造。原 descriptor 命中约 92.15%、View recipe 约 93.1%，已有 last/history/template/retained set、动态 UBO、source-flow 和 schema 不重复提案。

长寿命 typed image/sampler 表能让不同组合的数据交付更便宜，属于数据表示变化；但新 shader ABI、共同低 set、物理版本退休、参数 ring 和 GPU 索引读取都有成本。当前没有足够工作集／版本变化／能力证据证明净预算，暂不进行大规模改写，不能承诺消除整段 24%。完整只读事实见 `.local/logs/long-lived-binding-representation-BASE1-readonly-v3/{recommendation.txt,facts.json}`。

同时重新整理全部翻译线程，而非只看 draw 内部：已有官方 ReportLib 三窗中，提交规划 `mtl_sub_index` 为 5.18/6.26/6.76%；回放下直接 `stream_flush` 为 4.19/4.50/4.55%。后者包含 commit、结束 Pass 等职责，不能再与子树相加。其中明确物理联合覆盖检查为 1.11/1.80/1.26%，不是整段提交工作都能省。规划首轮与 store index 融合已实现，不再把它当新方案。只读完整父链分区保存在 `.local/logs/cpu-source-completion-e2d24a7-analysis-v1/BASE1/whole-thread-phase-partition.json`，没有新采样、计时或符号解释器；这是用户态权重，不是 GPU／等待墙钟。

独立 guest fence 通知的稳定基线移植 `2ee5e96` 已保留到主线。原同条件真机实验中，guest wait CPU 从约 0.30–0.35ms 降到 0.08–0.10ms／Surface 间隔，显式唤醒约 2500 次降到 250–300 次；这是局部工作减少，未证明整 NvN 或 FPS 改善。未将未证明净 CPU 改善的空来源预检继续扩大到普通 CPU 源。

内存方向的 driver Uniform span 候选已实现并完成原 host GPU 内容／异步验证。原约 20MiB arena 类存储只有在全部实际读取者证明 <=4KiB 时才具有约 1.25MiB 的理想预算；静态 3023 槽／2926 shader 合格不是运行时命中权重，不称 PSS/FPS/CPU 收益。证据与机制见独立候选 `5a5571d` 的 `docs/ANDROID_DRIVER_UNIFORM_SPANS_2026-10-22.zh-CN.md`。

真机候选后继 `dd4c694` 保留当前 Q `09a9` 的 lazy/coverage/JNI/UI 闭包，仅继承稳定 CV、span 及 main 的显式可选 ThinLTO 政策，不含 e2d 空预检。新增启动期 span-only reference，使同一 APK、同一完整 shader 资产只切换 4KiB／64KiB 范围；其他常量键、缓存、arena、依赖和分辨率保持。默认及 reference 的原 Uniform GPU 数值、串行／异步 arena 生命周期和 guest fence/reset 共 10 例已通过，尚待 Android 原游戏对照。新 shader 原入口自然生成的 DPL 版本与旧 950 不同，因此不能直接把换整库前后差值归因于 span。

此阶段没有新的已证整帧提速、持续温度下降或 6GB 设备启动结论。Mali 按用户暂停，6GB 平板不新增设备实验；上机只在产物与原始 NSO 记录完整后进行。

### 高层内存账本与独立范围证明交付

现有同次 BASE1 官方 showmap/dumpsys 改变了内存调查顺序：总 PSS 约 1959.21MiB 中，guest memfd 为 1438.61MiB（73.43%）；同一 4GiB backing 的两次映射合计 8GiB 虚拟地址，不能把两个 RSS 或 Buffer/import extent 再加成另一份物理内存。328.22MiB private-image allocation、345MiB Buffer unique 和 570MiB import extent 是不同口径，不能叠加到 PSS。主堆是下一项数百 MiB 级调查的对象，driver arena 的 18.75MiB 理想预算不能替代它。

原 SDK 日志已有约 685MiB free、约 680.46MiB 最大空闲块，但不含其页驻留、整页所有权、分配器元数据与未完成 GPU 引用交集。当前宿主主堆 resize 只改变保护，不删除 memfd backing；另一个 SDK alias 的既有 MADV_FREE 路径只对应约 4KiB PSS。没有证据证明可安全回收 685MiB，也不能从 PoolFinalize 推导 CPU 存储已释放。不实现没有生命周期证明的打洞或 Free 拦截。详细原始哈希和边界见 `.local/logs/Q-09a-BASE1-resident-memory-readonly-v1/{facts.json,recommendation.txt}`。

新 shader 链 `8b016120f586` 已在原入口完成 6931/6931 构建；基础 SPIR-V、metadata、DDK1 和 specialization 全量字节与原 `9507875007c6` 相同。但是当前工具链的 DPL v2 增加 single-omission 变体，`.plocal` 从 54.656MiB 增至 310.921MiB，增加 256.265MiB；Android 启动默认开启 pixel-local，首次请求会保留该 shader 的整个 blob 与 record 元数据，不能按 C 层 unset=OFF 假定没有驻留代价。额外 driver module 虽按需创建，这个增长仍可能抵消常量槽的内存目标。

因此完整新库不部署，保留其独立验证作为 DSP 证明来源。交付改为原 950 库全部原字节、原 `.chain`、JSON 与 DPL 不变，只增加 6927 份 96B DSP 和独立来源 manifest。补充合同须核原完整部署 receipt、已完成的新库 proof 验证、五类实际模块及指纹字节一致；不更换旧链标签，也不把补充产物冒称为原整库构建。现有整库部署工具没有独立 supplement 合同，新增工具仅覆盖这个产物组合缺口，复用原 proof 解析与原发布/恢复原语，不新增计时或采样。此时仍没有新的真机 PSS、CPU、FPS 或持续温度结论。

### 默认局部渲染导致常量证明静默回退的根因

进一步核对真实 BASE1 日志中的 109 个已加载 shader，其中 108 个带 DDK：原 DDK 的五产物指纹全部与实际资源匹配，但与默认 pixel-local ON 时的 pipeline 指纹匹配数为 0/108；新 DSP 也有相同失配。原因是 public `vk_shader_fingerprint()` 在五产物摘要后追加 DPL，而 DDK/DSP 加载仍直接比较这个扩展后的字段。这是常量证明没有被采纳的静态激活根因，不是新采样或已测 CPU 收益。原始资产、日志及精确计算在 `.local/logs/driver-proof-fingerprint-root-actual-assets-mismatch.json`。

候选 `f75de42` 从真实 dd4 源后继分离这两个身份：创建 shader 时保存额外 32B 原五产物 proof digest，DDK/DSP 比较它；原 public pipeline 指纹、DPL 变化失效及 DSP module token 不变。新的 DPL fragment module 在证明闭包外，保守清除继承的 DDK schema。draw 热路没有新增文件读取、hash 或原子操作。10 组最终实际宿主 GPU/CoreSYNC/ASan 测试通过，原 dd4 二进制对同有效 DPL 资源负控准确复现 schema assertion 失败；它们不代替 Android 验收。根审已核实际源与测试闭包，后续 canonical 和原始 NSO 重建使用新 f75 身份，不重标 dd4 旧产物。正式源记录为 `.local/logs/driver-proof-fingerprint-f75de42-source-ready-root-v3.json`；其中 direct base/source parent 为 dd4，真实 Q09a 比较谱系单列。

### f75 真机范围对照完成：保留容量收益，常量路径退出 CPU 主攻方向

Q735 的 REFERENCE1 与 SPAN0 两次原游戏事务均实际退出 0。使用同一 f75 APK、原始 NSO 重新加工并独立验证的模块、原 950 Shader 库加独立 DSP 补充、f830 驱动、full2838 资源、.60 比例和 2750×1260 输出；输入仅范围开关与输出目录不同。原三段移动、各三份 30 秒 FP 与五份 8 秒 Perfetto、原生命周期 save2、停止和资源恢复全部完成。四张实际 HUD 截图经根目视未见明显缺画面，不代表全部场景或战斗验收。

| 实测项 | 64KiB 范围对照 | 证明合格时 4KiB 候选 | 判断 |
| --- | --- | --- | --- |
| after-tools 常量 arena 容量 | 4MiB，8 chunks | 256KiB，8 chunks | 确定少 3.75MiB，减少 93.75%；保留实现 |
| NvN Running / 完整 Surface 间隔，全部五窗 | 25.983–27.911ms | 26.723–27.806ms | 仍约 26–28ms，未证明整线程提速 |
| 同窗 CPU 加权频率 | 1473–1545MHz | 1540–1571MHz | 频率不同，不作线性归一或跨窗百分比换毫秒 |
| Surface 平均间隔，全部五窗 | 33.098–33.186ms | 32.817–32.953ms | 仍约 30FPS，不能称已经解决帧率问题 |
| 完整 host constant 父职责，占 NvN 用户采样 | 0.468 / 0.567 / 0.605% | 0.599 / 0.769 / 0.558% | 包含子调用及 LLVM 内联归属；不支持继续按大热点投入 |
| meminfo PSS / swapPSS | 1961278 / 69485KiB | 2046579 / 149KiB | 交换驻留及工作量不同，不能用局部容量差声称整进程 RAM 改善 |

两轮 DDK 激活均已修正，因此这个范围对照不隔离 DDK 修复相对历史 09a 的收益。arena slots 98272 / 100609 为累计工作量，不能当作活槽；不能用历史约 20MiB 与本次 256KiB 直接作同条件比较。两轮 private image allocation 精确同为 344166400B。采样下 draw 总职责仍约 72%，绑定约 21–24%，均含必须保留的实际资源消费。

六份 FP 的实际 ELF BuildID 及完整祖先、六次 MONOTONIC→Perfetto 时钟重叠已核；376 个分析输出逐文件校验，保留全部十窗。present scope 的 wall 约 5.7–7.7ms，但实际 Running 仅约 0.61–0.67ms，不能当作提交 CPU 运算；queue-lock marker 没有数据，不能写成锁开销为零。原厂工具数据不支持再把完整常量路径包装成数毫秒 CPU 优化。

下一项内存调查转向占用更大的 guest 主堆。原 SDK 仍报告约 685MiB 逻辑空闲，但缺少空闲 span 与实际驻留的交集。已准备复用官方 LLDB，在原有每 300 帧统计自然进入、SDK 分配器锁已持有的位置只读元数据，结合两个同 backing CPU 映射的 pagemap；不调用游戏函数或先行回收页面。即使观察到 free-present，也只是 CPU 映射驻留下界，不解除 GPU／文件引用，不直接授权打洞。该快照尚未执行，持续散热和 6GB 稳定启动仍未验收。

原 APK、模块、设置和全部原 Shader 字节已恢复，测试游戏已停止；额外的设备端 Shader 副本也在完整库存核对后删除。Mali 与 6GB 平板未访问。候选仍是独立的已备份分支，不能将测试期间的临时安装说成主线默认包已更新。

实际记录：`.local/logs/driver-proof-fingerprint-f75de42-Q735-pair-root-functional-review.json`、`driver-proof-fingerprint-f75de42-root-official-analysis-review.json`、`driver-proof-fingerprint-f75de42-analysis-v1/{comparison.json,summary.md}`、`SDK-free-resident-snapshot-plan-v1/{plan.md,facts.json}`（均在 `.local/logs/`）。

### 完整路径预算：不再把高频叶调用当作优先级

当前没有新的、已证明可安全删除且足以解释约10ms差距的重复工作块。f75六份完整FP的绑定职责仍占21–24%，其中实际view、Buffer范围、依赖与引用消费不能因缓存命中而省略。更早BASE1对表示本体、descriptor构造与set交付的7.55–8.99%只是过宽包络，包含必须保留的工作；不能宣称能省整段24%，也不将旧版占比乘本版Running生成精确毫秒。

更高层次的实施顺序是：先证明必要工作是否能用更便宜的数据表示完成；再证明能否在完整setup之前减少合法draw入口；同时区分指令量和指令/数据缓存停顿。已有dynamic UBO、schema、recipe、history、预展开planned_op及融合规划不重复建设。新表示必须列出物理版本、真实consumer、GPU索引读取及退休成本；合批必须证明原NVN邻接、query、storage与alias顺序。当前Q记录2312draw，严格相同状态连续组为0；只放松IBO偏移最多少174个入口，这还不是174个合法合批，更不是7.5%线程收益。

已确认的重复操作例子是完成通知唤醒错误的waiter群体。此前实际guest等待CPU从0.299–0.352ms/Surface降至0.080–0.097ms，属于约0.2ms量级局部减负，已经保留；不能拿它解释NvN约27ms。空CPU源预检候选虽然机制可界定，实际完整职责未下降，不能继续预支其改前约5%为收益。常量完整父职责本次已低于0.8%，退出CPU主攻。

整APP审查也未发现大型宿主忙等：原SDK同步父包络约2.32–3.30%、HLE同步约1.63–2.22%、宿主门约3.41–4.07%整APP用户采样，互相重叠，不可相加；线程根已排除。原任务图让出函数还执行真实任务，不能当idle删去。三个guest线程频繁唤醒但主要阻塞睡眠，现有样本不能证明所有唤醒必要，也不能把未知guest PC都归为无法改进的游戏算法。完整证据为`.local/logs/driver-proof-fingerprint-f75de42-whole-app-readonly-v1/{summary.md,result.json}`。

### SDK内存快照已完成：容量调查独立于CPU提速结论

使用官方LLDB在原SDK自然GetMemStats调用、分配器锁已持有的位置只读取得完整6340个span与两个同backing映射的PTE；不调用目标函数、不修改或回收页。189份原始文件哈希/长度均核验。已分配逻辑3024.04MiB，两个CPU映射present并集1399.98MiB；逻辑空闲685.09MiB中该并集14.24MiB。无CPU present PTE不是物理不驻留证明，也不是回收授权。

最大四个已分配span贡献约798.97MiB CPU-view present，约占已分配并集57.07%。缺少同次运行NVN Pool归属，不能按块大小猜用途、拼旧进程地址或直接打洞。这项是工作集/内存生命周期调查，不冒称已缩短翻译CPU。原游戏移动、存档2次、退出及原包资源恢复完成；最终核实游戏PID为空，调试器自有forward已移除。工具源码独立备份于`codex/sdk-free-resident-tools-v1`的`118920a`。

原始快照、离线重放和根核验分别为`.local/logs/sdk-free-resident-Q735-v1/game-v4/native/readonly-memory-snapshot/sdk-free-resident.json`、`.local/logs/sdk-free-resident-Q735-v1-offline-analysis-v1/report.md`、`.local/logs/sdk-free-resident-Q735-v1-root-completed-review.json`。本节没有新增CPU提速、FPS或持续温度收益结论。
