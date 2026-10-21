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


## Android 真机短测与主线合入

`633ea750561f3cc3b072acc82e44a444cb25b045` 已完成完整 canonical Android 构建（API29/O3、native jobs 2），APK SHA256 `d45d489a6f051cffd2653ded6eb7ee2a680edae207d2ee2acfb08c853c2c7ec9`，宿主 ELF Build ID `813b080460461f7c705465e1d25f4558aa061448`。原始三份 NSO 重新加工、28 产物独立 verify-only 通过。APP 中不含 host 握手及 getter；保留的 Android fixture selector 拒绝 stub 仅直接报错，不能将同名 stub 当作执行了宿主用例。

Q735 真机 PID 11645 完成真实游戏模块、原三段营地步行与 simpleperf/Perfetto 采集。实际 HUD 前后截图未见明显新增黑块或几何错误，原绿色地面斑点仍在；不是全场景视觉验收。原生命周期 fresh save2 后停止，原 APK、驱动、模块、资源及缓存恢复，清理错误为空。模式仍为 native6217/f830/sharedON、0.60 fixed、2750×1260、1 MiB 导入窗口，不代表默认 0.75 或实际 6 GB 长期游玩。

原 collector 路径 13/633 个用户态样本，在本次 644 个样本中未采到（self 与保留的 inclusive 均为零）。这与无释放时跳过扫描的机制相符，但不能称零调用或把原 13 样本全换算成省下的时间。整翻译线程 Running 为每个有效 Surface 间隔 27.2190 ms，前次 10c 为 26.6791 ms；replay 为 26.0830 / 25.4849 ms，加权频率为 1618.20 / 1626.66 MHz。本轮 PSO/variants 174/31 全窗稳定，前次 record/scheduler 为 182/33，且前次 stat 不稳定，因此不是相同工作量 A/B。没有整线程净 CPU、FPS 或温度收益证明；保留依据是已证明的扫描工作消除、明确的小额成本和正确性/真机短测通过，不能将本项作为持续散热或低内存目标的完成证据。

根证据：`.local/logs/native-retirement-event-gate-633ea75-root-canonical-raw-review.json`、`.local/logs/native-candidate-short-633-Q735-v1-root-actual-review.json`、`.local/logs/native-candidate-short-633-Q735-v1-analysis/actual633-vs10c-official-summary-agent.json`。内存端点 PSS 约少 2.02 MiB，仅作独立端点记录，不归因于新增 8B/Pool 的本改动。
