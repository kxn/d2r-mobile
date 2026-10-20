# Storage bias 的 CPU 初始化范围

`1324492` 的绑定路径每次把一个 stage 的 32 个 bias word 全部清零。唯一的 push consumer 对图形路径只读 VS、FS 各前 16 个 word；compute 才读取自己的全部 32 个。此修改只减少 VS/FS 没有消费者的上半区初始化，其他 stage 保留原范围。没有新增缓存、状态预测或游戏判断，描述符完整 key、资源解析、依赖与 push 内容均保持原路径。

每次图形绑定少写 64 字节 CPU scratch。实际 NDK 28.2／API29／O3 代码用两个 32 字节 STP，取代原四个，并增加一次比较和条件分支；compute 仍执行四个 STP。热函数没有新增 BL，frame 仍为 6192 字节，函数 text 为 9680 字节，比原来多 4 字节。这个逻辑写入节省不能等同于 DRAM 流量或已测帧率收益。首次变量大小 memset 候选会增加 libc 调用，已在本机实验产物中保留并排除，没有交付该写法。

原官方 CPU 捕获中，绑定函数的 33 个 self 样本主要分布在常量 key／完整 key／句柄选择等路径，清零本身没有 sampled leaf PC。因此本改动是具有明确消费者边界的小幅工作量减少，不声称解决绑定热点。驱动的 113 个 self 样本单独归属绘制、描述符更新、Render Pass／command buffer 边界等接口，不能统称 Shader 执行时间。

使用原 GPU full async、serial、完整 key／A-B-A 碰撞／动态 UBO／边界测试，未增加重复实现语义的测试。另用原 `make_vulkan_fixture.py` 的 `DMSH_VK_STORAGE_BIAS=1` 生成自有 shader，完整 async 通过，原 storage alignment 检查确实命中 846 次 direct bias。官方 LVP、Khronos validation 与 synchronization validation 均启用。没有操作设备或运行游戏，真机净 CPU／功耗收益尚未测量。
