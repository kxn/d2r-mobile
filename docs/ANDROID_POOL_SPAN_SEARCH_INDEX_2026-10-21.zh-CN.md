# 受锁保护的 Pool 分块查找索引

## 当前 Profile 与范围

精确 `716cd6c` 的官方 Simpleperf 记录与 LLVM 源码归属见 `.local/logs/native-candidate-retirement-716-Q735-v1-analysis/native/official-full-samples-callers.json`。NvN 用户态共 636 个样本，其中 `pool_span_resolve` 自身 17 个，16 个的实际调用点来自 `pool_operation_pin` 内的 `pool_span_resolve_operation` 最终回退。该调用点已经排除了单块 Pool，并尝试了原单块提示与当前 submission 的八项提示，之后仍会遍历分块链。不能把这些样本全部当作可消除成本，也没有据此推算毫秒或 FPS。

## 改动与有效条件

仅在已经持有 `V.resources` 的查找中，提示未命中时使用按 `begin` 排序的分块指针数组，二分寻找包含地址的候选。原链仍负责物理资源所有权，原单项与八项提示仍先执行；单块及零长度查找走原路径。没有扩大任何范围、连接相邻 VkBuffer 或跳过原边界、句柄与 generation 检查。

索引只描述物理拓扑。追加、普通 quiescent migration、共享 union 和首次物化都会按原逻辑推进 `backing_generation`；下一次受锁查询才重新建立数组。普通 migration 重新发布前已核对旧 generation，因此不会把并发追加后的新索引误认为仍有效。物理链销毁清理索引，Pool 同地址重新注册是新对象。所有索引访问、重建和清理都在原资源锁内，未加锁的 retained reader 完全保留原解析方式。

每个 Pool 增加 24 字节元数据，指针数组按需增长，最多 4096 项（64 位下 32 KiB）；256 个节点需要 2 KiB。数组随 Pool 释放。超过上限、拓扑不合条件或分配失败时，沿用原遍历；同一 generation 不会每次绘制重试分配。新增成本是受锁慢路径的版本比较和二分访问，以及拓扑发生变化后的两次链遍历与偶尔扩容。稳定多块拓扑下，将提示未命中后的线性查找改为对数查找；小链或频繁改变拓扑时，不能假定净收益。

初始化、CPU dirty、GPU writer、实际 Buffer 消费者、上传、写回、资源 pin、hazard 与完成状态均不由该索引证明，也没有增加锁或等待。

## 验证边界

原 C NVN/Vulkan 后端在 x86 Linux/Lavapipe 运行，未执行游戏模块。正常与 ASan/UBSan 构建均在 Core/SYNC validation 下，以异步及串行模式通过以下四组：新索引与原解析器的逐范围对照、原提示/migration 字节用例、原 operation geometry 生命周期用例、原图形/计算/UBO/SSBO/VBO/index/ghost production-policy 用例，共 16 组。

新用例由原 Pool API 创建 24 个真实 Vulkan 分块，核对空洞、相邻节点、尾端、溢出、追加后的重建、索引元数据分配失败回退，以及实际 GPU 填充/读取的字节；还覆盖同地址删除重建与原零长度尾端语义。测试专用 oracle 和分配失败开关均排除于 Android APP。首次测试构建的 reserve 参数数量错误已修正，原失败日志保留，没有改生产逻辑或测试断言。

尚未 Android 构建、真机性能或持续散热验收。该候选不能宣称解决 6GB 平板超时，也不能将局部查找工作减少写成整帧收益。
