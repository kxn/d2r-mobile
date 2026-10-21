# 纹理退休索引：通用 CPU 工作量候选

## 依据与范围

A650 的原官方 Simpleperf 数据位于 `.local/logs/tablet-shared-633-f830-v1/cap3-scale50-cpu-early/retirement-hotspot-evidence.json`（SHA-256 `f2698be5f4c356579825a7fd90b53876d7d903506c7f4f51d30e782152c1d28d`）。实际 eeb9fc0/f830、原 BC 资源、shared ON/native OFF、3D 比例 .50；3329 个全进程样本、无丢失，其中 NvN 用户态样本 1515。`vk_texture_end_submit` 的两个叶 PC 对应每个退休 serial 再遍历全部 `images`，占该线程采样周期约 60.462%；全 descriptor capacity 扫描约 2.706%。

这是启动/地图加载阶段的 8 秒窗口，且当时系统 EGL q3dtools profiler 开启。它证明具体算法有高成本，不能外推为稳定营地每帧占比、预计 FPS 提升或 GPU 时间。独立 profiler OFF 对照由另一工具事务执行；本候选没有访问设备。

## 改动与语义

1. 退休的物理图像通过原有 `image_buckets[serial % 8192]` 查找。没有创建第二套 VkImage/物理资源缓存。原图像插入、销毁、promotion 和 O(1) 全链摘除不变。
2. 为逻辑纹理注册维护独立的 serial 哈希反向链。ID 使用 `id+1` 作为 token；每个实际注册 ID 有 8 字节 prev/next，按 256 ID 页面懒分配。descriptor 快照及其 TLS cache 布局没有增加字段；实际 64 位宿主 DWARF 显示原 `texture_descriptor` 仍为 200 字节。
3. 同 ID 重注册时从原 serial 桶摘除、挂入新桶；相同 serial 且已在索引内可保留原链。新 view、valid 和原 descriptor epoch 仍按原方式发布。
4. limit 缩小只改变可见性，不删除隐藏注册。退休遍历完整 serial 所在桶，只失效 full serial 匹配的 ID；哈希碰撞的其他 serial 不受影响。
5. 已 invalid 的历史注册仍保留在链中。原实现对 stored serial 匹配的 invalid/隐藏 slot 也推进 epoch，本候选保留该行为。索引不因 valid 清零而丢掉记录，直到 ID 重注册替换它。
6. 原 8 guest submission 快照宽限、翻译水位、实际 GPU last_use/completion、早期 alias 解除条件、提交与等待均不变。所有注册/索引/退休访问沿用 `V.resources` 互斥锁。

原注册表在 `vk_texture_shutdown` 后仍保留，本侧表具有相同进程生命周期；没有单独释放侧表而遗留未索引的注册表。没有缓存 CPU dirty、GPU readiness、hazard、物理 view 或对象存活结论。

## 成本与边界

旧路径对每个待退休 serial 分别搜索全部物理图像和全部 descriptor capacity。新路径仅搜索相应物理 hash 桶及注册 hash 桶，并逐项比较完整 serial；复杂度取决于桶内碰撞，不声称最坏 O(1)。对于没有相应物理图像/没有相关注册的 serial，空桶可以直接结束。

新增成本为注册时 O(1) 链维护与一次页分配（首次触及该页），退休时每个桶节点额外读取分页链接。64 位静态头+页指针 64 KiB，每个触及页面 2 KiB，全部 1,048,576 个 ID 都曾注册时共 8 MiB 链页。不是每个 descriptor 快照增加 8 字节；高 ID 没有注册的页面不分配。历史 invalid 节点仍占用有界侧表。如果 serial 人为集中在单桶，遍历和指针访存仍可能很大；不能据算法变化声称所有场景即时涨帧。

## 验证

仅 x86 Linux/Lavapipe 软件 GPU，原 NVN C 实现和 Vulkan 后端；没有执行真实游戏模块。宿主 -j2 构建、Core/SYNC validation、ASan+UBSan，异步/串行分别运行：

- 新 registration/retirement fixture：多 ID、同 ID 替换、不同 serial 同 hash 桶、跨 hash 桶替换、serial 0、limit 隐藏/恢复、重复 invalidation/epoch、TLS scope、稀疏页与链接双向完整性；后接原真实 GPU gate 的四种 template/plain 与 cached/reference 组合，保留 STORAGE/格式 promotion、嵌套绑定 scope、pending GPU 保留、完成后释放和独立采样像素断言。
- 原 shared image retired alias 用例：GPU 未完成、暂扣翻译水位、物理快照宽限与同地址 HDR→BC3 三 mip 真实 GPU 字节；原 packaged B2T/T2T/CPU 同值改写用例也保留。Lavapipe 不支持原生 ASTC，原 ASTC 分支明确 unsupported，没有冒称 ASTC 硬件验证。
- 原 operation geometry production policy 用例：图形/计算、UBO/SSBO/VBO/index、ghost 和跨实际提交的资源消费者；不改既有像素/字节 oracle。

有限故障注入使用独立源码/二进制：省略反向注册发布、忽略完整 serial 碰撞判断、物理 image 退休使用错误 hash 桶，必须被原有完整性/epoch/真实 GPU 退休断言拒绝。源数据、测试命令、真实二进制与日志哈希在 source-ready 归档列出。第一次负控工具复制二进制时漏 executable mode，尚未运行测试即 PermissionError；该记录保留，后继改用保留文件模式的复制后才取得实际负例，未改任何运行时断言。

尚未 Android canonical 构建或真机部署，没有新的帧率/CPU 时间实测。后续应使用现成官方 CPU 工具，在同资源/分辨率/加载阶段核查退休叶成本和新增注册成本；不需要另写计时器。
