# NVN 事件负载由 GPU 发布

独立候选基线为 `200bc0c` 的固定共享池修正版，尚未合入主线、未部署设备。原 Metal 在完成回调里修改事件值；这能满足等待提交完成后的 CPU 观察，但不提供事件值与后续 GPU 访问的命令顺序。固定共享内存把 CPU 回调写和 GPU 写放到同一物理地址后，旧回调可能覆盖新 GPU 值。原 `f5e152c` 的最小反例与失败日志继续保留。

本候选对已经登记的 NVN Pool 事件值改用真正的 GPU 命令：WRITE 是四字节 `vkCmdFillBuffer`，ADD 是一个 invocation 的 32 位 `atomicAdd` 内建 shader。复用原 Pool 取址、读写依赖、GPU writer 标记、写回和引用生命周期。ADD 描述符按设备对齐下取整，push 指定唯一的 word，不读写对齐前缀。描述符随原 submission 资源保留；push 是每次派发的不可变值。没有新资源预测、CPU 内存扫描或游戏分支，也不需要 shaderInt64。

已经登记的 Pool 不能在失败后悄悄回到 CPU store。原 backend 接口允许的非 Pool 地址（例如自有夹具的栈事件）仍保留原完成后 CPU post；这不宣称支持非法 NVN Pool 地址或已 Finalize 的对象。Legacy 镜像模式经原 GPU 写回发布 CPU 值，shared 模式经原 fence／缓存相位发布，不再对同一事件值追加 CPU 回写。

原共同录制器仍丢弃 SignalEvent 的 location／flags，WaitEvent 仍有上游的受限假设。为保留“不得早于此前渲染完成”的下限，每个真实事件发布点保留 `ALL_COMMANDS → TRANSFER/COMPUTE` release；不是每 draw 增加全局同步，也没有新增 CPU 等待。它可能结束 Render Pass／occlusion span。WRITE 增加一个 Fill；ADD 还增加一个独立描述符与小派发，首次 ADD 创建内建管线。此候选不声称已实现 TOP、VERTEX_PIPE、任意 WaitEvent、CPU/GPU 并发信号或非 coherent 轮询的完整合同。

自有原 NVN 夹具覆盖 WRITE／ADD 后同录制 GPU 拷贝、GPU 更新后 ADD、加法回绕、事件后 GPU 覆盖、对齐前缀、页边界和 Pool 最后四字节，以及 ADD 夹在两次原 compute 派发之间。原反例现在得到 9，且旧路径同样得到 9。完整 async／serial、完整 key、动态 UBO 和 PL／shared-DS 回归保留。原缓存依赖夹具改用独立的 `vk_completion_write` CPU callback，继续检查 query 等剩余 CPU effects 的保护，未因 Event 变为 GPU 命令而删除其断言。

Query 的 GPU reduce／时间戳格式化尚未在本候选实现；同录制 generic CPUeffect 与 shared GPU 页冲突的临时拒绝仍保留，packaged image、ReadTexels 及 Android 非 coherent 验收也未因此完成。当前证据是原 Vulkan 后端、自有 NVN 数据和官方 LVP／validation／synchronization validation，不执行游戏模块，没有 FPS、带宽或温度收益声明。
