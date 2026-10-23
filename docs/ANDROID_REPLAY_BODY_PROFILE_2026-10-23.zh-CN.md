# 回放主体约22.9ms：同窗官方采样的职责分解

本轮没有修改运行时或访问手机。只分析既有 Simpleperf / Perfetto，并用正式 ELF 的官方 LLVM 源码位置与反汇编核查调用归属。目标是识别大职责，不再从单个 memcpy、哈希或缓存函数出现频繁直接决定优化。

## 方法与预算口径

此前约22.9ms来自最终10d两轮、十个 Perfetto 窗的互斥 replay 主体 CPU：排除 submission planning、Store index finalization、queue submit 等子 marker。十窗中六窗与 Simpleperf 重叠，本轮仅使用这六窗及父版的对应六窗。

1. 官方 Trace Processor 从原始 marker 边界构造不重叠区间，选取最深 marker 为 `NVN command replay` 的区间；与 sched_slice 求交之和逐窗严格等于上次互斥主体 CPU。
2. 用官方 `to_monotonic()` 将每个区间转换到采样时钟，生成 Simpleperf 原生 filter file。
3. 官方 ReportLib `SetSampleFilter(--include-tid, --filter-file)` 只保留实际回放主体内的 `cpu-clock:u` 样本。官方 report_html 使用相同过滤文件独立生成调用图，样本数、eventCount 与分组输入完全一致。
4. 原始调用链保留；以完整父职责的直属分支作互斥分组，不累加父子 inclusive。内联函数使用此前官方 LLVM 的 exact ELF 位置信息；未保留的帧不补造。

父版六窗3120样本、1400个同窗Surface间隔；最终版六窗3074样本、1387个间隔。每窗约500个目标样本，因此细小分支的百分比不适合作为收益证据。

**以下毫秒是 cpu-clock:u 样本 period / 同窗 Surface 数的用户态成本估计，不能当作逐函数精确计时。** 当前六窗的调度主体 CPU 为23.0496ms，采样用户态估计为22.3868ms；两者相差约0.663ms，含采样误差、边界归属及用户态/调度统计口径差异，不能全部归为内核成本。此前十窗平均22.8993ms与本轮六窗平均的差异来自窗口集合，不是新的运行结果。

## 当前完整职责

| 互斥职责 | 回放主体用户态样本比例 | 用户态成本估计 ms/Surface |
| --- | ---: | ---: |
| 资源准备、绑定消费、访问/寿命记录 | 49.93% | 11.18 |
| 宿主 draw 状态、PSO/变体选择及特殊计划 | 11.58% | 2.59 |
| 图形驱动命令编码：draw、pipeline/set/vertex/index绑定等 | 10.51% | 2.35 |
| 命令回放、状态更新、逐操作计划查询 | 8.52% | 1.91 |
| 其余混合职责或调用信息不足 | 8.00% | 1.79 |
| Clear / compute / copy / query / event 执行路径 | 6.77% | 1.51 |
| 分块收尾与下一 recording 准备，不含已排除的 queue submit | 4.68% | 1.05 |
| 采样用户态合计 | 100% | **22.39** |

这约11.18ms是整个资源职责的成本，并非可删除预算。范围、布局、真实GPU访问和寿命有必要语义；哪些能合并、提前解析或省掉还要单独证明。

资源职责继续拆成互斥子项：

| draw 内职责 | 用户态成本估计 ms/Surface |
| --- | ---: |
| Shader绑定及真实资源消费 | **5.56** |
| CPU来源预检与来源解析 | 1.30 |
| 附件/Pass访问及构造 | 1.16 |
| 绘制后的图像访问历史 | 1.06 |
| 顶点/索引Buffer消费 | 0.98 |
| 图像准备 | 0.57 |
| 图像依赖/请求处理 | 0.39 |
| 作用域/引用边界及准入 | 0.16 |

最大的Shader绑定职责，进一步可见的主要分支是纹理视图/访问处理约1.9ms、Buffer地址/范围消费约1.1ms、绑定函数本身与内联处理约1.1ms。真正descriptor创建路径约0.52ms。该嵌套统计覆盖所有保留相同函数祖先的回放样本，含少量compute，不能再与上表相加，也不能把5.56ms都称为descriptor更新。

CPU来源预检中，来源解析、遍历/内联处理、纹理预检分别约0.47/0.41/0.27ms；`nvn_dirty_since`约0.12ms。主要表现为元数据和可见性检查，而非大块上传。

源码与栈对应：

- `nvn_vk_shader_pool_ranges.inc:vk_shader_cpu_sources` 遍历实际图像绑定与附件，解析来源后执行预检，已有source_plan交付保留。
- `nvn_vk_shader.c:shader_bindings_selected_impl` 在最终descriptor复用判断前，仍执行真实Buffer和图像消费；末端缓存命中不代表此前工作被跳过。
- `nvn_vk_texture.c:texture_view_binding_impl` 即使recipe命中，也保留实际消费、layout/access处理和request记录。这些是当前热点的重要来源，不能将其全归为重新创建VkImageView。
- `nvn_vk_image_dependencies.inc:image_record_normalized` 的访问历史、native记录与范围处理仍执行；已有covered-read早退不能重复算成新机会。
- `nvn_vk.c:record_pool_range` 已有last-range和single-word覆盖证明，热点不代表仍在整池复制。Buffer消费中的别名排除、物理span选择与阶段页记录也有实际成本。

## 修正一个调用归因问题

原始FP栈中部分libc叶子后直接接`vk_draw_record_impl`，中间调用帧没有保留。此前“直接copies/compare/allocator/TLS混合上下文”是保守桶，不能据此断言draw直接做了这些拷贝。

本轮以官方LLVM检查正式AArch64 ELF的实际调用点。最终版例如：

- ReportLib位置`0x30c5d2`所在指令为`0x30c5d0: blr x8`，官方源码位置对应`vkCmdBindPipeline`；其中53个memmove样本属于该调用站点下游，不是宿主draw直接memcpy，约0.39ms量级。
- `0x30c1ca`所在指令调用`draw_pipeline_prepare`；其memcmp/memmove等应归入PSO准备。
- `0x30ad22/0x30ac22`所在指令调用`vk_shader_bindings_selected`；对应libc叶子应归入绑定职责。
- `0x30a57a`所在指令调用`pl_plan`，应归入相应特殊计划。

只修正有正式调用点证据的职责归属，不向原始栈插入假帧；原始分组和修正分组都保留。父版使用其自己的ELF和PC独立核查。修正后最终版直接混合copy/compare桶从约1.03ms变为0.33ms，成本转归实际调用者，不是性能提升。部分调用地址不是4字节对齐，初次按该地址起始反汇编的unknown输出也保留；最终使用覆盖该位置的合法AArch64指令边界，未改工具符号信息。

## 为什么最近几路改动没有明显降低主体成本

同口径顺序结果如下。前三列为用户态采样估计，最后一列为调度直接计量：

| 运行 | 资源职责 ms | 其中Shader绑定 ms | 图形驱动编码 ms | 同窗回放主体在核 CPU ms |
| --- | ---: | ---: | ---: | ---: |
| 父版PARENT2 | 11.42 | 5.83 | 2.33 | 23.295 |
| 最终FINAL1 | 11.51 | 5.49 | 2.26 | 23.205 |
| 最终FINAL2 | 10.85 | 5.62 | 2.45 | 22.893 |
| 父版PARENT3 | 12.16 | 5.73 | 1.91 | 23.371 |

这不是完全同工作量、同核心/频率的随机化实验，细项采样波动也明显。不能把均值差0.28ms当稳定收益，或声称每项变化已经有确定因果。可靠结论是：四轮的大职责相同，资源链仍占约一半，没有消失的数毫秒级热点。

最近三路改动的实际适用预算很小：

1. **常量提前命中**：包含内联在内，全部`constant`路径父/子用户态估计约0.21/0.13ms，包含的并不全是可省c0/c2 key工作；key构造样本更少。外层纹理/Buffer消费和访问记录保持执行。不能从某个key省掉推导5.56ms绑定职责整体消失；这两个小数也不构成独立收益证明。
2. **Compute绑定复用**：整个guest dispatch路径只有约0.42ms量级，绑定只是其中一部分。内部query/event/retile等另有路径，不能把guest dispatch当所有内部compute的总预算，但当前样本没有显示compute绑定是主导成本。
3. **非native上传合批**：本批稳态营地样本没有出现`image_upload_contents`或其cold主体。零样本不等于绝对零开销；但host用例69 regions→2次copy的机制证据不能变成这段稳态回放的大预算。资源已加载后，运行时主要仍在消费和记账。

更早的按需窗口实验已证明过一次成本转移：准入约8～10%的样本降到很低，后续消费者却承担额外的覆盖证明/物理范围处理。该历史证据见`ANDROID_TRANSLATOR_WHOLE_PATH_PRIORITIES_2026-10-21.zh-CN.md`的533四轮部分；不能拿它量化当前10d的净成本。它解释了为何只看被删掉的入口会误判，必须观察整条资源路径。

没有证据证明“此前收益全部被降频抵消”，也不能据当前结果把总耗时不降归因于未知大锁。此次采样仍只覆盖营地，不代表流送、战斗、首次Shader编译的热点。

## 之后的研究焦点

第一优先级是完整纹理消费链：来源解析、物理视图/范围处理、访问请求和绘制后历史四个阶段，查明哪些输入/状态已经被前面证明、哪些真实依赖仍会变化。已有source_flow、recipe、covered-read及原native快速路径必须扣除，不再以重复增加缓存当解决方案。

第二是Buffer实际消费链：共享之后仍存在的逻辑地址、别名、span、阶段页记录和寿命处理。以整个消费者成本作前后比较，不能再只报某个查找函数的样本减少。

普通pipeline/set/draw驱动录制仍是约2.35ms级职责，可作为第三项；它与真正`vkQueueSubmit`的约1ms不同。不能把所有driver符号份额或全部draw父节点当可删预算。

这轮只把热点和事前预算找实，没有修改这些路径。约8%的用户态样本仍保留混合/不足归因，约0.66ms调度与用户采样差额不强行分配；它们不足以推翻资源职责占主导的结论，但不能宣称已经逐指令解释所有CPU时间。

## 可复核产物

目录`.local/logs/nvn-replay-body-audit-v1/`：

- 每窗`ranges.sql/ranges.csv/simpleperf.filter/samples.json`：实际互斥范围、官方时钟转换及官方过滤后的原调用链。
- `parents-official.html/current-official.html`：官方Simpleperf生成的过滤后调用图，3120/3074个样本及period与分析输入一致。
- `grouped.json`与`grouped-refined.json`：原始保守职责及有实际调用点证据的修正，逐窗完整保留。
- `callsite-review.json`及各LLVM输出：真实调用点证据，无重写栈。
- `conclusion.json`：互斥大类、完整四轮、最近候选覆盖范围及验证结果。
- `manifest.json`：本轮分析产物指纹；原始采样、trace和ELF的哈希在每窗输入记录中核对。

## 继续下钻：资源种类、命中后的成本与当前版本硬件证据

后续调查目录为 `.local/logs/nvn-resource-chain-drill-v1/`。运行时、APK、驱动和资源未修改；旧同窗采样继续用于职责预算，新增官方硬件事件用于补足“这些代码为何执行得慢”的证据。没有把两种不同窗口的比例相乘换算为收益。

### 11.18ms按完整资源职责重新分组

保持每个样本只归一个组，将绑定内部的真实消费者归回相应资源种类：

| 完整职责 | 当前用户态估计 ms/Surface | 父P2／当前F1／当前F2／父P3 |
| --- | ---: | --- |
| 纹理和渲染附件消费 | **6.69** | 6.81／6.51／6.88／7.31 |
| Buffer消费 | **2.57** | 2.61／2.94／2.20／2.57 |
| 绑定结果构造、描述符交付及未细分绑定工作 | 1.76 | 1.80／1.86／1.65／2.04 |
| 操作及引用边界 | 0.16 | 0.20／0.20／0.12／0.23 |

纹理6.69ms包括视图消费约1.85、来源预检1.30、附件/Pass约1.17、绘制后历史1.06、准备0.57、依赖批0.39及其余采样器等。Buffer2.57ms包括Shader中的地址消费1.06、顶点/索引消费0.98及span、texel、producer检查等。不是6.69ms外加此前5.56ms绑定；后者已被重新拆入这些互斥组。

继续查看完整消费者的直属子树：`pool_address`约1.85ms中，页/阶段记录约0.55、函数本体及内联约0.48、别名负查询约0.41、物理span选择约0.21、操作保活约0.15。它不是一个慢查找独占全部成本。`texture_view_binding_impl`约1.92ms中，本体/内联约0.62、布局消费约0.46、已有View查询约0.18、请求记录约0.12，其余分散。此处是全回放内同名函数的inclusive子树，含少量compute，不能再次加入上表。

### 已有工作量记录解释“为什么缓存命中了仍然慢”

读取此前六个当前版30秒采样前后的既有应用计数，不新增计数器：

| 指标 | 每次guest QueueSubmit的平均增量或命中率 |
| --- | ---: |
| 纹理View消费（命中+未命中） | 10,926次 |
| View recipe命中率 | 92.73% |
| Buffer地址消费（命中+慢路径） | 16,968次 |
| Buffer查找命中率 | 99.947% |
| 最终Descriptor内容命中率 | 92.32% |
| 布局使用计数 | 14,041次 |
| 实际布局transition计数 | 92次 |
| Pool镜像扫描/上传/回填字节增量 | 全部0 |

这些是约30秒的工作量统计，不是8秒采样窗的精确调用率，不能据此计算每次函数的纳秒成本。当前已有trace证明稳定区间一轮回放对应一次guest提交和一次Present，但这里仍以guest提交为分母。`texture_bindings.prepares`只计准备命中，`stream_submits.draws`只累计提前提交部分，不能冒充完整调用数或完整帧draw数。布局transition可能按mip计数，也不能把92/14041直接称为同一事件的miss率。

这些记录与源码一致：末端缓存省了创建和编码，前面的实际资源处理还在执行。共享内存已经消掉这里统计到的Pool复制；继续归因于旧镜像扫描不成立。零计数不等于所有图像上传或所有内存访问为零。

已核实不能重复算作新机会的内容：页/stage并集、image read proof、native stage ledger、source-flow、physical recipe和同值注册保护均已存在。此前空来源预检候选没有整体下降，且普通packaged来源仍需预检；不能再次声称整段1.30ms可免费删除。此前消费区间合并审查也已排除把整段View函数当作纯重复工作。

### 新采当前10d的硬件事件

只使用高通Q735，Mali与A650不操作。实际安装APK哈希仍为 `ec15c8c37edb7afe50783a2e44503f36eaf565fb86771f03ae1a6bf5f1af0db0`，宿主ELF Build ID为 `e5072519e4f0ea29f908832910cc7b9905769cff`，驱动为既有f830，shader chain为9507875007c6。使用原启动器进入原游戏营地，2750×1260、固定0.60、shared/native、external-profile及原60目标；这不是日常0.75/adaptive设置的性能结论。新位置为5225/6289，旧样本为5225/6295，没有修改游戏状态，也不声称严格相同工作量A/B。

原 `capture-official-cpu.py` 完成两轮系统Simpleperf stat/core/memory/front-cache/front-stalls及Perfetto。每个硬件组独立8秒，PSO/variant及目标在所有窗口边界稳定。原verbose数据的跨CPU事件覆盖约100%，保留系统对child-command权限的拒绝日志，实际已有线程计数成功，不能将权限日志隐藏。另用同一官方Simpleperf recipe采两次8秒 `raw-l1i-cache-refill:u`、period=1,000,000、DWARF32768；无自定义采样器，无补造调用帧。

| 当前版本指标 | 第一轮 | 第二轮 |
| --- | ---: | ---: |
| Perfetto整翻译线程Running/Surface | 26.496ms | 26.707ms |
| Perfetto互斥回放主体CPU/Surface | **22.986ms** | **23.129ms** |
| frontend-stall事件 / 用户周期 | **27.10%** | **27.03%** |
| L1I相关frontend-stall事件 / 用户周期 | **17.18%** | **17.10%** |
| 用户指令 / 用户周期（IPC） | 2.065 | 2.058 |
| 通用cache-misses / cache-references | 4.75% | 4.72% |

这再次复现了约23ms，不支持将旧采样全部视为失效。两个新trace各236个Surface间隔，完整cycle均为1 guest submit、1 Present、8 Vulkan submit，调度状态覆盖完整、无重叠和已报告的trace数据丢失。L1I事件是CPU取指问题，不是纹理带宽；通用cache事件不能证明具体缓存层级或DRAM带宽。frontend子事件不可任意相加，17%不是可兑现提速，尤其不能乘独立窗口的23ms当作可删除耗时。

两份raw-L1I数据中，翻译线程分别504和561个样本，**宿主DSO占80.56%／81.82%，Turnip占11.71%／13.37%**。实际PC及官方LLVM定位反复落在draw、绑定、pool_address、页范围记录、图像依赖/历史等代码；没有单一函数包办它。约14%的样本未保留x_thread祖先，仍按实际线程和叶模块报告，未补造父栈。官方report_html独立生成的1065目标样本及event period与ReportLib整理一致。

这使“热代码布局／取指效率”从旧版本线索升级为当前版本证据。仍不能将81%的refill份额乘17%的stall比例：refill次数不是各次miss延迟，属于不同事件和窗口。

### 调度因素也要单独验证

同两份Perfetto的翻译线程分别有218／222次可见换核；235个完整回放中，85／88个跨多个CPU执行，约36.2%／37.4%。相邻回放首个CPU不同分别84/234、81/234。运行主要位于CPU3～6，另有少量CPU7。频率随时间及核心变化，不能只用芯片最高频率解释23ms。

这是需要控制的因素，不是“换核导致17%停顿”的因果证明。完整回放CPU包含已有规划/提交子段，与互斥22.9ms口径不同。跨核回放与单核回放的工作量、频率、抢占也可能不同，不能直接相减得到迁移代价。

### 修订后的优先级

1. **对必要热路径做代码局部性对照。** 当前硬件事件及实际PC已支持一次有界实验。首选保持算法、O3、ABI和资源语义不变，只用原LLD的函数section排序验证，排序输入必须纳入canonical来源和哈希；宿主排序不能改变另一个DSO中的Turnip。先前完整冷阶段外提和ThinLTO已试过，不能重复包装成新优化。排序未必减少miss，padding、跳转距离等也可能退化，必须测净结果。
2. **单独控制翻译线程迁移做诊断。** 临时亲和性只作为对照，记录实际频率、运行/就绪时间和硬件事件；不同时改资源代码，不直接默认绑核。这样才能区分布局收益、CPU调度和频率变化。
3. **资源算法改造集中于纹理/附件6.69ms的整条链，其次Buffer2.57ms。** 要改变重复准备和元数据交付的粒度，而非继续给末端增加缓存。任何跨draw复用必须覆盖实际绑定、CPU发布、Buffer别名写、image/layout代数、嵌套回放及submission引用；现有命中率不证明所有这些条件不变。绑定结果表示的1.76ms也不能再冒充整段5.56ms可省预算。

本轮未实施性能候选，没有新增已证FPS或CPU提速。两个采集会话均已通过原Android生命周期确认新save commits=2，再停止自有游戏进程；Simpleperf临时文件/进程已清理、perf_harden恢复，第一会话保存的偏好哈希前后一致。原工具数据、实际命令、原始CSV/trace/record、职责分组及完整边界见调查目录的 `conclusion.json`、`manifest.json`。

## 并行实施后的第一个实验：函数排序没有带来改善

布局实验与资源整链候选的实现独立并行。布局分支 `675e972` 只为原构建入口增加带来源指纹的 LLD symbol-order 输入；不改变算法、O3、游戏资源、Shader 或驱动。73 个热函数大小不变，地址跨度从709,828B收拢到89,900B；这不是少执行了指令。285条编译命令规范化目录后相同，535个ABI入口和524个隐藏入口核验通过。四个辅助hook的文件哈希因调试信息/BuildID变化，但官方LLVM逐section和完整反汇编证明加载内容（除BuildID）相同，不能错误地说只有host文件的哈希变了。

三个原始NSO再次fresh重建并独立verify。真机沿用已验证的原模块：23个执行/规则文件及build_src与fresh产物逐项一致，保留原、新manifest的关系，没有修改加工后的binary。只替换APK进行A/B/B/A，最后恢复原10d日常APK。仍为Q735、2750×1260、固定0.60、target60、f830/full2838/shared/native；不是日常自适应比例或持续温度实验。

| 轮次 | 回放主体CPU ms/Surface | 全翻译线程CPU ms/Surface | L1I相关停顿/用户周期 | 调度窗加权MHz | Surface FPS |
| --- | ---: | ---: | ---: | ---: | ---: |
| 原版A1 | 23.207 | 26.795 | 16.74% | 1524.8 | 30.18 |
| 排序B1 | 23.722 | 27.414 | 17.68% | 1525.6 | 30.18 |
| 排序B2 | 23.585 | 27.240 | 18.37% | 1556.4 | 30.27 |
| 原版A2 | 23.128 | 26.731 | 17.30% | 1553.8 | 30.27 |

候选主体均值比原版高约0.49ms，取指停顿没有降低；没有依据默认启用这次排序。保留独立实验源码和证据，不将其空间聚集机制当成已经减少CPU工作的优化。本结果也不能否定所有取指优化，只否定这次具体布局作为当前提速方案。A1最早stat窗口有variants30→32，B1 core窗口也有同样变化；保留原始记录，B1 IPC不用于比较。表中cache/stall/scheduler各自窗口稳定，事件覆盖约100%，完整cycle保持一次guest提交/Present及八次Vulkan提交。PMU和Perfetto是独立窗口，不把比例乘成可删毫秒，也不按MHz线性修正结果。

另一次不改包的临时taskset诊断在读取目标线程affinity时被Android权限拒绝，没有改变mask，也没有产生有效A/B数据；原存档后停止，没有转去改变系统权限。A2采集前的标准`/proc`快照确认翻译线程属于CPU/cpuset `top-app`、nice0、普通调度policy0、prio120、uclamp0–1024，排除了误设后台组/优先级的具体假设；不能据此证明OEM频率策略最优。

证据：`.local/logs/nvn-layout-device-v1/{analysis.json,conclusion.json,manifest.json}`、`.local/logs/icache-layout-28a-v1/`、`.local/logs/nvn-affinity-diagnostic-v1/`。四轮均原生命周期新存档commits=2后停止；Mali与A650未操作。并行的资源候选是同pass内已完成native只读消费的跨阶段交付，尚在独立正确性/构建验证中，不能把6.69ms纹理职责全算作其可省预算。

## 并行资源候选完成：可达不等于消掉整条热路径

独立分支 `codex/native-consumption-proof-v1` 最终候选为 `58f5744`，默认关闭，未合并主线。它复用同一实际render pass、同一录制上下文中，前一成功记录的draw已发布的native只读消费证明。这里的“完成”仅指host命令记录与依赖状态发布，不指GPU执行完成。CPU来源预检、prepare、资源保活、真实写依赖以及query语义保留；最终检查在不可撤销query槽位分配前进行，失败整批回退一次。

首版 `cc29f32` 的host验证通过，但真机场景没有实际成功命中。原因之一是要求所有NVN sticky barrier位为零，过于严格。修订版按原有covered-read语义允许已证明的只读重用，仍要求指定同步模式；没有清空NVN barrier，也没有放宽真实写依赖。两个ON会话均在实际成功记录draw后触发首次命中标记，证明新路径可达。单次标记不证明稳态覆盖率；采样PC位于条件判断上也不能证明分支taken/not-taken。

同一个修订APK依次ON/OFF/OFF/ON，官方Simpleperf与Perfetto保持既有独立窗口方法，Q735、驱动、资源、固定0.60等条件不变：

| 轮次 | 开关 | 回放主体CPU ms/Surface | 全翻译线程CPU ms/Surface | Surface FPS | CPU对照资格 |
| --- | --- | ---: | ---: | ---: | --- |
| B1 | ON | 24.062 | 27.581 | 30.09 | 合格 |
| A1 | OFF | 23.725 | 27.185 | 30.18 | 合格 |
| A2 | OFF | 22.520 | 25.943 | 30.22 | 排除：调度窗variant 30→32 |
| B2 | ON | 23.418 | 26.955 | 30.22 | 合格 |

保留A2原始数据和其他独立稳定窗口，不用其CPU结果计算稳态平均收益。两次ON分布在合格OFF结果两侧，目前不能认定净改善，也不能认定确定退化。B1/B2营地截图未发现明显黑块或严重几何错误，仅限该场景检查，不等于全部游戏正确性验收。候选和对照均执行原ARM64游戏模块；fresh原始NSO重新加工与23个执行/规则产物及build_src核验一致，没有改加工后的游戏字节。

进一步源码审查解释了为什么这条方案未显现大收益：它把一条40字节image-request记录换成另一条40字节只读记录，仍需逐资源资格判断、追加、draw前最终验证、draw后历史发布；原路径本身已有exact-read证明早返回。新列表不是整段消失。B1约1.44%的全翻译线程用户态采样落在新检查相关代码，这不是互斥回放时间，也不是净增1.44%的定论。

原6.69ms纹理职责包含1.17ms附件/pass，此候选不覆盖；已明确定位到native专属祖先链的0.284ms只是识别下界，不是全部native成本或可省时间。约5.13ms仍位于无法判定资源身份的公共代码，其中来源与prepare约1.84ms未被此候选删除。此前1.58ms也只是部分调用者包络，不能全部记成收益预算。

后续有意义的结构方向是让一次operation已完成的来源/物理解析结果直接交付后续绑定、依赖与历史消费者，避免再次解析同一资源；而不是新增平行列表再分别检查。需先证明适用资源覆盖、失效合同及净删除的工作量，现有数据还不能承诺省3ms或更多。本轮保留独立实验源码，没有把“FPS未涨”当作撤回已证明减负的理由：目前连稳态净工作减少都尚未证明，新增检查却客观存在。

四轮均经原Android生命周期确认新存档commits=2后停止。随后恢复已验证日常APK `ec15c8c…f0db0`，确认游戏停止；Mali与A650未操作。证据在 `.local/logs/native-completed-sticky-device-v2/{analysis.json,conclusion.json,manifest.json}`、`.local/logs/native-completed-sticky-guard-audit-v1/`；源码和构建来源记录在 `.local/logs/native-completed-sticky-v2/`。这两项并行实验均没有新增已证明的生产性能收益。

## 实验之后的架构复核：收紧下一轮预算

技能包交付后，再次并行审查三种更高层边界，避免继续把必要资源处理称为可删除缓存开销。结论仍是：**尚未找到已证明可稳定减少数毫秒的方案**。以下是源码和已有官方采样的离线复核，不是新真机性能结果。

1. **整stage采样资源计划**：若只加一个总失效代号，但Pool仍按draw分别借用，绑定还混合变化的UBO/SSBO，实际逐资源循环就没有消失。真正的改造要同时改变成组资源持有、绑定表示及变化时的失效传播；其稳定覆盖尚未证明。更窄的整stage CPU空预检在 `bf351f8/e2d24a7` 已试过，历史采样没有降低；当时剩余预检大部分可定位到普通packaged来源的backing/dirty/CPU-effect处理，不能把它全按native marker无操作处理。不得重复包装这个旧实验。详见 `ANDROID_TRANSLATOR_WHOLE_PATH_PRIORITIES_2026-10-21.zh-CN.md` 的e2d部分。
2. **pass级附件计划**：当前 `prepared_pipeline` 命中之前仍构建context，`target_match`只在一次draw内复用，确有剩余工作。但六窗附件getter父成本约0.415ms、prepare约0.058ms、access约0.124ms；它们属于既有1.17ms附件包络。context构建及reuse检查源码行包络约0.182ms、仅25个样本，不能当精确计时或全部可省。普通active非local pass的固定元数据复用可行，真实layout/access仍必须执行；列为次级亚毫秒候选，不作为3ms突破启动大改。既有约83%的pipeline-prepare复用率来自较长工作量窗口，不能与这批8秒采样直接推每次成本。
3. **跨提交保留图形命令**：原记录已经类型化；同handle不代表命令字节和CALL/JUMP子图不变，当前没有权威命令版本。录制的Vulkan secondary还有当前descriptor、UBO位置、query和生命周期绑定。五类驱动录制互斥包络合计2.352ms；它既不是23ms，也不是已证明全部可复用。此前f75已审查过相同前提缺口，当前尚未补齐，不立即实现handle到command-buffer缓存。

这次复核的优先级结果：若继续追求数毫秒级变化，应先证明成组资源消费的实际适用覆盖和可以整体取消的循环，再进行表示与生命周期的联合改造。附件元数据小缓存、更多getter合并、另一轮空预检都不能替代这一步。命令缓存需要先补版本与重复覆盖证据，不能用一次静态截图推导跨帧重用。

本轮未新增运行时代码或设备实验。报告位于 `.local/logs/stage-plan-architecture-v2/`、`.local/logs/nvn-attachment-plan-architecture-v1/`、`.local/logs/nvn-command-reuse-architecture-v1/`；后两项的预算来自同一批已归档官方样本，不能与原职责重复相加。

## 按NVN责任审查，而非默认所有消费检查都不可省

用户要求从NVN的上层同步合同重新审查。已完成只读接口审计，证据在 `.local/logs/nvn-sync-responsibility-audit-v1/review.json`。本地NVN头提供枚举与原型，不是完整官方同步规范；旧Metal注释和已测游戏路径不能替代完整规范。

需分别归因：游戏显式Barrier/Fence/Flush要求、API已有隐含保证、Vulkan物理表示引入的copy/layout/alias依赖、当前保守epoch与缓存策略。按draw重新解析metadata和维持平行消费列表不是由NVN接口直接强制的算法。后续将已有同窗成本按这些责任映射，优先判断能否在实际注册/写入/物理替换/发布入口建立有效性，而不是继续逐消费者证明。

实际发现 `CommandBufferWaitSync` 与 `WaitEvent` 仍进入空桩，未找到直接验证其Vulkan通用等价性的用例。CPU SyncWait、acquire和已测资源hazard案例不能替代GPU等待合同；同Vulkan队列的提交顺序也不单独建立执行/内存依赖。这个缺口不能当作可删同步的性能预算，也不应据此盲加全阶段Barrier。本轮没有修改等待语义或声称它解释了23ms。

另一个直接GPU后端分支不在本轮修改范围。架构讨论需区分游戏可见NVN结构与硬件描述符/图像字节布局：直接后端可以减少API转换、对象和绑定管理，但跨厂商的texture tiling、固定功能attachment/压缩和硬件cache依赖不会因此消失。Shader按原字节地址读取可以保留部分Buffer/纹理表示；一般过滤采样、压缩解码、混合/深度/MSAA则需单独设计，不能把一次地址变换视为全功能兼容证明。
