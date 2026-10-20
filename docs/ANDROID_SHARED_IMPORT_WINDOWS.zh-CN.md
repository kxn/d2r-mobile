# 共享 Pool 的有限范围导入实验

本候选基于 `0b36faa`。0b 的真实 Q 首次四次 Buffer 请求分别只有
3968、24064、16、32 字节，却通过旧策略导入整个 400/710/2/1 MiB Pool。
这证明了策略粒度问题，不能据此证明所有后续访问都很小，或预言 RSS 收益。

## 冷启动与范围

默认构建不含实验：`DM_VULKAN_SHARED_IMPORT_WINDOWS=OFF`。
启用构建需同时选择 `DM_VULKAN_SHARED_POOLS=ON`。设备和原共享字节探针通过后，
原冷启动 `pool_window_bytes` 非零选择有限共享导入；显式 0 保持原整池对照。
窗口必须是二次幂，至少 4096 字节，并覆盖主机页、host-import、NC atom、UBO、
SSBO 和 texel offset 对齐。整池同步参考与此实验不兼容，冷启动明确拒绝；
运行中请求该参考会被拒绝，实际 getter 不改。无热路径环境变量读取。

启动时延用原 memfd guest-RO/RW-alias 探针，在两个导入同时存活时增加真实 GPU
双向字节检验：旧窄 handle 写→新宽 handle 读、新宽写→旧窄读，使用全局 memory
依赖。失败不伪称共享窗口可用。窗口模式只在后续所有 NC/CPU-observation 冷门也成功后才发布，失败保持完整 legacy 策略。这个冷探针不代表任意将来范围均可分配。

## Operation 与保活

沿用原 operation gather、identity、canonical span、epoch 与 retained reference。
缺失范围按固定窗口向外取整；一个完整绑定即使跨多个窗口，也只有一个连续
VkBuffer。全批先验证，重叠新范围合并，分配失败回滚本批私有节点。
已有且完整覆盖的节点保持不动；新增不相交节点不结束旧在途 reader。
未知枚举或预算溢出明确失败，不靠整池/镜像掩盖缺口。

旧节点相交时，原 maintenance 路径先结束并提交实际命令、等真实 GPU 与 CPU
完成、command reset，再 reclaim descriptor/typed BufferView 上下文；之后才重导入
整个相交连通区。它仍映射原 memfd 页，不复制内容、不造 TRANSFER writer、不改变
CPU authority、dirty 或 initialized bitmap。新的 primary 首部只为这次罕见重导入
加入一次跨旧/新 handle 的全局 MEMORY_WRITE→MEMORY_READ|WRITE 依赖。
新旧 Vulkan 导入会短暂同时存在，实际 GUP/driver 内存行为待真机验证。

Pool identity 自身被保活。finish/reclaim 后若还有 0b 的短 CPU observer 引用，
用原 condition variable 释放资源锁等待；closing/identity 变化会放掉自己的 pin 并
唤醒删除者。不在资源锁内等 GPU。未来 native-family 分支持有的长期纹理引用
不属于本候选，不能直接当短 observer 等待；必须另审后组合。

## 消费者

所有既有 draw/compute/transfer 完整范围仍在借用句柄前预留。B2T 另外将
retile 的完整 `texture.L.size` 和 storage-alignment prefix 与源一起收集；T2T
也先完成该预留，再取 image、temp、descriptor。ASTC 原协议要求目的已导入；
若已有部分/相邻窗口，先完整 union，再做原观察资格判断，不把任一 backing
当整范围证明，也不回退读取旧 CPU 编码。未导入的原不支持情况不借观察凭空 pin。

NC Flush/Invalidate、used pages、真实 writeback HOST barrier 均按实际 span/local
offset；完整 used/writeback 不允许跨洞，公开 Flush/Invalidate 可跳过从未导入的洞。
窗口边界及池尾保留 cache/bitmap 页对齐。整池参考保留原 cache/barrier 快路，
不新增逐范围资源锁。共享池不启用镜像迁移的 CPU-authority 表。

## 验收边界与成本

owned fixture 使用原真实 memfd heap/HLE 和 Vulkan API，覆盖稀疏、多绑定去重、
prefix、非零偏移、完整跨窗、错误/OOM 不发布、在途 reader、不复制 union、
CPU observer、typed-view 先销毁、相邻 span writeback、尾页、NVN compute、
B2T/T2T/T2B retile 与 GPU 像素/原 NVN 布局字节。ASTC 沿用原物理编码夹具，
无支持的 Lavapipe 明确报告 unsupported，不用纯逻辑检查冒充真实格式执行。

64 位 Pool 结构由 280 增至 288 字节；span 仍 88 字节，每个实际导入增加原
VkBuffer/VkDeviceMemory/driver 对象，不增加 guest/shadow 内容副本。维护会引入
稀有完成等待和双重映射短暂峰值；NC window 路径增加按 span 锁和 cache 调用。
未承诺 RSS、带宽、FPS 或热收益。Android 普通 UID、真实 Turnip/NC、真实 ASTC
及原游戏仍需独立验收，本报告不把 host 原创夹具当真实游戏结果。
