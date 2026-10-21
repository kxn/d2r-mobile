# Native import retirement 通知门候选

基线为 10c5369。真实 Q735 10c 官方 Simpleperf 中，NvN 的
`native_allocation_collect_locked` 有 13/633 self 样本，均来自
`begin_submission → vk_pool_native_image_collect`：8 个 PC 在读取 released 后的
比较，3 个在链结束判断，2 个在游标推进。它们仅定位重复扫描，不能换算成可节省时间。
原始分析和精确 PC 保存在 `.local/logs/native-candidate-short-10c-Q735-v1-analysis/`。

每个 Pool 增加一个 64 位 atomic pending 数。每次实际 release 先把状态置为 1，
再登记 pending，最后减 live image 引用并以 release-store 发布状态 2。
状态 2 发布是该释放者最后一次访问 allocation/Pool，之后 collector 可立即释放节点。
collector 持原 resources 锁；pending 为 0 时保留原 empty-list/index 一致性检查后返回，
不遍历 native 节点。非零仍走原链/index/free，只有确实摘除状态 2 节点才减 pending。
状态 1 节点不会消耗通知。扫描结束不清零标志，因此并发完成的发布不会丢失。

Pool 的每个节点最多登记一次、最多摘除一次；重复 release、计数溢出/下溢仍硬失败。
live image 引用下降之前已有通知；真实 Pool close 仍先 finish、关闭图像、等待原引用，
再 collect。原图像销毁/队列完成/fence/索引/缓存/authority 次序不变。
不支持新增任意线程提前关闭仍在发布中的 Pool；测试线程只在 recorder 稳定、Pool 存活时
执行有界 release，且 join/完成确认后才关闭 Pool。这没有扩大现有线程接口。

新增成本为 8B/Pool（NDK DWARF: Pool 304→312B），节点仍 184B。
每次 collect 增一次 acquire-load；仅实际 release/摘除时各增一次 atomic RMW。
没有新增 draw 查询、锁、运行时计时器或统计采样器。pending 非零时仍可能完整扫描，
不是替代退休队列。没有发生 release 的常态省去整个 resident native 链检查。

host-only fixture 复用原真实 memfd import、RGBA clear/readback 和 GPU timeline gate：
3 imports/2 Pools/多 submission，零通知的重复 collect、state1 暂停发布期间的重复 collect、
真实 inflight 完成后销毁、其他 Pool 不受影响、ready2 最终摘除与 Pool close 收尾。
异步路径由真实 GPU gate 保持工作未完成；串行路径在 GPU 已完成后用 release worker 交错。
每条路径检查实际像素、原计账与节点/ref/pending 集合。没有执行 ASTC 或真实游戏模块。
原 native-only→晚 Buffer、两次 union、pending acquire、resident/previous writer 继续原 oracle。
测试握手/getter 仅 host test 编译，实际 Android APP 对象中不存在。

此候选仅 source/host/ASan/实际 NDK 对象验证；尚未 canonical、真机或 FPS/热验收。
