# 前台私有图像缓存预算候选

Android 官方 `ComponentCallbacks2` 文档说明：从 API 34 起不再通知
`TRIM_MEMORY_RUNNING_*`，`onLowMemory()` 也不再调用。因此已有系统回调和
暂停时的清理仍保留，但不能作为前台冷图像缓存的唯一回收触发。
来源：https://developer.android.com/reference/android/content/ComponentCallbacks2

本候选是 `ed2c139` 的 guest-only 后继，不改游戏资源、内存池内容或 NVN 指令语义。
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

只有 HLE `x_submit()` 在翻译任何命令之前调用 `mtl_sub_guest_begin()`；该入口在
Vulkan 执行预算维护后进入原 begin，在 Metal 只转发原 begin。内部 stream flush、
CPU preflight、present 等继续使用原 `mtl_sub_begin()`，不维护缓存。这样不必推断
内部重开时的调用栈是否还借用某个 image 指针，也将维护频次限制到原 guest 提交。

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
timeline 阻塞下的 in-flight 保护、最终物理分配计账归零；新增真实超预算且冷却足够
时内部 begin 不淘汰、guest begin 才有界淘汰的对照，恢复旧 ed 内部维护位置应被
同一用例拒绝。另复用 shared-retirement
和 native-resident oracle 检查共享导入、GPU 别名依赖及旧物理副本生命周期。
这些 host/Lavapipe 结果不代替 Android 原游戏、6GB 设备或功耗验收。


## 真机集成与预算边界（2026-10-21）

同一773候选已在Q735真实游戏（native6217/f830/sharedON/.60fixed）和
Mali PHP110真实游戏（系统Vulkan/sharedOFF/原ASTC4/cbd8 shader/.75adaptive）
完成行走、官方工具采集、原生命周期save2和环境恢复。源码作为有界前台回收机制
合入；512MiB仍是暂定软预算，不是本轮已取得内存节省的参数结论。两次实际释放
量均为零：Q可重建118.99MiB，Mali结束时82.20MiB，因此不为强迫命中而压低预算。
Mali的534.37MiB私有图像总量不能全部淘汰，GPU写入内容等保护继续生效。

Mali的系统showmap成功取得6270项独立VMA，PSS各项求和与TOTAL一致。
按原guest AS日志地址定位，guest heap实际PSS约1344.31MiB，其余VMA共415.07MiB，
其中libc_malloc约299.78MiB。dumpsys另报GL mtrack约1602.38MiB，整体PSS约3374MiB。
这些是不同统计视角；不能把VMA allocation、镜像覆盖范围或累积snapshot再相加，
也不能用Native Heap Alloc约1.6GiB推断它们都已驻留。
原普通Pool确有镜像及CPUshadow路径，但showmap未暴露p->shadow指针归属，不能
把所有libc_malloc直接标为shadow。本轮优先继续处理guest驻留与驱动资源，不把
小型可重建缓存当作全部内存问题。

Mali从初始.75动态降到结束.65；本次不是与fixed.60匹配的性能对照，截图瞬时
FPS也不代替持续游玩/热/6GB验收。原022生产APK、模块、shader、驱动、资源和缓存
已恢复，未把候选测试包长期留在设备上。

实际root凭据：`.local/logs/mali-candidate-guest-773d530-v1-root-actual-review.json`
（SHA256 `9fa235de5f04dfee27f5b693beba1beaa767eaae19dbef06b771d2044c54c4f8`）。
showmap分析：`.local/logs/mali-candidate-guest-773d530-v1-analysis/official-showmap-memory-analysis-agent.json`
（SHA256 `7fbb0eab1719b04b2da03da2cc867ee50545f8f1d9ef764209fbdff4758647c9`）。

### ASTC6 的同阶段轻量内存对照

相同 773 运行时、系统 Vulkan/sharedOFF、原 cbd8 shader/UI，ASTC4 与修正后
11,605 项 ASTC6 资源分别完成相同两段营地行走，然后立即使用系统 meminfo
与一次 showmap。两轮全部实际快照均为 `.75`，行走初始/结束坐标相同；
本轮不重复 CPU/GPU 采集，消除旧 full4 在长采集后降至 `.65` 的阶段混杂。

本次 ASTC6 的总 PSS 为 3,224,228 KiB，ASTC4 为 3,270,622 KiB，差
45.307 MiB。其中 guest heap/Unknown 少 32.762 MiB、GL mtrack 少 7.020 MiB，
Native Heap PSS 少 2.676 MiB；这些类别已包含在总量里，不能再次相加。
showmap 是另一个重叠视角，也不能与 meminfo TOTAL 相加。两轮 Pool backing
容量都为 387 MiB，先前与更晚端点相比的 48 MiB 容量差没有复现，不能归为
ASTC6 的固有收益。私有图像 allocation 少 16.673 MiB 也不直接等于物理 PSS。
两轮预算实际释放均为零，内存变化不是前台淘汰取得的结果。

这是同测量流程的一次短营地端点对照；实际 tick 为 302/321、PSO 数为
178/181，不能称相同工作量的多轮随机 A/B，更不能证明持续发热、带宽、
野外战斗或 6GB 已验收。两次均经原生命周期 save2 后停止并恢复原包资源。
对照见 `.local/logs/mali773-ASTC4-ASTC6-lightweight-memory-comparison-v1/result.json`
（SHA256 `1f367c7c444356b74f650056caece7c684ca6d064854049c01e6c42bf4b8b63d`）。
