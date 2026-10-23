# 从整条翻译路径检查准备工作的复用

以下主体结论来自已有官方工具数据和代码的离线分析，没有修改运行时或增加计时器；为补齐 NVN setter 频度而启动的后续只读检查单列于末尾。源码为 `0f9eb3b`。目标是减少完整准备流程的重复执行，而不是继续给叶子函数加缓存。

结论：高频重复仍然存在，但“次数很多”与“可删除成本很大”必须分别证明。前一图像子集实验已删除约57%的 View 消费访问，Buffer 消费、CPU 来源遍历和最终 descriptor 内容处理仍在执行。下一步应研究持久逻辑资源计划与动态访问记录的分离，不能把扩大图像组当作已经找到数毫秒收益。

## 先明确整条路径

当前提交规划先展开 NVN 指令，计算附件、Store 和 Query 等需求；回放再更新绑定状态并执行 draw。普通 draw 的主要资源流程是：

1. 取得 shader schema，检查 CPU 来源，建立本次 operation。
2. 执行可能产生提交或重入的来源发布、顶点依赖和附件准备。
3. 准备各 stage 的图像，逐 stage 混合消费 Buffer 与图像，生成 descriptor 值。
4. 处理附件及资源依赖，确定实际 Render Pass 和 PSO。
5. 解析 VBO/IBO，编码 Vulkan 绑定与 draw。
6. 发布图像访问历史、Buffer 写入等真实消费结果，结束 operation。

这里混合了三种不同生命周期：逻辑绑定身份、物理资源及其所有权、当前访问的同步状态。Shader 或 Buffer 的动态参数变化，不一定意味着图像身份变化；同一资源身份不变，也不意味着真实 writer、CPU 发布或所有权没有变化。

源码已存在的复用不能重复算成新机会：shader consumer schema、View recipe、descriptor history、PSO epoch、native recording/stage ledger、Buffer last-range/single-word 页覆盖、Buffer common-visible 和 operation source-flow 均已实现。

另一个重要修正：当前共享懒导入配置中，`pool_operation_begin(..., lazy_draw=1)` 直接建立空 operation scope，不调用完整 `draw_pool_collect`。因此，源码里有这段收集函数，并不代表当前每个 draw 都执行它。新方案不得把消除它记为收益。

## 当前真实访问次数

下表读取前一实验同一 scheduler 外包络的已有应用计数，按该外包络的 **guest QueueSubmit 数**归一。A 为同 APK 功能关闭，B 为开启；不是精确工具裁剪窗、不是每个函数的计时，也不是跨版本历史捕获计数。

| 每次 guest QueueSubmit 的平均事件数 | A1 关闭 | B1 开启 | A2 关闭 | B2 开启 |
| --- | ---: | ---: | ---: | ---: |
| View 消费缓存命中＋未命中 | 11,337 | 4,862 | 11,337 | 4,847 |
| Buffer Pool 成功地址消费，hit＋slow | 17,301 | 17,281 | 17,301 | 17,301 |
| descriptor 内容命中＋未命中 | 4,790 | 4,786 | 4,790 | 4,790 |
| Store 规划 descriptor bucket 查询 | 12,015 | 11,995 | 12,015 | 12,015 |
| 实际 pipeline prepare 事件 | 2,390 | 2,388 | 2,390 | 2,390 |

View 消费下降约57%，两个对照方向一致。这个结果说明前一实现确实删除了大量访问；不是所有命中都只多做一次检查。但是仍未证明稳定的净 CPU 毫秒或 FPS 收益，不能用次数比例直接换算时间。Pool 慢路径只有约0.052%；descriptor 分配在整个外包络仅数十次调用，继续追这两个尾部没有高收益证据。

计数口径审计也修正了此前“24.6% draw 命中率”的称法：`stream_submits.draws` 在分块提交时累积 `V.draws`，包含 dispatch 等操作且可能遗漏流尾，不能作为精确 graphics draw 分母。`pipeline_prepare` 也只是 prepare 事件，可能重入，不用它替代 draw。命中数、跳过项数及原始比率保留，但不再声称逐 draw 覆盖。

## 重复的是资源，还是仅仅同一个大 Buffer

用既有官方 GFXReconstruct 单帧输出重新整理资源身份。该帧属于历史 `533820e`、frame1049，共2312个 draw；不能视作当前候选的合法命中率。分段键包含实际 Pass/subpass、command buffer 和原分析中保守的硬命令 epoch；shader 变化不自动改变资源身份。

Buffer 动态偏移按完整 set layout 的 binding/array 顺序解码，再加到 descriptor offset。共11741项动态绑定，11628项偏移非零，不能只比较 VkBuffer 句柄。

| 历史帧观测对象 | 槽访问总数 | 各段首次出现身份数之和 | 段内重复身份访问 | 其中同一 draw 内重复 |
| --- | ---: | ---: | ---: | ---: |
| 采样图像 View＋layout＋stage | 10,701 | 2,677 | 8,024 | 22 |
| Buffer 句柄＋有效偏移＋范围＋类型＋stage | 15,731 | 3,995 | 11,736 | 0 |
| Buffer 句柄＋stage，仅物理 backing 身份 | 15,731 | 1,265 | 14,466 | 5,135 |

因此图像有8002次重复来自后续 draw，Buffer 的11736次也全部来自后续 draw。它们不是仅在一个 draw 内将重复槽去重就能消掉的量。第三行还表明：即使 Buffer 范围改变，物理 backing 身份仍经常不变；但这不能授权省略新范围的依赖和页记录。

进一步按该历史提交对应源码及官方 `VkBufferCreateInfo`、内存绑定、host-pointer import 记录区分来源：

| Buffer 来源证据 | 全帧槽访问 | 段内重复有效范围 | 主 Pass 槽访问／重复 |
| --- | ---: | ---: | ---: |
| 宿主 Uniform-only 常量家族 | 3,662 | 2,942 | 2,344／1,706 |
| Pool usage 形状，且实证绑定导入宿主内存 | 12,069 | 8,794 | 6,735／4,680 |

后者涉及10个 VkBuffer，全部同时具有 HOST_ALLOCATION 声明及 `VkBindBufferMemory` 到 `VkImportMemoryHostPointerInfoEXT` 的证据；这些 descriptor 槽没有 temporary/unknown 分类。它证明共享导入 Buffer 仍有很大重复规模，也排除了把宿主常量的2942次重复错算给 Pool 路径。仍不能从 Vulkan 句柄推断当前 guest Pool 身份或实际 `pool_address` 调用次数；texel BufferView、VBO/IBO未包含在这张表里。

独立复核使用已有官方 SPIRV-Cross/spirv-dis 数据：72个 graphics module 未发现 storage writer，全部10701图像项都出现在对应反射接口。排除同 VkImage 的 framebuffer 附件后，仍有10683项访问、8017次段内重复。主 Pass 为607 draw、9109项采样访问、6652次段内重复，没有同 VkImage 附件重叠。

这些数只回答“重复规模是否值得研究”。CPU 发布、不同 VkImage 的共享内存别名、native 所有权以及当前 shader 实际路径仍需自己的证明。不能将8017或11736称作可以删除的函数调用数，更不能相加后当作本版本已节省操作。

## 结构上的两个主要候选

### 1. 逻辑资源计划持久化，CPU 来源只遍历需要观察的子集

当前 `image_source_plan` 在 operation 开始/结束失效，CPU preflight 每 draw 重新建立槽序列。native 来源最终立即返回，但外层仍先遍历、解析身份、调用。source-flow 已缓存逻辑值，不是每次从零解码；共享 lazy draw 又已经不运行它原来服务的完整 admission 收集。

应把“shader 使用哪些槽、槽当前对应哪个逻辑资源、哪些需要 CPU 来源观察”变成随实际状态变化维护的计划。TEX/IMG、program/schema、注册代数、完整 state restore 等变化时更新；纯 Buffer offset 变化不重建图像部分。未变化时直接沿用计划，只执行确实需要 CPU 观察的条目。

这层只保存逻辑元数据，不缓存像素内容，不借此跳过 GPU alias 或 Pool 所有权。native marker 可以证明不需要 CPU 解排布观察，不能证明内存从不改变。存在 CPU 效果排队、未知来源或回调重入时，仍走原正确路径。

扩展到整段 native 图像消费复用，需要另一个完整合同：实际首次消费后发布访问状态；计划持有 distinct Pool 和物理资源；writer、layout、CPU 发布、外部 owner、关闭/替换及 recording 边界能失效整个相关计划。仅在现有组里取消 `native_entry` 排除，会漏掉所有权问题，不能这样实施。

### 2. Buffer 的身份解析与范围访问分开，按变化更新消费结果

`pool_address` 当前成功命中后仍可能依次执行 mip/图像别名处理、Pool/span 解析、初始化判断、当前 operation 保活和页/stage 记录。它不是一次哈希查找。历史帧中大量有效范围完全相同，当前成功消费仍约1.73万次/guest提交，因此值得研究这整段。

目标是让绑定状态持有已经解析的逻辑范围与物理 backing 关系；范围或物理身份改变时更新这部分。实际访问的可见性、writer 和生命周期单独管理，并在当前 recording 中首次真实消费时登记。只改变偏移时不重新推导没变的 Pool/backing；范围也没变、且访问状态有效时，不再重复整条消费链。

新设计必须从 setter/registry/writer 等变化入口维护状态，不是在每 draw 再建立一张“需要检查的 Buffer 列表”。VBO 地址绑定长度为0、跨 span、可写/未对齐临时副本、内部提交、ghost 重入与资源关闭不能套用普通只读范围合同。已有页覆盖和 native stage ledger 的节省必须扣除，只考察它们外层仍在执行的链条。

## 成本预算与实施顺序

已有同窗官方采样对旧稳定版回放主体给出互斥职责：纹理和附件约6.69ms、Buffer约2.57ms、其余绑定结果构造约1.76ms、操作边界约0.16ms。图像来源预检约1.30ms包含在6.69ms内，不能再相加。整个资源职责约11.18ms；约23ms主体的其他部分包含draw状态、驱动编码和回放等。

这些是职责成本上界，不是可删除预算，而且前一图像组已经改变当前图像链的成本。尤其不能再把旧完整图像预算全部交给下一候选。即使假设消除半数旧资源职责，也仍不足以单独把约26.6ms全翻译降到16.67ms；这个算术只是目标差距，不是收益预测。

本轮优先级：

1. 补齐当前高频消费者的**变化频度和身份分类**，以已有计数/官方捕获为基础，确认 Buffer 有效范围、native/普通图像、来源观察子集分别覆盖多少。Vulkan API 捕获不提供原 NVN Pool 身份，采样次数也不是调用次数；缺口必须明示。
2. 下一实现候选优先选 **Buffer 完整消费计划**，画清“变化入口—计划更新—首次访问—后续消费”的职责。先证明会取消哪些完整循环，扣除新增维护、失效和重建成本，再决定实验范围；尤其先从历史 Buffer 槽计数中分出宿主常量，不能将它们全部归给 `pool_address`。
3. 持久图像来源计划及 native 整组所有权保留为后续结构机会，排在合同和剩余预算明确之后；不同时发起多套缓存实现，也不因身份重复次数大就默认扩大。
4. 分配慢路径、PSO miss、compute 绑定尾项暂不作为主线。也不重新写 descriptor cache 或增加更多页检查。

本轮没有新的运行时收益宣称；前一实验仍是工作量减少成立、稳定净 CPU 收益未建立。主线默认源码已包含普通图像子集实现，桌面交付因 Java profile 标签验收缺失已完整回滚，Q735日常包保持旧稳定版；交付回滚结束时已存档并停止。该交付问题已独立收尾，不继续占用结构优化时间。

后续已开始补一个影响设计的实际缺口：Vulkan 层看到有效范围相同，不代表 NVN 没有重复发送同值 setter。若按“收到 setter 就让整组失效”，可能使覆盖率归零。已有离线文件不含完整 NVN 命令流与入口状态，交由独立代理复用已成功的 LLDB 只读停点抓取一个有界提交，分析同值/变化的真实绑定命令；不新增计时器或运行时采样，也不重新采性能基线。得到结果前不以历史身份重复率承诺下一候选命中率。

## 可复核证据

- `.local/logs/translation-structure-reuse-v1/count_structure.py`、`structure-counts.json`：只整理已有官方捕获输出，输入SHA保留。
- 同目录 `independent-review/review.json`：独立offset、反射及附件重叠复核。
- 同目录 `independent-review/buffer-origin-counts.json`：剥离宿主常量后的共享导入 Buffer 身份频度。
- 同目录 `counter-deltas.json`、`parent-budget.json`、`report.md`：当前同包A/B工作量与原互斥父职责预算。
- 同目录 `source-review.md`：源码生命周期/已有优化审查。
- `.local/logs/image-buffer-split-device-v1/final-independent-review.md`：原六轮官方性能结果及计数分母勘误。
