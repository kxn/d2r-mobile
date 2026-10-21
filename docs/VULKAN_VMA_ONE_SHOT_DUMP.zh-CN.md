# 一次显式 VMA 内存图

目标是解释现有 Mali 官方 GL mtrack 与应用资源分配计账之间的缺口。GL mtrack 包括驱动管理的内存，不能先将差额称为浪费；应用当前 `vk_memory_stats` 仅返回 VMA AllocationBytes，缺少其底层 BlockBytes、空闲区间、memory type 和详细分配图。

沿用固定版本 VMA 原生 `vmaBuildStatsString(allocator,&json,VK_TRUE)` 与 `vmaFreeStatsString`，原 JSON 不包裹、重算或筛选。默认 `VMA_STATS_STRING_ENABLED=1`，但原成品未引用的导出可能被链接器删除，新的最小调用才会使其进入成品。当前 wrapper 使用 AllocateMemoryForImage/Buffer，因此分配类型为 IMAGE_UNKNOWN/BUFFER，Usage 为未知；未增加名称或热点分配计账。JSON 可直接展示分块/专用分配/空闲空间，不能进一步凭空区分哪个 Buffer 是临时缓冲。

Android 仅在已有 Activity 收到显式 `vma_dump_on_suspend=true` Intent 时请求一次；JNI 只原子置位，busy 请求不重复排队。调用者没有输出路径参数。正常运行默认 0，不在 game-state tick、draw 或 submit 上遍历 VMA，也不增加周期采集。后续正常 Android 暂停路径原有 `x_drain → mtl_drain → x_trim_after_suspend` 不变；在本来已有的 XJ_IDLE_TRIM owner 任务中、本任务原 trim 调用前消费请求（此前 tail FRAME 可能已执行原 trim，不保证首次 pause-trim 前）。没有新增 job、GPU wait、提交、trim、重试或更改玩家状态。原同步/未启动 owner 路径清除请求并报告不支持，不为诊断初始化 GPU。

这个边界覆盖完成线程：原 mtl_finish 先等 queue serial 的 GPU 和全部 CPU effect（包括旧图物理分配退休回调），再等原 command-pool reset 返回并发布 queue_reset_completed。单有翻译线程空闲不能证明这一点。固定 VMA 的 DedicatedAllocationList::AddDetailedStatistics 未自行取得列表锁，不宜在普通前台 guest begin 直接调用并假定 completion 已停止。dump 当刻还检查 pending/in_flight 为空、queue_reset_completed 覆盖 queue_submitted；不满足直接清请求并报告失败，不增加等待来凑准入。当前 tail FRAME 的 mtl_set_frame 只更新相位与缓存统计，不提交 GPU；检查仍保留防止其他工作撤销静默条件。后端同时保留现有 resources mutex 排除普通 PoolFinalize 等资源更改；只在构建原 JSON 期间持锁，文件写入时已释放，不增加全局分配热点锁。

输出固定为启动时已锁存的 app-private DM_TEMP 下 `vulkan-vma.json`。先排他创建固定 `.part`（不跟随链接），完整写入后 rename；失败只清理本次创建的部分文件，请求已消费，不自动重试。任何输出失败都不阻止原 trim。完整 JSON 的 CPU 构建、临时字符串和文件写入会增加一次暂停收尾成本，不能用于帧率/功耗测量。VMA 会临时构造并复制字符串；返回内容在完成/失败后立即释放，不长期保留副本。

当前 allocator 未打开 VMA 的 EXT_MEMORY_BUDGET flag，原生 JSON 的 Budget/Usage 会采用 heap 80% 与 VMA BlockBytes 等估值，绝不能冒充实际 GL mtrack 或 RAM/PSS。这里是原生命周期 post-drain、本 IDLE_TRIM 任务 trim 调用前的分配快照；此前 tail FRAME 可能已执行原 trim，不能称首次 trim 前，与前台 meminfo 为不同阶段；它可以解释分配/分块空隙，不能强求数值精确等于另一时刻的 GL mtrack。非 VMA 的 swapchain、驱动 PSO/descriptor/command 内存，以及直接外部共享导入都不在 VMA BlockBytes 中。

复用原 idle-cache-trim GPU 用例：请求发生时仍在记录/真实 GPU gate 和 completion reset gate 均不输出；原暂停后才得到有效 JSON，随后原释放行为和像素不变。固定 `.part` 被占用的失败会清请求，原 trim 完成，下一次无新请求不重试。宿主只用标准 JSON 解析器校验原报告与 dump 当刻、同一 resources 锁及静默条件下既有 VMA stats API 的返回分配数量/字节/块数，不另造统计器。Lavapipe 测试不等于 Android 原生游戏验收。
