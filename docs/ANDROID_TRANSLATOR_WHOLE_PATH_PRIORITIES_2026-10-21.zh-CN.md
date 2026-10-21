# 翻译线程：按整条调用链安排优化

固定绑定 schema、图像请求阶段边界、首次 walk 融合 store index 和构造阶段外提已完成真机对照，尚未证明整帧 CPU 或 FPS 净收益。整池共享对照和完整2,838项 ASTC6 资源的原游戏短测也已完成。已完成共享路径中的旧 Pool 扫描、比较、镜像上传计数为零；不能继续把该路径的 CPU 时间归因于已经消失的拷贝。现有证据不支持阻塞锁是主要原因。

当前实施顺序：

1. 保持1MiB有限窗口，缩减每draw真实资源准入和消费的重复工作。统一版本179c82d复用固定绑定元数据、跳过coherent native分配的无效cache遍历、借用当前recording已有的Pool引用；跨commit前升级独立pin，保留全部物理覆盖与数据依赖。host内容/重入/异步/负控、canonical/fresh raw28及实际游戏短测均完成，未证明净CPU/FPS收益。宿主ThinLTO已完成相同资源的原游戏对照：整线程Running约27.36ms vs27.51ms、频率约1577 vs1583MHz，仍约30FPS。编译选项保留为默认关闭的实验入口，不继续以编译微调为主线。
2. 完整ASTC6混合包已进入原游戏并完成三段跑动、存档和恢复；全6217 native payload从443,588,608降到373,624,832B，减少66.72MiB。实际营地NvN Running约26.68ms/完整Surface间隔，出图约33.52ms，未证明净FPS或进程PSS下降。保留资源表示收益，继续测实际加载范围与持续成本，不把资源大小当DRAM或温度。
3. 进一步评估是否能把Submission/可证明片段中的元数据和几何准入一次交付给多个draw。先查已有walk、source_flow、geometry和recipe边界，不盲加跨draw缓存；低覆盖的分类删除不足以将约27ms直接压到16.7ms。native GENERAL小候选已独立源码审查及8项GPU/ASan内容测试，179后继43e856f正在canonical/raw构建；只减少准备和纯布局分类，仍执行全部真实消费/依赖，宽包络3.074%含大量保留工作，不是预期收益。
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
