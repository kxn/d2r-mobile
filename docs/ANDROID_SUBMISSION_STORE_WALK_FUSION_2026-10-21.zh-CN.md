# 提交展开时同步收集附件索引事件

本候选基于 `886b8ef`，只合并同一次 submission 中的两个前向遍历。它不缓存跨 submission 的 command handle，不修改 shader、玩法、GPU 同步或资源生命周期。尚未进行 Android、原游戏或硬件性能验收。

## 当前真机证据及可削减范围

使用已完成的 Q735 / `8480a53` 官方 Simpleperf ReportLib + LLVM 栈：605 个 NvN 用户态样本，`mtl_sub_index` 可见子树 36 个（5.9504%）。其中符号自身（包括编译器内联的下层函数）14 个、`walk` 自身 12 个、descriptor footprint 子树 5 个；这些归属包含重叠，不能相加。

旧实现先按真实 CALL/JUMP 顺序把命令展开到 `plan`，再逆扫深度/模板/查询需求，随后又按相同顺序读取每条 `plan.op`、`plan.opcode`，重建 render-target、program 和 texture/image-slot 状态来产生附件事件。本候选让第一次 `walk` 直接将当前 opcode/payload/序号交给同一个前向状态上下文，省掉完整的第二次 `plan` 遍历、对应指针/操作码重载和循环控制。

状态分派、shader 查询、descriptor footprint、真实事件生成仍然存在；逆向 DS/query 分析、稳定 scatter、按地址排序和别名闭包也全部保留。例如旧 `store_index_build` 的 scatter 样本并不是本候选可消除的工作。没有把 5.95% 写成可节省比例，也没有预测毫秒或 FPS。

## 为什么不跨提交复用 handle summary

NVN command handle 是可复用的游戏命令内存地址。录制器可以在合法生命周期后重录同一地址；CALL/JUMP 又可指向其它可复用块。目前没有覆盖这些写入的 command-generation 注册表。初始 DS/stencil、RT/scissor/program/绑定槽也随 submission 改变，descriptor 注册另有 epoch。仅凭 handle 地址或 shader serial 缓存 summary 会产生过期证明。

已有计划数组、当前 build 的 descriptor-ID 桶、附件桶和不可变 shader image-slot 分类仍照用。本候选不加第二套缓存或 epoch。

## 数据流与保守退路

`submission_index_build` 在原有许可条件下初始化 `store_build_context`；所有顶层 handle 与递归 CALL 共用该 submission-local 状态。JUMP 的展开顺序、重复调用的状态继承、1,000,000 节点和 32 层深度限制不变。只有原 walk 对该节点完成必要验证后，才收集它的状态/事件；原有的 END、CALL、JUMP 节点仍在 plan 中。

附件解释器独立检查自己会读取的 payload 长度，包括短 render-target、scissor、program、clear/copy、绑定头和实际句柄数组。该检查是融合所需的新成本：旧版只在完整 walk 成功后才读取这些 payload，融合版会在发现后续坏 root 前处理已合法的前缀。短 store-only payload、store 事件分配失败只取消附件索引，原结构/DS/query 计划仍能完成；结构性失败则取消整份计划与索引。失败前可能已进行前缀的元数据查询/分配，分配时机不再与旧版相同，不能声称非法命令具有完全相同的分配副作用。

descriptor epoch 在收集开始时抓取，最终仍重新核对；构建期间注册改变时保守弃用索引，不把逻辑缓存当 ready 证明。查询时原有 epoch 校验、独立参考扫描和 check 模式保持不变。

`store_build_context` 是 8,096 字节的局部状态（静态上限 8 KiB），原先这些 RT/slot 状态也存在于旧 builder 的栈上。现在只有 submission 一份，递归只传指针，不按节点或递归层复制整个上下文；没有新增持久资源缓存/每命令元数据。调用位置和编译器内联可能改变栈峰值及代码布局，尚未声称最终 Android 栈不变。非索引路径不初始化/消费上下文。

原官方 trace 的整段 `NVN submission planning` 范围保持；子范围明确改名为 `NVN attachment store index finalization`，只包含剩余的索引发布/scatter/别名闭包，不能和旧的整段附件构建子范围做等范围耗时对比。没有新增计时器或采样器。

## 源码和软件 GPU 资格

原始完整 `store_index_build` 被逐字保留为仅 `EMBED && !APP` 的参考函数（仅函数名变化），并核对与基线源码一致。每次成功构建后比较桶 footprint、begin/end、alias 链、last-read、原顺序事件、稳定 scatter 结果、全局需求和 epoch，使用各字段比较避免结构体 padding 干扰。参考执行产生的已有诊断累计值会恢复；不加入生产 APP observer。

normal 与 ASan/UBSan 两种构建、async 与 serial 四次 focused 运行均开启 Core/SYNC validation，只用软件 LVP：每次 128 次完整旧算法索引对照，实际原 `store_overwrite_test` 的 D32S8/D24S8 两种格式、覆盖/部分清除、mip/array-layer、采样/拷贝、独立/重叠存储、描述符重注册和 epoch 回退的像素与省 store 断言均通过；24 个 CALL/重复 child/JUMP/双 root 有限元数据用例也参加完整对照。短 payload 的精确小分配和 event 分配失败用例在 sanitizer 下通过。原 `planned-forward-reuse` 真实查询/DS/counter 回放另在 normal 和 ASan/UBSan 各通过一次。

负控在独立树中故意不给递归 CALL 传递 store 上下文；没有绕过原索引比较，实际被旧完整算法内容比较拒绝。候选本体未改成该负控。

以上执行原 NVN 录制器和 Vulkan 后端/软件 GPU 功能夹具，未执行真实游戏模块，未进行 Android canonical、NSO 重加工或设备运行。下一步是 Root 源审后结合已完成的其它独立候选安排一次构建与官方 CPU 对照；不能把本软件资格当成硬件性能收益。
