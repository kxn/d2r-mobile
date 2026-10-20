# Native image 与共享 Buffer 窗口（默认关闭）

基于 `eddb4ca`，接入 `30854d3d` 的版本化 native family；不改变游戏二进制，不改已有 driver。

每个 native VkImage 独立导入它的 payload 对应 memfd 页。范围按真实 host-import、VkImage memory requirements、nonCoherentAtomSize 对齐，必须完整落在 Pool 和已提交 heap 内；内存类型取 host pointer / 临时 Buffer / VkImage requirements 的交集。成功绑定后销毁用于查询和导入的临时 VkBuffer，仅保留原物理页的 VkDeviceMemory 映射。VkImage 一生的 memory/offset 固定，Buffer union 不访问或释放这份 allocation。没有分配、扫描或复制另一份纹理内容。

native 的长期 Pool 引用仍使用独立 `native_image_references`，不进入 union 等待的短 `pool.references`。后者仍覆盖 operation、GPU submission、CPU observation 和 completion effects。图像原物理退休先销毁 VkImage、释放写 scope，随后 token 发布可回收状态；owner 在原退休/Pool close 边界回收，completion callback 不重入资源锁。Pool close 保留原 finish→destroy images→等待短引用→释放两类存储顺序。

本版保守保留 active Buffer 窗口覆盖完整 native payload。shader 的 sampled/compute image、copy source/destination、ReadTexels 和 ghost 使用原 operation 规划，在物理借用前按精确 table/完整 descriptor 收集范围。原 buffer-only mask 不变，image 使用既有 immutable image iterator。每次实际访问仍进入原 pool access/WB/hazard 引擎；NC 对实际 image token 和 active Buffer imports 分别维护缓存，image writer→HOST 使用覆盖 alias 的 memory barrier。原外部 GENERAL/FOREIGN 发布、scope/prewrite gate、fence/Invalidate 均保留。tail 恢复原 NVN layout，不走 native allocation，也不会把 opaque full payload 送入 Tegra deswizzle/retile。

成本：每张图新增一个有限 import/map/token；active Buffer 对同页的 import bookkeeping 暂时重复。它们共享物理页，不能把两份 VA/RSS 当作两份物理内容。token 数量、import 字节和 `sizeof(token)×count` 元数据分别提供查询；NC cache 对两个 import 的维护和 native lane 遍历也有额外 CPU 成本。本版不是最终去掉全部重复 GUP 的版本，更没有实测 FPS/热收益。

宿主验证使用 LVP、官方 VVL/SYNC、LP_NUM_THREADS=2。有限新 RGBA image 的 GPU oracle 验证两次 union、不可变绑定、图像写→Buffer 读、Buffer 恢复 opaque 字节→图像像素、分配失败清理、最后 release 回收；这不代表 ASTC producer/FOREIGN 合同。原 full/tail ASTC 夹具新增真实 memfd heap、两次 union 与每 mip 块/采样对照，LVP 不支持 ASTC，因此待实际 host-image driver 与普通 UID 执行，不能标为已通过。原窗口和完整 legacy async/serial 回归另行保存。此阶段没有 Android 构建或设备操作。
