# 前台私有图像缓存预算候选

Android 官方 `ComponentCallbacks2` 文档说明：从 API 34 起不再通知
`TRIM_MEMORY_RUNNING_*`，`onLowMemory()` 也不再调用。因此已有系统回调和
暂停时的清理仍保留，但不能作为前台冷图像缓存的唯一回收触发。
来源：https://developer.android.com/reference/android/content/ComponentCallbacks2

本候选基于 `80a71bc`，不改游戏资源、内存池内容或 NVN 指令语义。
`vk_memory_allocation_bytes()` 读取现有 VMA AllocationInfo.size，记录每个
独立私有 VkImage 的真实分配范围，不把 native 导入或同页别名计成第二份内存。
物理 promotion 的旧、新分配同时计入总量，旧分配直到原 completion 回调才扣除。

现有 game-state 增加 `private_image_cache`：

- `allocation_bytes`：仍存活的私有物理图像分配，包含等待 completion 的旧分配。
- `cpu_rebuildable_bytes`：仍在逻辑缓存中的 CPU 可重建私有图像分配；包括近期或
  尚在飞行的图像，因此它不是“马上可以释放的字节数”。
- `budget_bytes`：只对上一项生效的软预算。当前源码中的 512 MiB 是待真机工作集
  审核的候选值，不是已验收的默认参数，也不是进程或总 GPU 内存的硬上限。
- `released_allocation_bytes`：预算策略累计交还 VMA 的 allocation 字节数；不是
  硬件 DRAM 流量，也不是当前 RSS 降幅。VMA 可能暂留一个空 block。

普通游戏提交开始前，在没有 command buffer、recording resources、binding scope
时，仅在可重建缓存超预算的情况下检查候选。每次最多检查 32 项、释放 4 张；
释放达到 8 MiB 后停止（单张大于 8 MiB 时仍只释放这一张）。保留游标，下一次
继续，没有每 draw 或每 submit 的全链扫描。图像列表增加反向链接，使删除不需要
再次从表头寻找。未超预算时只读一个资源计账原子量。

淘汰沿用 `image_destroy()` 及其依赖和缓存失效流程，且必须同时满足：CPU 原始
内容足够重建、已初始化、存在原 layout/Pool 来源、未暴露裸句柄、真实 GPU 完成
且至少冷却 256 个 queue chunk。native 导入、GPU 写入结果、正在 recording/binding
或 in-flight 的资源不淘汰；不等待 GPU，不提交新工作，不写回或丢弃客体数据。
以后再次使用原逻辑 descriptor 时，仍由原有 preflight/upload 路径重建。

额外成本是每个内部 image 的分配计账与链表字段、CPU 重建状态转换时的资源计账，
以及超预算边界的有界检查。图像被再次访问时可能增加驱动对象创建和上传，因此
参数必须根据实际 `allocation_bytes/cpu_rebuildable_bytes` 与原厂 Profile 选择，
不能为了立即降低数字强压热工作集。

验证复用原 image-pressure-trim GPU 用例：真实多 mip/array 上传与逐像素重建、
GPU clear/copy 内容保护、裸句柄保护、40 张冷图像的每轮额度及游标进展、真实
timeline 阻塞下的 in-flight 保护、最终物理分配计账归零。另复用 shared-retirement
和 native-resident oracle 检查共享导入、GPU 别名依赖及旧物理副本生命周期。
这些 host/Lavapipe 结果不代替 Android 原游戏、6GB 设备或功耗验收。
