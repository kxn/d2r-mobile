# Vulkan 临时 Buffer 空闲回收

临时 Buffer 仍按大小和读回属性复用，硬上限仍为 256MiB。此前只要没碰到上限或 Android 显式 trim，已经完成的冷 Buffer 就会一直保留。历史官方完整帧 API/资源图中该 usage 类共约 91.4MiB，该帧实际引用约 1.35MiB。这说明值得检查缓存驻留，但不能据此认定其余都是当前游戏的可释放物理内存，也不能把历史帧当成当前工作集。

默认策略：可复用列表超过 64MiB 时，至多每 256 个队列提交块扫描一次；只回收至少 1024 个提交块未被请求的对象，达到软上限就停止。提交块不是画面帧，也不是时间。持续使用的工作集允许超过软上限，避免为了追求缓存数字而反复分配。原有 256MiB 硬上限保留。

所有操作发生在原有录制线程的 reclaim 边界。临时对象必须先完成 GPU 执行、CPU completion effects 和其独占命令池重置，才进入可复用列表；在途及 reset-blocked 的对象不会进入该列表。回收不引入等待、提交、写回或图像驱逐。每次请求记录最后使用的队列 serial；不读取热路径环境变量、不增加计时器。

额外成本：temporary 元数据从 56B 增至 64B；每次请求多写一个 64 位 serial；超过软上限时，每 256 个提交块最多遍历一次可复用链表，并承担真正销毁对象及未来冷重用时重新分配的成本。回收只减少仍空闲的逻辑分配，不保证 VMA backing block 或系统物理驻留按同样大小立即下降。该策略不能独立证明涨帧或解决过热。

冷启动参考开关 `DM_VULKAN_TEMP_CACHE_AGE_REFERENCE=1` 禁用本次自动老化，保留原来的硬上限及显式 trim，便于同包对照。

原 NVN 夹具新增 `DM_TEMP_CACHE_AGE_FIXTURE=1`：使用小阈值和真实队列 serial 验证冷对象释放、热对象 VkBuffer 复用、GPU copy 字节、VMA 逻辑 allocation 下降，以及 timeline gate / command-pool-reset gate 下在途对象的保留。另运行既有 idle-trim 和完整 async/serial 原 NVN、独立 Store 参考、官方 Vulkan validation / synchronization validation。夹具结果不代表游戏原生执行或真机散热验收；部署和真机记录单列。
