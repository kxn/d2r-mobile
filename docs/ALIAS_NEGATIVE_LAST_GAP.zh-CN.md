# 相邻只读 Buffer 范围复用纯几何空洞证明

基于 98a534e，保留原 32 槽 exact `(alias epoch,start,end)` 首试，增加一个 24B 的 last-gap 证书。只有请求整个落在相同非零 alias-index epoch 的已证明空洞内时，才跳过重复几何查询。证书不含节点指针、内容、initialized/encoding 标志、CPU authority、GPU 可见性或 writer history。它不替代 Buffer 解析、初始化、range 记录、真实 barrier、flush/invalidate、native acquire 或退休。

冷查询沿原 AVL 的一条路径，以 `node.start < request.end` 分区。前缀的最大 end 包含沿途节点及完整左子树的 `max_end`；后缀取最小 start。前缀最大 end 不大于 request.start 时，整个 `[prefixMaxEnd,suffixMinStart)` 没有几何交集。重叠区间、相同 start、原 unsigned stored-end 语义都不需改节点；原空树和 `root.max_end <= start` 的常数快退保留。存在任何几何交集时仍查询原 live eligibility，不能把暂未初始化等状态缓存为安全。新增范围证书只在旧 exact miss 后检查，gap hit 不维护 exact 表；新冷证明仍填原 exact 项。

同一个 owner 元数据从 776B 增到 800B；不增每节点字段、锁或分配。旧 exact hit 不增加后续检查；exact miss 增加有限的 epoch/包含关系比较，cold lookup 是 O(logN)，不全表扫描。epoch 在原 writer 身份插入/移除点失效，wrap 到零后两种缓存均永久停用。实际物理 promotion 若保留原 logical node/range 无需另造 epoch；逻辑退休、CPU rebuild、pool close/shutdown 仍走原 remove。

既有 846 官方 583 个翻译线程样本中，alias leaf 共 17：10 个 UBO+5 个 VBO 位于现 exact cache miss 后的纯几何查询；另 2 个 native allocation 查询用于实际 named/HOST barrier 资格，不属于本候选。原记录没有地址/长度/缓存占用，不能将 15 个样本称为可省或预测 FPS/CPU 时间。具体 PC、调用链和早先统计更正在 `.local/logs/alias-geometry-846-readonly-v1/result-v2.json`。

复用原 interval/full-list oracle、新增空洞边界/不同范围/碰撞/嵌套/重复起点/插入移除/wrap 测试，以及原真实 GPU shared-image writer/退休/storage promotion/重注册、native window/alias/previous-writer fixtures。GPU 夹具在 Lavapipe/LP2 Core+SYNC 与 ASan 下运行；ASTC 硬件不支持时保持 unsupported，不称 Android 或真实游戏验收。epoch 故障负控及所有原始日志由 source-ready receipt 归档。本候选无新运行时计时器、采样器、命中统计或游戏判断；源码阶段不构建 Android 包、不访问设备。
