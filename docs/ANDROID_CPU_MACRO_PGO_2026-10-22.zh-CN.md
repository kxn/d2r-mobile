# 翻译线程宏观预算与样本引导编译对照

本轮不新增逐 Draw 缓存。问题从“还有多少重复操作”转为三件事：必要工作能否采用更便宜的数据表示、合法绘制入口能否减少，以及必要代码的取指／访存／执行成本能否下降。

## 先限定可得预算

现有真实 Q735 数据中，NvN 每次呈现约有 27–28ms Running；到 16.7ms 需要约四成减少，但没有已证明可删除的同等规模重复工作块。

- 绑定整个父职责约占 21–24% 用户态样本，包含实际资源、依赖及寿命消费。旧版表示构造／Descriptor／set 交付的 7.55–8.99% 只是混合包络，仍含必要工作；假设能减少后者一半，条件算术也仅约 4%，不是已测收益。
- 当前 2312 Draw 记录中严格相同状态相邻组为零；仅放松索引偏移后的 174 个入口不是合法合批数，也不是线程可省比例。没有相应语义证明前不实现命令缓存或重排。
- 已保留完成通知的局部 CPU 减少，约 0.30–0.35→0.08–0.10ms；Store 记录数量减少约 95% 亦保留。二者不能解释整个约 10ms 缺口。常量父职责实测低于 0.8%，不继续当 CPU 大热点投入。
- 历史取指停顿比例不是当前版本预算；需要当前官方 PMU，而不是将采样符号份额变化解释为操作被删除。

完整来源和限制见 `ANDROID_TRANSLATOR_WHOLE_PATH_PRIORITIES_2026-10-21.zh-CN.md` 及其中的原官方数据引用。

## PGO 四轮已完成，没有证明整线程净收益

原入口构建的候选 `33d2d8a` 与父版 `f75de42` 仅编译策略不同，C/C++/ASM、Shader 与游戏规则不变。候选完整 `.text` 增加 54,696B，约 1.93%；AppliedSamples 表示编译器消费样本，不表示省下指令或 CPU 时间。

Q735 按父／候选／候选／父顺序执行。每轮原游戏三段移动、三份 30 秒官方 FP 记录、五份 8 秒 Perfetto、原存档两次、退出和恢复均完成，四轮控制器实际退出 0。相同 0.60 比例、2750×1260 输出、f830、原 950 Shader 和 full2838 资源；这些不是日常默认 0.75 的直接性能结果。

| 顺序 | NvN Running ms/Surface | Surface 平均 ms | NvN 睡眠 ms/Surface |
| --- | ---: | ---: | ---: |
| 父版 1 | 27.754 | 33.060 | 5.121 |
| 候选 1 | 28.060 | 33.388 | 5.166 |
| 候选 2 | 27.280 | 33.942 | 6.473 |
| 父版 2 | 27.891 | 33.417 | 5.349 |

表中是各轮按完整 Surface 数加权的描述值，全部 20 窗保留，不是独立重复实验的置信区间。候选 1 未降低 CPU；候选 2 较低 CPU 伴随更长出图间隔、更多睡眠及核心／工作量差异，不能归因为 PGO。位置、PSO、频率和核心分布不同，不能线性按 MHz 归一。父版 2 首书挡的 PSO 174→181、variants 31→33 没有剔除。

12 份记录均无报告丢失／截断，12 次官方时钟重叠校验通过。父版 2 第二份记录有一条真实 `pthread_cond_broadcast→__pthread_start→__start_thread` 栈缺少 `x_thread`，原样保留；其余记录该祖先完整，不能补造缺帧。根复核 951 个原始／工具／分析文件的哈希。

决定：保留可选构建、来源和内容正确性资格；不将 PGO 默认启用，不称已经提速，也不推断 PGO 在其他场景绝对无效。四轮都恢复到已实际交付的 a322 日常共享内存／原生纹理组合；实际设备上的私有启动配置和收条哈希未变。

本机证据：`.local/logs/host-sample-profile-33d2d8a-pair-analysis-v1/{comparison-ready.json,comparison-all20-windows.json,summary-all4-rounds.md}`、`.local/logs/host-sample-profile-33d2d8a-ABBA-root-review.json`、`.local/logs/host-sample-profile-33d2d8a-post-ABBA-private-files.json`。

## 修改前的预算与优先级

当前不是已经找到约 10ms、只等删除的重复工作。27–28ms Running 到 16.7ms 需要减少约四成；单个叶节点或重复绑定次数不能解释这个差额。阻塞锁不消耗 Running，因而不能将这 27–28ms 大头解释为阻塞锁等待；自旋等消耗 CPU 的行为仍须看实际调用栈，不能仅凭 Running 排除。

优先研究的是改变必要工作的处理粒度和表示，而不是继续叠缓存：

| 方向 | 现有预算与限制 | 实施顺序 |
| --- | --- | --- |
| 将稳定资源的准备归到整个 Pass，逐 Draw 只处理真正变化和实际依赖 | 依赖／history 整段约 4.96–5.34%；扣除已有 range／stage／generation 早退后，剩余 request 入列和 history 外层 self 混合包络仅 1.03–1.28%，仍含必要工作，未扣新增版本／成员／寿命检查，净下界为零。 | 审计后降为低优先级，不实现新 Pass 缓存；不能拿静态重复次数放大预算。 |
| 长寿命资源表示与逐 Draw 动态参数分离 | 表示构造／Descriptor／set 混合包络约 7.55–8.99%。即使能省一半，条件算术约 4%，尚非预测。已有 schema、dynamic UBO、recipe、source_flow 与 history 不能重复计算收益；还须扣新增参数上传、GPU 访存及失效成本。 | 比单个查找缓存优先，必须先找出现有表示仍迫使重建整段数据的实际输入。 |
| 缩小必要代码与数据工作集 | 当前 PMU 已确认前端／取指成本存在，但 L1I 相关 17.405% 不是可回收预算；PGO 已有四轮无一致净收益，LLD 也已消费调用图排序，不能再假设这些能力未开启。 | 按完整 draw 路径审视热路径代码和数据访问形态，不再盲加编译开关。 |
| 附件不变元数据在仍活动的 Pass 内复用 | 较明确的视图／描述构造站点约 0.47–0.90% 用户态样本，较宽且含必要工作的乐观包络 1.03–1.63%；新增校验及回退后净收益未知。 | 小预算候选，不能作为解决十毫秒差额的主线。 |

最后一项的约 5.6–6.5% 附件父职责大部分是必须执行的布局、访问和依赖处理，不能全部删除。新 Pass 也不能复用旧 Pass 的一次性 CLEAR／DONT_CARE 描述。现有对象缓存已避免重复创建 RenderPass／Framebuffer／ImageView，候选不再重建相同缓存。详细合同与三个原 FP 窗的预算在 `.local/logs/f75-active-pass-metadata-readonly-v1/contract-and-findings.json`。

实际旧 Q 帧有 2,312 Draw／50 Pass；静态采样声明出现 10,701 次，按 Pass 和精确图像／范围／阶段／布局归类为 609 个组合。这仅说明资源身份具有复用结构，不是实际 Shader 读取次数，也不是可跳过 94% CPU 工作的证据。必须排除已做的早退、CPU 并发写、别名、反馈及实际 GPU 写入；不能按这些计数开写大改造。严格相同状态相邻组为零也意味着不能把通用合批作为现成大收益。

只读 Pass 审计最终证据为 `.local/logs/f75-pass-image-read-scope-readonly-v1/{ready.json,contract-and-budget.json,findings.txt}`。其中 254 项输入／源码／官方工具输出均已核对哈希；旧帧不能当当前场景命中率。仅 1,208 次静态声明能确认对应 host-imported 图像，其余不能直接认定 GPU-only。读取入列时已覆盖也不能无条件删除：同一 Draw 后续 writer 可能清理旧 reader，当前实际读取仍要正确恢复 WAR 依赖。

## 当前 PMU 与大块内存来源

另一个原 f75 游戏事务已实际采集基础、core、memory、front-cache、front-stalls 五组官方 Simpleperf 计数，以及独立 cpu-clock、raw L1I refill 和 Perfetto。控制器实际退出 0，原存档／停止／恢复完成。基础 stat 窗 PSO 172→179，后续记录／硬件计数／scheduler 的 bracket 为 179；分析保留这一差异，并按原输出核对事件支持、每核心有效计数及 multiplex/scaling；不能把 cache-miss 或 bus-cycles 当实际 DRAM 字节。采集源为 `.local/logs/f75-current-PMU-a322-baseline-v1/game/`。

完整官方分析现已完成，根复核 116 项原始／工具／分析文件。core 窗的 NvN 用户周期为 9.092B、退休指令 18.568B、IPC 2.042，前端停顿约 26.875%、后端约 23.553%。独立 front-stalls 窗前端约 26.492%、L1I 相关约 17.405%；事件可能重叠，不能相加或当成可回收时间。front-cache 窗 L1I access 4.811B、refill 556.719M，比值约 11.571%。memory 窗 generic cache miss/ref 约 4.915%，没有独立 L2／DRAM 字节计数，未核 OEM 的全部事件语义，不能据此宣称已经证明带宽瓶颈。

四组 verbose PMU 的实际每核心原始计数逐事件相加与报告一致，聚合 running/enabled 约 100%；保留异构核心差异及原工具的 childcmd 拒绝日志，不能据最终有数就省略限制。正常 DWARF 栈 634 个 NvN 样本中 495 个保留 `x_thread`，独立 raw refill 栈为 547 中 455 个；无丢失／截断不代表所有父链都完整。raw refill 的 self PC 约 76.23% 在 host、15.90% 在驱动；它们不是整个 L1I 停顿成本的归属证明。独立 scheduler 窗 NvN Running 27.971ms/完整 Surface，不能用该帧数去除先前独立硬件计数。

这将代码／数据工作集及必要代码的执行形态提升为当前研究重点；没有新增可省 17% 的承诺，也不因 PGO 第一种实现未见净收益就将取指问题排除。证据为 `.local/logs/f75-current-PMU-a322-analysis-v1/{ready.json,facts-and-priority.json,PMU-counts-and-coverage.json,summary.txt}`。

内存调查继续针对同停点 SDK 快照中 NVN Pool 外两块大分配，250MiB 与 260MiB 的 CPU-view present 并集合计约 382MiB。这不是可释放或独占物理 RAM 的证明。原 SDK 分配来源调试首轮在 native 启动前因 `adb jdwp` 等待 EOF 超时，未进入 LLDB/JDB；原失败保留，确认未启动 native 后退出并恢复，无存档成功宣称。工具后继 `a31c333` 仅有界消费原 ADB 输出的精确完整 PID 行，保留真实 stdout/stderr/EOF/退出状态并清理自己的本地 reader；原 SDK 244B、断点／读预算、官方 JDB/LLDB、后路快照及存档恢复不变。其 host 检查和离线资格不代表已经取得分配调用栈。

本轮没有新的持续散热或 6GB 稳定游玩验收，目标仍未完成。Mali 与 6GB 平板未访问。

## 后续实施：缩小绘制热函数，而非增加缓存

对精确 f75 的官方 LLVM inline ranges 继续审查，主绘制函数 22,356B 中，BQ 匹配约 4,696B、PL 准备约 1,428B、PL 消费约 864B、实例展开正分支约 1,304B，另有约 228B trace 正体。这约 8.5KiB 是包含共享分支及错误路径的静态范围，不是可省 CPU 百分比。现有独立 `pl_plan`、管线创建及早先冷阶段外提不重复认领。

独立 `codex/draw-cold-positive-phases-v1` 以已构建的 930 修复版为基线，实施 BQ、实例展开和 PL 成功路径的完整阶段外提。普通／禁用绘制的拒绝判断仍留在热路径；不增加 Draw 数据副本、资源缓存、分配或跳过依赖。原字段与参数直接传入独立函数，实例展开提前返回时仍由原调用者完成 operation/local-ready 清理。

同 Clang 19.1.7、原 host O1/no-LTO 的实际 x86-64 机器码显示：主 Draw 23,723→15,041B，减少 8,682B；四个独立函数合计 9,099B，合并计算总代码反而增加 417B。主函数栈仅减少 16B。机制是缩小普通绘制路径所混入的代码体积，不是删除真实绘制计算；完整正分支新增函数调用，最坏总栈深可能上升。这些不是 Android AArch64 数值，尚无 CPU/FPS 或散热收益证明。原内容／GPU／sanitizer 资格正在独立进行，结果及命令位于 `.local/logs/draw-cold-positive-phases-v1/`；静态合同在 `.local/logs/f75-draw-cold-stages-readonly-v1/`。

后续源码已冻结并推送 `cb297f8`，六个原 host GPU 内容／Core/SYNC／sanitizer 用例通过。原 Android canonical 构建和六库、7 个 APP 预处理单元、ABI、签名核验也已完成，根实际核验 76 项引用及 2,796 个归档 producer/NDK 输入。**实际 AArch64** 主 Draw 为 22,356→15,612B，减少 6,744B；四独立函数为 8,288B，五函数合计增加 1,544B，全库 `.text` 增加 1,008B。主函数栈为 4,224→4,160B；正分支调用链额外栈分别为 BQ 1,728B、实例展开 128B、PL 准备 112B、PL 消费 64B（不含双方相同的深层被调用函数）。不将 host 尺寸代替 Android，也不把静态文本减少称为 CPU 时间减少。根记录在 `.local/logs/draw-cold-positive-phases-cb297f8-root-canonical-review.json`；原 NSO 已重新生成并独立校验，随后完成下节的四轮原游戏对照；没有将该 CPU 候选静默替换为日常默认。

## 内存归属：从全分配器断点转为少量已证记录

原 main 的静态调用链已确定三个 TLSF 子分配器的 backing 记录：250MiB 的 pointer/size 位于 `main+0x31162e8/+0x31162f0`；260MiB 的记录位于 `+0x31dafb0/+0x31dafb8`，原名称为 `Popcorn Asset Pool`；另有 40MiB `Popcorn Scratch Pool`。分配参数、返回值存储、TLSF 初始化及两个原名称的关联均有原始指令／字符串证据，不能再将其仅描述为按尺寸猜测。

历史 SDK 快照没有读取这些全局，不能跨 PID 关联旧指针。本次已在原 SDK/Pool 同停点补读固定记录，额外 810B，归一 backing offset 并核对前后记录、原始指令和 930 ELF/DWARF；三个实际指针均与本次 SDK allocated span 精确匹配，无当前 NVN Pool 交集。没有重新布置高频 Allocate 断点，也没有遍历 TLSF。大 arena 是子分配器 backing，不意味着其中所有子块有效，更不意味着可直接释放。静态证据见 `.local/logs/sdk-large-nonpool-origin-static-v2/`，实际捕获和根复核见 `.local/logs/sdk-main-arena-provenance-930-Q735-v1/root-actual-memory-review.json`。

另一项预算修正：在本次 f75 PMU 日志实际出现的 110 个 Shader key 中，旧 950 库的 33 个非空 DPL 文件总计 3,326,480B，新 8b 库对应文件为 13,912,752B。它们仍只是所有这些 key 均实际请求 lazy recipe 时的 blob 上界，不是已观测常驻量／PSS；新库全库存约 256MiB 的磁盘增长不能直接写成实际多驻留 256MiB。新 subset 模块、元数据和驱动分配仍须单独验证，当前未部署新库。原文件哈希与统计在 `.local/logs/f75-loaded-shader-DPL-residency-budget-v1/facts.json`。

## 共享 B2T 稳定性修复已通过一次原游戏功能验证

V5 恢复期间的实际 f75 崩溃位于 B2T 调用的 `image_upload` 源预检保护。原 GPU 夹具能通过真实 clean 查询后发生真实同值 CPU 写，复现相同 FATAL；窄修 `930ac8f` 只要求 B2T 在借用物理图像前完成既有源可见性准备，保护条件及普通采样 clean 快速路径不变。八项原 GPU 内容／ASan 用例通过，旧生产代码在相同最终夹具上仍失败；真实最初崩溃的具体纹理 serial 和暂停因果仍未知。

930 已由原 Android 入口构建，并从三个未修改 NSO 重新生成、独立校验全部 28 个交付文件，游戏可执行字节与 a322 相同。Q735 原游戏 PID 3782 已完成三段真实输入跑动、营地画面查看、原生命周期存档两次和退出，控制器终态 0、原日常 a322 APK／配置／资源恢复，cleanup 为空。此轮仅验证正确性与系统内存报告，没有 CPU/GPU 计时采集，不宣称提升帧率、持续散热或 6GB 稳定运行。证据为 `.local/logs/shared-b2t-ordered-preflight-930ac8f-root-actual-functional-review.json`；分支 `codex/shared-b2t-ordered-preflight-v1` 已备份到 fork，当前日常包没有被静默替换。

## 热路径拆分的四轮原游戏对照已完成

对比父版 `930ac8f` 与拆分版 `cb297f8`，保持原 O3、无 LTO/PGO、0.60 比例、2750×1260 输出、target60、f830、full2838/950、1MiB/lazy、span reference 和 ADPF reference。按父／候选／候选／父顺序，每轮原三段移动、三份 30 秒 99Hz FP 记录、五份 8 秒 Perfetto、原存档两次、停止及恢复均实际完成。四轮控制器退出 0，根核验 945 个原始／官方工具／分析文件。

| 顺序 | 完整 Surface 数 | NvN Running ms/Surface | Surface 平均 ms | NvN Sleep ms/Surface |
| --- | ---: | ---: | ---: | ---: |
| 父版 1 | 1176 | 26.802 | 33.043 | 6.013 |
| 候选 1 | 1140 | 26.672 | 34.014 | 7.038 |
| 候选 2 | 1163 | 26.867 | 33.499 | 6.379 |
| 父版 2 | 1182 | 26.819 | 32.993 | 5.939 |

没有证明稳定的整线程 CPU、FPS 或发热收益；不能将主函数体积减少 6,744B 当作运行时间减少三成。所有 20 窗均保留，不挑选较好的候选 1。CPU7 分布、频率、坐标和睡眠有差异；前三轮 scheduler bracket 为 181PSO，父版 2 前三窗 172、后两窗 179，30 秒 record2 还可跨越准备工作。不能声称完全同工作量，也不按 MHz 线性修正。

12 份原记录 lost/truncated 为 0，官方时钟映射均覆盖相应 Perfetto。父版 1 record2 有一个样本缺少 `x_thread`；保留真实父链，不补造。现有完整职责仍以资源绑定、源准备、附件与访问依赖等必要消费为主；不能把其中某个叶节点份额降低解释为整个职责被省掉。保留拆分源码、内容资格和静态工作集证据，不因 FPS 单独撤回，也不宣传已省 CPU。实际日常 a322 APK、私有配置和收条经根 ADB 哈希复核未变，游戏已停止。

证据：`.local/logs/cpu-draw-cold-cb297f8-pair-analysis-v2/{comparison-ready.json,comparison-all20-windows.json,summary-all4-rounds.md}`、`.local/logs/cpu-draw-cold-cb297f8-ABBA-root-review.json`。首次离线编排误将字节相同的脚本也要求发生变化，导致 binary cache 尚未创建就中断；v1 失败保留，v2 仅修主机路径／身份配置，全部原始采集未改，不额外采集。

## 当前内存分账与两个独立候选

930 的 PID14215 原 SDK 自然锁定停点已完成三个 main arena 关联。容量 250/260/40MiB，CPU 两映射 PTE-present 并集分别 206.777/132.375/7.977MiB，合计 347.129MiB，全部位于当前 NVN Pool 之外。它们不是图形层重复镜像；也未取得 TLSF 内部活对象或回收权限。

同次官方系统统计总 PSS 为 1959.871MiB，guest heap 两 alias 的 PSS 合计 1391.590MiB，约占 71%。SDK allocated PTE 并集 1374.008MiB，其中 Pool 内 448.477、Pool 外 925.531；扣除三个已关联 arena 后，仍有 578.402MiB 非 Pool 并集未归属。系统统计与 SDK 停点不是同时采样，PSS、PTE 并集及 VMA/import allocation 不能直接相加。逻辑 free 684.449MiB 只有 8.695MiB CPU PTE-present，不能据其逻辑容量预支回收收益。

私有图像 allocation 330.176MiB，其中 CPU-rebuildable 为 120.945MiB。原预算 512MiB 只比较后者，因此这次 released 为 0。独立 64MiB 预算候选的当前 allocation 减少上界约 56.945MiB，下界仍为 0，实际 PSS 和重建开销待测。只沿用原非 external、可重建、已完成且至少冷 256 个完成 chunk 的资格，不删除 native imports 或 GPU 写入结果，不增加 GPU 等待。32 次扫描、4 次移除有计数上限；8MiB 是循环前检查，整图释放可越过该阈值。VMA 保留空 block 也可能使 allocation 下降却未减少驻留。该候选与 CPU 候选分开，不改变当前日常默认。

CPU 的 `88620ec` 候选则将提示未命中的物理 Buffer 选择从逐节点扫描换成复用已有区间树。它保留最新 cover 优先、否则单个 canonical、原提示／零长度／维护路径以及全部初始化、依赖和寿命。节点仍 64B、span 仍 152B，没有新缓存或分配。10 项原 GPU 内容、异步、非 lazy 与 sanitizer 回归通过；实际 Android canonical、7 APP、六库 16KiB 及三个原 NSO 重新生成／独立校验 28 产物均已完成，尚未有这项的真机 CPU 收益。预算仅为 resolver 自身约 2～3% 样本的一部分，不是整个 pool_address 的约 9%，最坏嵌套区间仍可能线性。

内存来源和原冷预算合同见 `.local/logs/sdk-main-arena-930-memory-budget-v1/`、`.local/logs/private-image-budget-vma-contract-930-cb-readonly-v1/`。区间索引源码已备份 `codex/lazy-physical-span-index-v1`，根源码／canonical／RAW 复核见 `.local/logs/lazy-physical-span-index-88620ec-root-canonical-raw-review.json`。本轮没有新的持续散热或真实 6GB 稳定游玩验收，目标仍未完成；没有访问暂停的 Mali 或平板。

## 索引四轮完成：相同父版的系统状态变化成为优先调查项

物理 Buffer 区间索引已完成父／候选／候选／父四轮真实游戏事务，全部原存档两次、停止、恢复和 cleanup 通过。根复核全部 941 项原始、官方工具及分析文件；实际日常 a322 APK、私有启动配置和收条未变，游戏已停止。

| 顺序 | NvN Running ms/Surface | Surface 平均 ms | NvN Running 加权 MHz | NvN CPU7 Running 占比 |
| --- | ---: | ---: | ---: | ---: |
| 父版 1 cb297 | 25.738 | 33.790 | 1677.3 | 15.03% |
| 候选 1 886 | 26.996 | 33.434 | 1568.3 | 7.75% |
| 候选 2 886 | 26.978 | 33.537 | 1556.0 | 6.35% |
| 父版 2 cb297 | 35.520 | 48.110 | 1264.2 | 0.93% |

上表按完整 Surface 数加权，仅作描述。两个**相同父版**的 Running 相差约 9.8ms，远大于此索引仅约 2–3% resolver SELF 子集的事前包络。不能用父版均值或较慢的末轮宣称候选节省约 9ms，也不能将末轮退化归为候选回归。候选中的递归查询和剩余 legacy resolver 仍有成本，内联选择仍在 `pool_address` 中；局部 PC 样本份额下降不等于净 CPU 时间下降。

已对相同 20 条官方 Perfetto 做整机调度／频率 SQL 审计，无新增设备采集或自计时。末轮大核整体 Running 加权频率约 1.2–1.33GHz，首轮约 1.56–1.68GHz；CPU7 整机也几乎空闲，并非被别的进程占满。NvN Runnable 约 0.35ms，没有相应增加；末轮外部 CPU 占用亦未增加。已有 submit lock 约 0.02ms、present lock 约 0.003ms/Surface，量级不足解释这一差额。没有发现新出现的主导调用父链。

这是频率／驻核状态变化的证据，**不是温控原因的证据**。末轮公开 thermal 日志为空，不能当作 status 0；窗口外相同的最大频率快照也不能排除窗口内或厂商内部策略。两父位置、PSO 和命令形态略有不同，没有相同指令数证明，不能按 MHz 线性归一，不能保证状态变化解释全部差额。睡眠及完成等待相位也变化；跨线程、可嵌套的 wall marker 不能再相加成串行帧时间。

因此后续优先级调整为：

1. 先解释并核对同父系统状态的可比性，区分必要工作量、实际执行速率和等待相位。现成 FP／Perfetto 已能暴露这一区别；不先绑核、升频或关闭温控。已有同机 ADPF 有效会话的 OFF／ON／OFF 未显示加速，不能把重复开启它当作尚未尝试的新方案。
2. 结构改造只针对完整职责：长寿命资源表示与逐 Draw 参数分离是方向，但已有 schema、dynamic UBO、recipe、source_flow、history 必须扣除。绑定整块 21–24% 不是可删预算；旧表示／descriptor／set 交付混合包络仅 7.55–8.99%，即使假设减半，也只是约 4% 的条件预算，且尚未扣新增 GPU 索引访存、参数写入和版本退休成本。没有净预算前不进行大规模 shader ABI 改写。
3. 缩小代码／数据工作集属于降低必要工作的执行成本。冷阶段外提与 PGO 均已有实际对照，没有稳定整线程收益，保留真实结果，不重新包装成未做的大收益。
4. 继续降低内存驻留与搬运，但单独计账。64MiB 可重建私有图像预算已完成源码、canonical 和原始 NSO 资格；按现有 allocation 的减少上界约 56.945MiB、下界为 0，尚无真机净 PSS 或热收益。重建成本和 VMA 保留 block 可能抵消收益，不能将其当作直接减少翻译线程 10ms 的方案。

当前没有另一块已证明可删除、足以消除约 10ms 的重复工作。已实现的 Store 记录从约 1.44 万减至约 720/submit、区间扫描改为查询既有索引，分别是减少表示工作和改变查找复杂度；它们有独立机制证据，但不能代替宏观预算。后续每个候选先列明完整父职责、可变部分、新成本、条件上界和净下界，再修改。

证据：`.local/logs/cpu-lazy-physical-88620ec-pair-analysis-v1/`、`.local/logs/cpu-lazy-physical-88620ec-ABBA-root-review.json`、`.local/logs/index-ABBA-parent-DVFS-drift-readonly-v1/{ready.json,report.md,root-review.json}`。审计只读既有工具数据，没有访问 Mali／平板；本节不新增持续散热或 6GB 验收结论。

## 512→64MiB 缓存预算：四轮真实内存与 CPU 对照完成

父版为 `88620ec`，子版 `74d7e17` 仅降低原 CPU-rebuildable 图像软预算，不改变已完成／冷龄／CPU 可重建／非 external 的原资格，不释放 GPU 权威数据或 native imports。四轮仍用同 f830/full2838/950/.60/2750×1260/target60，实际运行原游戏、三段原输入、3×30s 官方 FP、5×8s Perfetto，再原存档两次、停止、恢复。原内置 VMA 仅在最后正常暂停／drain 时请求一次；没有新增采样或计时器。

| 顺序 | NvN Running ms/Surface | Surface ms | 前台 PSS KiB | 暂停后 VMA Block MiB |
| --- | ---: | ---: | ---: | ---: |
| 父版 1 | 27.028 | 33.538 | 1,988,516 | 412 |
| 子版 1 | 26.393 | 33.276 | 2,108,044 | 348 |
| 子版 2 | 26.874 | 33.123 | 1,985,491 | 348 |
| 父版 2 | 26.468 | 33.465 | 1,984,058 | 412 |

110 个原预算端点核对实际策略正确。结束时父版私有图像分配均 346,214,400B，子版 286,461,952/286,494,720B；两子分别少约 56.98/56.95MiB，变化全部来自 CPU-rebuildable 类别，其余私有类别同为 219,394,048B。两父暂停后 VMA 均 15 block、两子均 13 block，保留 block 准确少 64MiB。这是重复观察到的分配和 allocator footprint 收益，不能当作物理驻留或 DRAM 带宽节省。

前台总 PSS 没有稳定下降：同一子版两次就相差 122,553KiB。子版 1 对父版 1 的增长中 EGL/GL mtrack 合计 +79,384KiB，顺序采集的 showmap 映射 PSS +46,183KiB，主要在 guest memfd；不同统计阶段的残差保留，不强凑唯一物理 RAM。子版 2 没有重复同样的 mtrack 增长，但仍不能将子版 1 剔除或强归为无关系统噪声。暂停后四轮 VMA live allocation 均约 246.8–246.9MB；它与早期前台私有图像计数不同阶段、不同全集，不相减认领回收量。

子版 1 仅首窗累计预算释放 +626,688B，此后三份 FP／末窗不变，子版 2 五窗均不变；没有该城镇稳态持续淘汰的证据。此前加载／跑动期间的累计释放，以及其他区域重建次数／字节／成本仍未知，不能把累计销毁量当净 PSS 节省或零重建证明。CPU 也没有一致候选优势：子版 2 比末轮父版更慢；PSO、位置、频率和驻核有差异，不用小均值差宣称加速。

全部 20 窗、12 份 FP 保留，health 无错误、lost/truncated 为 0，12 次官方时钟重叠通过；父版 1 record2 的一条真实缺 `x_thread` 样本没有补祖先。根复核官方分析 950 项及内存审计 150 项原始引用；两子原有营地截图已实际查看，未观察到黑块或几何损坏。四轮原保存／恢复／cleanup 均完成，根 ADB 实际确认 a322 APK、私有 profile/receipt 未变且进程停止，没有访问 Mali 或平板。

保留 64MiB 候选及其有证据的分配减少，不因 FPS 未涨将其判无效。总 PSS／重建成本的未解边界仍需调查，暂不替换日常预算。另行将已完成内容、生命周期及真机资格的 930/cb/886 底层改动纳入 Q 本地正常启动交付，独立验证两次无 extras 桌面冷启动；不将这一交付写成热、FPS 或 6GB 验收。

证据：`.local/logs/private-image-budget-74d7e17-pair-analysis-v1/`、`.local/logs/private-image-budget-74d7e17-ABBA-root-official-review.json`、`.local/logs/private-image-budget-74d7e17-memory-analysis-v1/{all4-ready.json,all4-report.txt,root-review.json}`、`.local/logs/private-image-budget-74d7e17-post-ABBA-private-files.json`。整体持续游玩散热与 6GB 稳定运行目标仍未完成。

## Q 日常版本已实际更新为 886，缓存预算仍为原值

`88620ec` 已通过原 publisher 实际安装为 Q735 正常桌面版本，原进程终态 0。两次原 `MAIN/LAUNCHER` 冷启动（PID25720/26432）无 Intent extras，均实际观察 Java profile 消费、原游戏 HUD、Turnip/shared/native 策略，再由原生命周期存档两次并停止。原目录／文件 CAS、安装器和失败回滚保持；存档不回滚，原 950 Shader 与用户 HUD preferences 未改。实际普通启动比例均 0.75；上节受控采集为 0.60，不能混称同一性能条件。

本次纳入 930 的 B2T 源准备修复、cb 冷阶段拆分及 886 区间索引，保留原 512MiB 缓存预算，没有部署 `74d7e17` 的 64MiB 实验。新普通启动功能验收不是 CPU/FPS、持续散热或 6GB 结论。

旧 publisher 的资格门早于当前源卡格式，离线拒绝记录保留，未重标根卡或削弱门。工具 `9faac06` 将原 publisher／原 12 用例逐字节带入当前 53 工具树，并移植既有 attach 观察单行修正及真实 helper 测试；原当前 runner、installer、candidate gate、资产与构建／NSO 门均未改。原 12+15 主机用例通过，根复核 2,075 个归档／实时源码文件后，才使用新真实 886 功能资格及独立 Q 本地交付收条执行。没有新的运行时或 Shader 改动。

根实际 ADB 复核：APK SHA `22d62b10d2ef2bcfd33c77ed881c8ccf9abe4ad6baeda1926c083ad6016167c0`，profile SHA `4a8ec8326276716b3735d2ed5031255ab57c567c8b2c36d700ddef4579452066`，交付收条 SHA `ea04491c67505dad25e2359ae7248e209bf1b5fea4b79ec6ed08c834e80ac384`；模块 manifest、native table、Shader chain 均匹配，进程停止，锁及传输已清理。旧 a322 APK 与私有回滚备份保留。

**后续 Q 事务必须以新实际交付基线为原状态／恢复目标**：`.local/logs/lazy-physical-span-index-88620ec-desktop-Q735-v1/delivered-baseline.json`，SHA `28f3f9dd9763e4b7fb8cdd8caff24c94dc69b54d7a885a198dac755adcdebdb6`；a322 只作为历史回滚记录，不再代表当前安装。根验收为 `.local/logs/lazy-physical-span-index-88620ec-Q-local-root-actual-delivery-review.json`。源码分支 `codex/lazy-physical-span-index-v1` 与 publisher 分支 `codex/q-local-desktop-publisher-current-v2` 均已备份到 fork。

## 重复操作之外：先区分工作量、执行速率与关键路径

当前没有找到另一块已证明可删除、足以填补十毫秒差额的重复职责。后续不能从“经常调用”直接跳到“需要缓存”。修改前先给完整职责的现有包络、真正可消除子集、新增成本及净收益边界；包络不能当作预期收益。

886 普通桌面真实启动新增一次原 Simpleperf 指令采样／group-read，与原 Perfetto 同窗。197 个完整 Surface 内，CPU3–7 覆盖 NvN Running 的 99.9195%，实测用户指令为每 Surface 76.548–77.056M，用户周期 37.307–37.571M，覆盖部分 IPC 2.037–2.065。全 NvN Running 为 29.562ms/Surface，Runnable 0.286ms、Sleeping 9.409ms；Sleeping 尚不能归为纯 GPU。CPU0–2 另有 4.687ms 累计 Running 无计数样本，因此没有完整线程计数的有限上界。

官方 ReportLib 的 1955 条样本／counter payload 与设备原 `simpleperf dump` 逐条相符，时钟映射重叠、lost/truncated 为零；按同 ID、同 CPU 的边缘实际读数给界，不插值或按 event name 混算迁核计数。此窗是 0.75、PSO167，不与旧 0.60 数据混作候选 AB；采样开销无 control，不能称无干扰基线。根复核 39 个原始／工具／分析文件及设备原 APK、profile、收条、manifest、native table、Shader chain，进程已原存档两次并停止，当前仍为 886。

这说明在当前执行方式和工作／呈现关系下，仅消除睡眠不能把约 29.6ms 的 CPU Running 缩至 16.7ms。还须减少实际工作或降低必要工作的执行成本。现有完整父职责仍有用，新指令记录没有调用链，不能拿叶 IP 指令比例冒充职责 CPU 时间。

优先级继续为：

1. **同版本的执行速率与工作量。** 已有同父 25.738→35.520ms 的变化伴随频率及驻核变化，幅度远大于局部候选。尚无同窗指令证据证明两父工作量相同，不能称可追回 9.8ms 或按 MHz 归一；本次官方同窗能力已验证，可用于后续真正的同版本重复比较，不先改调度、绑核或温控。
2. **替换必要工作的表示。** 例如实际 Buffer GPU 地址／范围表替换逐槽 UBO/SSBO descriptor 组合，属于不同输入也能更便宜的结构方向。当前 886 的独立 driver descriptor bind 包络仅 1.198–2.066%，还含 image、constant 和必须的根绑定；buffer-only host 构造成本未分离。旧 7.55–8.99% 是历史混合包络，不是当前可省预算。地址表引入参数发布、GPU dependent load、边界处理和在途寿命成本；净下界仍为零，不能据此立即重写 Shader ABI。
3. **降低必要代码的执行成本。** 继续依据完整路径与官方 PMU 分析取指／数据布局；已做 PGO 和冷阶段拆分的无一致 CPU 收益结果保留，不重新包装成未做的大收益。
4. **小范围重复检查降级。** Pass request/history 外层仅约 1.03–1.28% 混合包络、附件视图／描述构造约 0.47–0.90%，均含必要工作且尚未扣新检查成本。资源重复声明数不能当跳过比例；不再把这些列为十毫秒主线。

本节只有测量及预算审查，没有新增运行时优化、FPS／持续散热／6GB 验收。证据：`.local/logs/nvn-instruction-window-886-offline-analysis-v1/{ready.json,report.md,root-review.json}`；结构合同：`.local/logs/buffer-device-address-886-readonly-v1/{ready.json,report.txt}`。

跨职责纯诊断审计亦未发现已证明的大预算：多数热统计已按 recording owner 累计并在 commit 发布；此次普通启动的可选 copy accounting 已关闭。`cpu_known_copies.fallbacks` 的 425,819,296 增量单位是逻辑拷贝字节，不是调用次数、CPU memcpy 或硬件 DRAM 流量。当前无父栈的指令叶不足以为整类诊断计价，不能把 generation、依赖、ready、寿命和预算状态一起删除。审计为 `.local/logs/886-instrumentation-wholepath-readonly-v1/{ready.json,report.txt,root-review.json}`，21 项原引用已复核；不据此新增运行时开关。

## 同一 886 的三次实际指令／周期重复

继续保持当前真实桌面 886、f830/full2838/950、0.75 与 2750×1260，不改任何运行时代码或参数。新增两次普通冷启动，同样原 Simpleperf 10s 指令事件组与原 Perfetto 8s 重叠，每次原存档两次、停止和工具清理均完成，实际控制器 72275/23028 退出 0。根复核 161 项原始／官方工具／分析文件，当前 APK、profile、收条、manifest、native table、Shader chain 未变，游戏已停止。

| 窗口 | 完整 Surface | 用户指令 M/Surface | 用户周期 M/Surface | NvN Running ms/Surface | 运行加权 MHz |
| --- | ---: | ---: | ---: | ---: | ---: |
| 首次 | 197 | 76.548–77.056 | 37.307–37.571 | 29.562 | 1421 |
| 第二次的可观测交集 | 188 | 80.372–80.957 | 40.091–40.433 | 29.383 | 1523 |
| 第三次 | 197 | 76.497–77.005 | 36.063–36.358 | 28.585 | 1399 |

首次计数覆盖 99.9195% NvN Running，缺小核的 4.687ms 累计运行，不能称完整线程上界。第二次原完整 195 区间保留：CPU4 首次样本在起点之后 242.55ms，因此不能用其仅覆盖 74.80% CPU 时间的 61M 计数冒充工作量减少。补充 188 区间由活跃核样本时间包络交集和原完整 Surface 规则机械决定，没有按性能挑窗；该补充窗及第三次的所有实际驻核均有计数边界。三 record 的 1955/2005/1934 样本分别与各自设备原 dump 逐条相符，lost/truncated 为零；没有盲目缩放或按 MHz 归一。

首／三指令规模接近，而周期、IPC、CPU 时间相差数个百分点，且运行加权频率反向变化。因此不能单靠 MHz 解释执行速率，也不能把相近 FPS 当作同样 CPU 工作。第二次新增 PSO/variants（167/30→174/32），首为167/30稳定、第三次为174/32稳定；相同营地位置不保证相同工作或指令混合。原 marker 约每 Surface 一次 replay、八次 Vulkan submit、一次 present；没有 Draw marker，不能补造相同 Draw 证明。采样本身的中断成本缺少 control/handler 测量，未宣称零开销或从 CPU 时间扣除。

本轮改变后续判断：局部 CPU 优化应同时核验同窗工作量和执行周期，不能仅因 CPU ms 或某叶样本变化就断言删工成功；现有重复没有给出可直接追回约10ms的证据。下一步的必要代码工作集研究与持续游玩工具适配均先做源码／机器码合同，不先改调度、频率或 GPU 语义。完整证据在 `.local/logs/nvn-instruction-repeat-offline-v1/{ready.json,report.md,root-review.json}`。没有新增 FPS、持续散热或真实 6GB 稳定游玩结论。

另对精确 886 的 draw/shader 两个 Android AArch64 TU 做有界 O3/O2/Os scratch 编译：六次均成功，O3 两对象与正式 canonical 对象完整逐字节相同。未链接 text 合计 153,120→141,204/123,992B；主要 draw+binding 入口 23,308→21,648/19,200B。实际解释包括首次常量初始化的 73 个展开 store 改为循环，以及五 stage 的五个静态绑定 callsite 改为一个循环 callsite；实际有效 stage 仍逐个消费，并有新增循环检查。栈没有一致减少，缺少动态 block 权重，不能据尺寸表估净 ms 或改日常默认。没有链接／APK／RAW 或游戏源码变化。原工具与机器码证据在 `.local/logs/886-O3-O2-Os-twoTU-v1/{ready-v2.json,report.txt,machine-explanations.txt,root-review.json}`。

持续验收工具的只读合同亦完成：旧工具独立锁旧 6f/6217/.60 fixed，candidate/full2838 门和后续 scope/stage 也不支持直接组合；不能只换 runtime 或绕 validator 执行当前 886。当前正常启动为 .75 起始且保留 adaptive/HUD 策略，不能暗改 fixed。正在独立工具分支最小适配，保留原 proof、CAS、输入、save2、四个官方 8s Perfetto 窗与失败清理；本节没有长测结果。原营地反向路线仍未证，首 miss 必须失败，营地不能冒充战斗／完整游玩或 6GB 结论。合同在 `.local/logs/sustained-886-source-contract-readonly-v1/result.json`。

## 当前持续游玩测试适配与真实启动

工具分支 `codex/current-sustained-886-v1` 已推送：82f1e6a 明确允许已实际交付的886/full2838/950/.75起始adaptive组合，原四个8s Perfetto、输入、生命周期、安装与六目录CAS保留；可选copy accounting显式OFF，各端点核lazy/raw/span/cache512策略。61项主机方法通过，根复核原关键8文件、15函数和phase finally未改变，并实际执行完整离线proof。它不生成性能收益或长测成功。

第一次实际控制器19434在原 `original_check` 因temp快照变化退出1，尚未创建owner、安装或启动游戏。独立停止态盘点确认，仅既有PSO.bin、recipes与游戏日志3个hash正常更新，其他7目录及不可变身份相同；未删掉或回滚暖缓存。后继b5dbe6d只添加显式根审核的同交付fresh停止态快照支持，固定原交付28f3不改，独立重算8目录/文件集合/严格PSO与GameLogs文件名/逐条old-newhash。38项受影响方法通过，根两次新完整proof通过；原original_check/expected_baseline/CAS未改。原失败与新fresh2009829、根审核52b1bce9分别保存，没有重标旧卡。

实际新事务46601使用新独立输入f887cad9，PID11664，已进入真实游戏并完成首批双向营地走动，00官方窗口完成。根查看原有截图：营地、角色和HUD可见，无明显新黑块；截图电池28.4°C、overlay25.5FPS只是该张截图读数，不代替官方测量或持续散热结论。原30min流程仍运行，后续检查点与最终save2/停止/恢复尚未完成，不能把当前阶段写为连续通过。实际数据为 `.local/logs/current-sustained-886-Q735-v2/`，输入/终态编排在 `.local/build/current-sustained-886-Q735-v2/`。

另做两个有界核实：停止态Q735的公开GameManager为standard、无该包intervention，不能排除OEM私有功耗/温控策略；最近6GB实测仍旧eeb、rawBC、shared线性Buffer而native image OFF，最后GPU fault/errno35不是已证OOM。当前886含后续B2T顺序修复但未在A650验证，不称修好或已失败。证据分别在 `.local/logs/Q735-current-public-game-mode-readonly-v1/result.json`、`.local/logs/a650-current-blocker-bounded-readonly-v3/report.md`。完整性能、持续游玩散热及真实6GB稳定目标仍未完成。

00分钟已完成的静止8s官方检查窗为198个完整Surface，平均39.340ms（25.42区间/秒），同窗NvN Running29.721ms/区间、加权1383.8MHz，PSO174/variants30稳定；trace health无error/data_loss。thread_state有38.677ms跨度未覆盖，未补入睡眠。此窗不是行走全程FPS或30min成功。数据与根复核在 `.local/logs/current-sustained-886-Q735-v2-analysis/00min/`；后续源控制器46601仍须按真实进度继续，不能因观察超时重启。

## 修改前的决策：不再把高调用次数当作主线

用户再次要求从更高层思考，并在修改前估算份额。当前结论：近期局部减重复不是足以填补约十毫秒差额的方案。已完成的 Store 表示收缩（约1.44万→720条/submit）与物理 Buffer 区间索引是实际机制例子，但不能重复认领，也未证明稳定整线程收益。仍可能减少的 Pass request/history 外层约1.03–1.28%、附件视图/描述构造约0.47–0.90%只是包含必要工作的样本包络，不能相加成为预期加速。

更高层的三个问题依次是：

1. **工作本身是否必要。** 按实际消费者准备代替整轮预枚举已经做过；准入职责下降时消费/覆盖成本上升，不能只报被删除的父节点。相邻绘制合批也已经检查，严格相同连续状态组为零，没有现成的大规模合法合批预算。命令typed化、planned_op及融合首次walk均已存在。
2. **必要工作是否必须采用当前表示。** 长寿命资源与逐draw参数分离，乃至真实GPU地址/范围表替代UBO/SSBO descriptor组合，是结构方向而非新命中缓存。现有dynamicUBO/schema/recipe/history先扣除。当前driver descriptor bind仅1.198–2.066%混合包络；buffer-only主机构造份额仍未分离。历史7.55–8.99%不能冒充当前预测。新增参数发布、GPU依赖读取、边界及寿命成本计入后，尚不能承诺整段21–24%绑定成本可删除。
3. **相同必要工作为何执行速率不同。** 同版本重复窗口已显示相近指令量伴随周期/IPC/驻核变化。它要求研究代码/数据访问与系统执行状态，不能再用一个叶子的占比下降证明全线程变快。当前O3/O2/Os静态尺寸差异和已测PGO均没有给出可兑现的动态净预算；不据此直接换编译默认或强行调度。

后续修改前必须写清：当前完整父职责的份额、候选实际覆盖的子集、已实现部分、新增CPU/GPU/RAM成本和验证预期。条件算术须明确假设，不写成预测收益；缺少子集成本时明确尚不能定量，不先做大型ABI改造。本节只更新研究优先级，无运行时代码或新的性能收益。正在运行的原30分钟城镇测试保留；其正常自适应已从0.75降至0.60，00/10分钟不能当作固定分辨率对照。

## 持续测试实际中断；Buffer 表示预算进一步缩小

当前886持续事务46601真实终态1。已完成331段原摇杆行走和00/10分钟官方窗，最后一段原walk的`tail -n1`读到正在追加的JSONL尾行，`json.loads`在char4093报未终止字符串。没有20/30分钟窗，不续接成连续30分钟。原finally确认当前PID11664存档2次、退出、原目录CAS恢复及cleanup[]；根另以原Device核实际APK/28模块/chain，并核profile/receipt/f830/native表哈希、游戏PID和controller lock均已不存在。失败及收尾依据为`.local/logs/current-sustained-886-Q735-v2-root-outcome/root-review.json`。原crash buffer包含大量历史/其他PID记录，不归给本次。

10分钟官方窗为241完整Surface，mean32.632ms，NvN Running24.791ms/区间、Runnable0.308ms，weighted1495.7MHz、CPU7驻留27.69%；截图电池31.4°C。比例已由原自适应从.75降至.60，PSO由174至224，因此不能用相对00的差值认领固定画质提速。10分钟原分析7份输出及trace均经根核；仅该检查点不代表全段行走FPS或持续散热通过。

本次两个官方meminfo端点PSS为2,010,443→2,147,118KiB，GL mtrack130,008→207,160KiB，EGL mtrack96,584KiB不变；同时私有图像allocation反而382,115,840→346,529,792B，逻辑共享并集363,855,872B不变，累计保留import描述范围578,813,952→593,494,016B。它们说明内存调查不能只盯私有图像缓存；PSO、驱动内部BO及不同统计口径需分开。尚不能由该变化确认是哪一对象或泄漏，也不归因给未开启的64MiB候选。下一步优先核实际f830已有的BO分类/RMV工具能力，避免自己再写追踪器。

原写端分多次fprintf，最后才写换行。已将原行走读取修为只消费最新换行终止的完整记录：`tail -n2`保留一条已完成记录，忽略尚未完成尾行；最新完整行若畸形仍立即失败，不回找更老的ready记录。实际主工作区commit`2d70fdd`含共享小解析器与原脚本回归，10项通过，覆盖中途失败释放触点、not-ready及原8秒停滞失败。这是观测工具修复，不改变游戏/运行时或证明长测成功。其他原采集入口的同类读取仍在独立后继接入，未重启长测。

当前6份原FP又经官方Simpleperf annotate和精确LLVM linked指令核对：绑定SELF约3.76–4.88%；能单独识别的直接UBO handle/range小块仅0.045–0.313%，Buffer tuple0–0.134%且含driver constants。共有hash/history为1.434–1.820%，跨image/buffer/constants，原PC没有运行时类型，不能全部分给BDA。可定位子集不是总Buffer成本上界，也不是可省下界；此次审查没有证明足以启动大Shader ABI重写的净预算。68项原record/ELF/source/官方输出经根核，见`.local/logs/886-buffer-representation-cost-v1/root-review.json`。研究优先级因此转向仍占真实工作量的资源消费及驱动/内存生命周期，而非预支整段绑定21–24%的收益。

## 原生驱动内存工具找到216MiB的描述符池预留

现用f830已编入Mesa的BOS与RMV，无须新增追踪器或驱动构建。核实stock Android属性与日志出口后，根实际启用BOS/startup/file logging，复用已交付886的原`ordinary_round`无extras冷启动和原`save_owned`。PID25878实际到原HUD、共享/native/provider验收及两次存档后停止。未安装、改profile、换资源或改游戏。该次工具扰动大，不测FPS。

原始日志17,887,154B、SHA`74b41719b1e36ebdf5fdccd0dc471e0639b4a2e3e2ab14e713390ba83962fc7f`，明确记录`TU_DEBUG=0x100001`。共14,203个完整命名BO表，逐块count/size与summary吻合，尾部完整。最终较大类别包括：imported host pages 652,408KiB、32MiB VkAllocation共12个/393,216KiB、descriptor pool16个/221,184KiB。总1,410,844KiB是BO逻辑extent；imports含重叠别名，不能当独占RAM或与PSS相加。所有表未见名为pvtmem/scratch的BO，不等于证明零spill。

**新候选是物理描述符池分块尺寸与留存预算分离。** exact886默认及上限为2,048 sets（不是先前口头估计的4,096），每池对多种类型总预留6,912B/set，f830实际BO为13.5MiB；dynamic UBO另预留CPU host区域。当前descriptor_create/recycle共用同一容量选项。如果只降低这个选项，会同时降低留存阈值并可能破坏跨提交复用。因此独立候选仅将物理块上限缩至512，需求增加时沿原OUT_OF_POOL/FRAGMENTED和链式扩展逻辑增长；原2,048留存阈值、batch、immutable sets及完成后回收保持。若仍16块，BO条件预算216→54MiB，少162MiB；实际块数、CPU和PSS收益未知，不承诺四倍净RAM下降。原统计5675累计sets不是per-context峰值，不能据此认定512足够。

BOS中池数也有13→8、16→1后再次增长，说明已有删除/重建，不能将216MiB解释成永久泄漏。候选先做真实池溢出、缓存留存和异步内容的原GPU测试，再进入Android构建与对照。仅BOS现有输出已改变内存研究优先级。数据和根复核为`.local/logs/current886-BOS-memory-analysis-v1/`。

该短采集初次wrapper终态1的原因是ADB shell将恢复setprop的末尾空参数丢掉；采集及原存档停止已成功。根随后用明确shell引用和CAS恢复三个原空属性，核游戏、锁和唯一日志已退出/移除；失败记录与成功清理后继分开保留在`.local/logs/current886-BOS-memory-Q735-v1/{result.json,cleanup-followup.json}`，没有重复采集或改标失败。RMV虽已编入，但stock导出硬编码/tmp且tokens持续增长，现不优先。

完整行修复的工具后继`b3137c3`已独立推送，58项相关检查通过。根复核原phase_game/main及持续run/validate等5个完整AST函数未变，原APK/NSO与运行时不变。实际主工作区的输入工具/解析器与后继逐字节相同。停止态八目录重新核实后，原1800秒持续测试以新事务53886启动，先前331段的失败不拼接；新输入SHA`b9261f784fe3162d68004470b1c76c93bbc504afbd8a38c95d2ade4c2c7238d1`，实际全offlineproof退出0。当前没有新的30分钟或战斗/6GB验收。
