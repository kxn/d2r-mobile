# 64 MiB 私有图像冷缓存预算候选

独立父版为 `88620ec7c92edeb59512612cebf42906a4848509`。生产只把现有
`private_image_budget` 静态默认值从 512 MiB 改为 64 MiB；父版保留作
512 MiB 对照，不改变主线或已安装日常包。没有新增启动选项、JNI、HUD、
缓存、追踪器、计时器、Shader、游戏逻辑、GPU 命令或等待。

已有930捕获中，CPU-rebuildable private-image allocation为120.945 MiB。
相对64 MiB预算，当前额外分配量的可减少范围为 **0–56.945 MiB**；这不是
可回收PSS、物理唯一DRAM或后续场景上界。资源是否足够冷尚未逐项观测。
VMA可能保留空block，driver也可能保留物理页，因此减少allocation账不代表
已经减少实际驻留。原捕获与本候选父版不同，不将旧数字当候选实测。

全部原资格不变：只在无recording/绑定scope的新guest-submit owner边界，
淘汰有完整CPU源、initialized、CPU-rebuildable、非external、真实GPU引用
完成且至少256个完成queue chunk未用的私有image。native import与GPU写结果
不作为可重建缓存删除。原dirty、CPUpublication、别名、物理recipe失效及
生命周期保持，逻辑descriptor保留；再用时由原路径重新创建、读取当前源并
上传完整mips/layers。不得凭guest指针回收数据。

原维护器每轮最多扫描32个、删除4个image。`freed < 8 MiB` 是循环前检查，
随后删除完整image，所以单图可以超过8 MiB；不是严格释放上限，也不是硬
resident cap。内部submission重开仍不维护。

预期代价是冷图再用时的Vulkan分配、CPU deswizzle/BC decode、staging和GPU
上传带宽，可能增加绑定/视图重建。需要分别比较allocation/released、VMA
BlockBytes以及官方showmap/dumpsys/mtrack，不能相加或预报净收益。

原预算夹具会把预算临时覆盖为0以验证真实淘汰、像素重建和pending拒绝，
因此本候选只在夹具首次覆盖前增加原stats的64 MiB实际默认值断言和输出。
新增观测明确排除APP；保留原数值/同步断言、原显式恢复512 MiB的测试清理。
这是主机GPU内容资格，不能替代Android原游戏、实际驻留或6GB验收。
