# Descriptor pool 物理块 512 候选

独立于 exact `88620ec7c92edeb59512612cebf42906a4848509`，仅将 `descriptor_create` 的物理增长块设为 `min(V.options.descriptor_sets,512)`。原配置默认/硬上限均为 **2048**，其留存/完成后 reset 阈值不变。类型配额比例、布局/template、immutable set、批次增长 1/2/4/8、溢出链及完成后回收顺序不变。没有新增运行开关、缓存、计数器或常驻结构。

## 改前预算与新增成本

原 Q735 官方 `TU_DEBUG=bos` 最后完整表有 16 个 descriptor pool BO，各 13.5 MiB，总 216 MiB。精确 f830 `tu_CreateDescriptorPool` 在创建时按所有类型配额分配 BO；dynamic UBO 配额另外进入 host allocation，不是该 BO。512 的相同比例 BO 为 3.375 MiB，**若仍是 16 块**则为 54 MiB，条件差 162 MiB/75%。这不是 PSS、物理驻留或净收益测量。

实际块数可能增加；没有当前每 context 占用分布，不能保证总量降到 54 MiB。BOS 已有 13→8、16→1 的销毁/重建，不能称原 216 MiB 永久泄漏。累计 allocated sets 也不是同时占用。小块增加溢出时的 Vulkan 分配/链节点及未来 reset 调用；已经分配的集合仍按 2048 保留重用，不会因为 512 而每帧强制 reset。小于 512 的原测试配置原值保持。

支持的消费路径上限 128 个单元素 binding，最大分配批次 8；最小类型配额 `512*4=2048` 足够最坏同类 `128*8=1024`，maxSets 也足够一个批次。原 unsupported descriptor arrays 检查保持。低配额配置不是本候选新增合同。

## 生命周期与功能资格

原 `vk_descriptor_allocate` 仅将 OUT_OF_POOL_MEMORY/FRAGMENTED_POOL 当溢出，批次失败先试单个，再沿旧链/新块，其他错误仍失败。f830 批次分配失败会释放已分配部分并清空输出，原 all-or-nothing 重试成立。只有原 GPU fence 和 CPU completion effects 均完成后才 recycle；旧/在途池和集合不提前 reset 或销毁。

复用原 `descriptor_inflight_test`，旧调用仍为 6 sets；新 !APP 场景分别为 768/2304 sets。A、B 独立 timeline gate：A 完成并回收时 B 必须仍 pending，然后 A 编 C；最终三提交全部逐字验证 `(10000*run+100*set+word)*3+7`。768 跨物理块后必须保留并重用，2304 才能触发原 2048 reset。真实 create info 核 512、类型数，两个错误结果均实际进入原重试路径。

Lavapipe 自然不执行 maxSets 上限，首轮强断言失败已保留。最终仅 !APP 夹具在真实 create-info 容量边界注入上述官方错误；成功的分配、更新、提交、等待、读回均为真实 Vulkan。故这是通用错误处理/寿命与 GPU 内容资格，**不是 Turnip 自然耗尽或 Android 游戏验证**。normal Clang19、GCC ASan/UBSan，serial/async、Core/SYNC 各一例；另原 descriptor hint + dynamic UBO 内容两例保留小池配置。APP 有效预处理排除新 helper/body/dispatch/literals。没有运行游戏模块、APK/RAW 构建或设备操作；自然 Turnip spill、块数量、BOS/PSS、重建/运行成本待后续原事务验证。

容量证明的边界：`shader_descriptor_layouts` 不是简单 nb 计数，它将每项 `b->count`（验证 1..128）写入 `VkDescriptorSetLayoutBinding.descriptorCount`。metadata/legacy 能构造 array layout 不代表已实现其消费：`binding_consumer_compile` 要求 count==1，任一项失败使整个 compiled program 不建立；原 `shader_bindings_selected_impl` reference 分支在任何 descriptor 材料化/分配前仍逐项拒绝 count!=1。因此没有将已支持的 array 消费削减。DPL 只转换 count1 sampled，tssbo 只转换 count1 texel，其余项仍经过上述原拒绝；spec/pred 沿原 schema。所有其他生产 `vk_descriptor_allocate` 调用（query、present、retile、extrap、event、bright、BQ）均固定 1–3 个 count1 binding。未来支持 arrays 时必须重新审计配额，不能沿本证明自动认为任何合法 Vulkan layout 都可用。
