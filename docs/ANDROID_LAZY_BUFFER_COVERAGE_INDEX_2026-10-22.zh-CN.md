# Lazy Buffer 物理覆盖索引

基于 `533820e`。只优化已开启 coherent lazy shared imports 时的物理覆盖查询，不改 NVN、游戏或 Shader 语义。

已有 Q735 同版本三窗官方 Simpleperf 中，`shared_pool_storage_covered_locked` 自身约占 NvN 用户态采样 period 的 2.77–2.81%，包含子调用约 3.80–3.87%。这些与资源绑定、纹理消费路径重叠，不能相加，也不能把绑定路径约 22% 全部算作收益。目标是减少这段查询中的链表遍历；没有真机收益结论。

## 所有权及行为

- 原 `backings` 和 `shared_covers` 两条链继续负责 Buffer 选择，顺序、句柄、物理偏移及已有 hint 不变。新树只证明逻辑范围是否由真实物理导入的并集覆盖，不能拼接两个 VkBuffer 当作一个绑定。
- Buffer 使用独立的 typed AVL，复用原 `alias_range` 的插入、平衡和删除算法；native image 的独立树与 `released` 判断保持原样。native 的非零 released 状态不参与覆盖证明。
- 只有成功的真实 lazy Buffer import 才在原 `V.resources` 锁内发布节点。已有节点、句柄和起点不变，无新增锁、引用、内容 epoch、payload 拷贝或查询分配。
- 原 Pool 关闭及引用/GPU 完成流程结束后，销毁每个 Buffer 前从树摘下它，全部链销毁后要求树为空。CPU/GPU 内容变化不改变物理覆盖，不需要更新此索引。
- 范围、空洞、原实验 node/bytes caps、page/stage 记录、CPU 可见性和生命周期检查不变。

每个 `pool_backing_span` 在编译 shared-pools 时增加 64 字节节点，每个 Pool 增加 8 字节根指针。嵌入的 `whole` 也增加 64 字节，因此每个 Pool 另有固定 72 字节；非 lazy span 同样承担结构字段成本，但不发布索引。按已有现场 338 个独立 Buffer span 约增加 21 KiB，不能把它误写成整进程总增量。查询复杂度为 O(log N + 实际重叠)，高度重叠时仍可能访问多个节点；新导入维护为 O(log N)。

## 验证边界

沿用原八个 host GPU 用例及原官方 Vulkan validation/Core+SYNC、ASan/UBSan 配置。原 alias GPU 用例新增现有 coverage 查询断言，验证冷空洞、相邻真实导入、重叠 cover、嵌套 scope、尾部整池导入及关闭后的缺失；原双向 GPU 字节、旧句柄与 producer/CPU 可见性断言不变。仅 host 的 metadata oracle 以原双链线性查询逐范围核对树结果，覆盖 native released=1/2、删除、重复起点和嵌套区间。该 oracle 排除于 Android APP。

这些是底层正确性验证，不是 Android 真实游戏运行或性能收益。原始 NSO、资源和驱动不变；Android 产物仍须走原 canonical/fresh-NSO/独立 verify28 入口后再由真机验证。
