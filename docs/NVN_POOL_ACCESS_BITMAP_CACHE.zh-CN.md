# Pool 访问位图的录制上下文内复用

此优化保留原 NVN→Vulkan 页访问、initial-content、完整覆盖与 stage hazard 语义，仅复用真实完成且 command pool 已 reset 的 CPU 位图存储。默认开启；启动前设置 `DM_VULKAN_POOL_ACCESS_CACHE_REFERENCE=1` 使用原 calloc/free 路径。无运行时热切换、额外每 draw 标签、原子、计时或采样。

## 依据与范围

81e8529 的现有官方 Simpleperf 窗口有 669 个 NvN 用户态样本，5 个（0.747%）保留 `record_pool_use` 分配调用链，其中 4 个在 libc memset、1 个在 allocator 查找；精确 ARM64 调用点为三平面 calloc。`record_pool_range` 15 个 self 样本（2.242%）主要是页位计算与写入，不能全部归为可省分配。这里没有 FPS、热或净 CPU 改善结论。

`GPU_ACCESS_SHIFT=12`，每个位图 word 覆盖 64 个 4KiB 页。每个 pool 在一个 recording 中最多创建一个节点：pages/upload/overwritten 共一份三平面分配，七种 stage 平面按首次使用惰性分配。既有 last-range 和 single-word 覆盖快路不变。

## 生命周期与清零证明

缓存归原 `submit_resources`，只从原 `retired` 列表 reclaim 时接收节点；真实 GPU fence、全部 CPU effects、pool reference release 和 command-pool reset 均已完成。pool 本体可能已销毁，因此节点保存 `bitmap_words` 和已分配 stage mask，reclaim/trim 不解引用旧 owner。

新 recording 仅取完全相同 `bitmap_words` 的节点，不依据 pool identity、guest VA 或物理 window 复用逻辑状态。取出后仍重新取得当前 pool reference，并执行原 range 登记、初始化、dirty upload、pending 查询和 GPU hazard。

所有非 pages 位的写入均满足子集关系：

- `record_pool_range` 先登记 pages，再登记 upload/stage；既有早返只允许已覆盖范围。
- `vk_pool_overwritten` 先调用 `record_pool_use`，再登记 overwrite/upload。
- 其余访问这些平面的路径只读取。

因此回收时扫描 pages 的全部 word，仅对非零 word 清除三平面和所有已分配 stage 平面的对应 word。重置 last-range 元数据；stage mask 表示数组分配所有权，跨复用保留，不表示新的访问历史。新 stage 仍按原 calloc 失败语义处理。

每上下文最多 16 个 idle 节点、512KiB（含节点和全部实际已分配数组，不含 allocator 元数据）。超限/过大节点直接 free，owner idle pressure trim 释放全部 idle 节点；在途、reset-blocked、当前录制节点不参与。最多 16 个上下文意味着 idle 缓存理论上限 8MiB，非预分配量；active 节点不属于此 idle 上限。

## 代价

ARM64 O3/DWARF：节点 120→136B，上下文 20944→20968B；options 增加一个启动期 reference 字段。首次 pool-use 的缓存查找最多 16 个节点，命中免去节点/三平面及已存在 stage 的 malloc/calloc/free。reclaim 新增完整 pages 平面读取，以及每个非零 word 对全部已分配平面的清零；密集访问仍需同样的 zero stores，不能认为分配减少等于 DRAM 减少。

历史七池几何下，若同一上下文全部使用，额外 pages 扫描 53,600B；历史四个 backed pool 为 35,616B。这只是容量算术，不是当前每帧实测流量，亦不随物理 window 缩小而错误改变逻辑 offset。真实历史快照不同于本候选的游戏验收。

精确 NDK O3 静态指令数（含冷路径）record_pool_use 86→157、record_pool_range 621→609、reclaim_submissions 176→270、idle trim 95→186。对应 frame 仍为 96/144/96/80B；同 recording 已登记 pool 的快路不遍历缓存。静态指令数不能直接换算运行成本。

## 验证

独立 Lavapipe、`LP_NUM_THREADS=2`、官方 validation + synchronization validation：

- 新 owned fixture async 17553、serial 17470；原分配 reference 和 window 模式均通过。
- 实际 GPU 字节覆盖跨 pool 等 words 复用、旧 overwrite 和不同 stage 不泄漏、原 pool 销毁后 reclaim、同 guest VA 新 pool、尾部 193B、无效越界调用不消费 idle 节点。
- 真实 timeline fence 和 command-reset 两阶段 gate 证明 pending/reset-blocked 节点不能复用；压力 trim 和 17 个节点的 16-entry 上限有实际观测。
- 仅在隔离构建故意漏清 overwrite 平面时，后续 pool 的实际 CS 输出断言失败；正确源未被改写。
- 完整 async/reference 各 22031790 checks、serial 21929811 checks，零 VUID/SYNC；NDK Android29 O3/TLSdesc 后端与 fixture 均编译通过。

owned 入口 `DM_POOL_ACCESS_CACHE_FIXTURE=1`；原 full suite 不因新测试选择器改变顺序。512KiB 字节上限有实现检查和全部已观测缓存字节断言，但本 fixture 未刻意制造超过 512KiB 的 bitmap payload；不能把 entry-cap 触发当 byte-cap 故障注入。未运行本候选 Android APK 或真实游戏，不提供新整帧或热收益结论。
