# 图像绑定枚举减负

此变更把反射中不变的 `kind 3/4/6` 分类移到最终 shader schema 构造阶段。
CPU source preflight、image preparation 和可选 replay readiness 依然逐次解析当前
handle、descriptor、epoch、数据可见性与依赖，只跳过与这三种绑定无关的元数据项。
不改变 SPIR-V、NVN 命令、GPU 命令或任何游戏逻辑，没有新增内容预测缓存。

不能直接复用 `image_read_slots`：它是原 store 前瞻的逻辑槽投影，包含 texel-buffer
及 texel-storage 类型，并非原 binding 索引。保留该协议，另加两个 64 位索引掩码。
ready 字段利用原对齐空隙，真实 Android NDK DWARF 的 `vk_shader` 从 744 增到 760 字节。
所有图像绑定占满 schema 时继续原连续扫描，避免只增加位枚举工作。
未构造的 mock、超过 128 项、descriptor count 异常或图像寄存器越界保留原扫描与错误顺序。
原 texel-storage、pixel-local alternative 在转换绑定后重新经过同一构造函数。

原 1a5280a 官方 CPU99 的 NvN 519 个用户态样本中，CPU source 枚举 7 个 self、
prepare 2 个 self，提供方向依据；不能把 9 个样本都算作可消除时间。
36a9781 的非 packaged prefilter 已减少 descriptor 快照，本变更只补剩余全表分类。
新增成本是一次有界冷分类、每 shader 16 字节和选中位的枚举，不承诺 FPS。

原 GPU pool-binding fixture 扩展了 mock、128 项、63/64 边界、clone 重建、数组 count、
无效 reg 与全图像/无图像情况：示例检查项 128→4，clone 128→2；实际 GPU sampled→
SSBO→compute RAW 与后续 CPU 改写在原扫描/掩码两模式均一致。
宿主 LVP / 官方 VVL+sync：owned 1030、完整 serial 22196697、pixel-local/shared DS
398488 checks 通过。完整 async 在原 readback pool 测试遇 Fill→Copy WAW；独立原
36a9781 同配置也复现相同错误，因此保留为基线问题，不标注 async 通过、不放松断言。

真实 NDK Android29 / O3 / TLSdesc 的原版和候选 shader TU 均编译成功。三入口及主
binding 函数栈大小不变，没有 iterator 外部调用。新增双路径令 prepare 静态代码变大，
CPU sources 代码变小；静态数量不等于动态指令或周期收益。完整冷 Android 资源任务
因生成环境/长任务未完成，本候选没有宣称新 APK 或新真机运行通过。
