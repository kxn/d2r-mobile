# 原生图像首次使用的通道前 acquire

本候选基于 bb4ac54。原生资源仍默认关闭，仍在每个 command chunk 末尾归还 FOREIGN。不改变 CPU 发布、原生表、驱动身份、Pool import、GPU/CPU gate 或退休合同。冷 image_get 原有的通道结束保留。本项只避免已经创建的原生贴图在新 chunk 首次片元采样时，不必要地结束一个尚在 secondary 中录制的通道。

只有以下确定性负证明全部成立，才复用 `vk_render_pending_primary()` 将 acquire 放到该通道之前：消费者为片元 sampled read；自身在该通道尚未使用；无待处理 mip-clear/NVN order/reference/未知 Buffer producer；payload 的完整宿主页 envelope 与本次 recording 的任何 Pool GPU access、保留的 Buffer writer 区间、未完成 CPU effects/writeback 均不相交；原 GPU image alias 索引无交叠 writer；原生物理范围索引无本通道已使用的别名。此版比只排除写入更保守，连先前同页 Buffer 读取也拒绝。无法证明时完整保留旧 split。不会凭文件来源、hash 或 dirty stamp 推断不变，也不遍历全部资源表预先 acquire。

已知消费者的 acquire 目的范围收窄为 `FRAGMENT_SHADER/SHADER_READ`。虽然规范忽略 acquire 的 source scope，当前 AA185 Mesa 会消费传入的 source mask；本项保留旧 ALL_COMMANDS source，避免连旧 release/acquire 实现顺序一起改变。释放端依然沿旧流程，后续可独立研究按真实 image/Buffer writer 合并 release，但不能遗漏 TRANSFER 写或把 sampled 标记当作只读权威。

工作量收益是命中时少一个真实 Render Pass 结束及下一次重启，同时减少 acquire 的目的 cache/stage 范围。新增成本只在每 chunk 某图像第一次 acquire：原 Pool/CPU-publication/Buffer interval 与 image alias 元数据查询；没有内容副本、新预测缓存、计时器或游戏场景分支。没有测量 GPU 时间、DRAM、FPS 或热收益。

专用 host GPU 夹具使用真实共享堆和有限 imported RGBA VkImage，原 NVN draw 产生真实 pending pass：独立情形保留通道；真实 UBO 别名读取、真实 aliased attachment writer、显式 order 各自保留旧 split；随后标准 image→buffer 转移核红像素，并核原 draw 目标。测试 entry 只为了复用相同 acquire 规划代码，不绕过生产 ASTC 表验证，不宣称它是 ASTC native payload 或片元采样 native 的验收。首次测试曾因未启用 FOREIGN 扩展被 VVL 拒绝，原日志保留；仅专用测试冷入口检查并启用真实支持后重测。普通代码入口不因此开启新扩展。真实 AA185 ASTC 及原游戏必须单独验收，LVP RGBA 不能代替。
