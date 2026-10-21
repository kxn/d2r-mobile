# Operation-owned Buffer geometry packet（宿主候选）

本候选基于 `6b12d7f167561104999248eff2bb31c191bc03db`，另独立 cherry-pick 已审 `fa18943994d9bcf3831fb47f5cab6434e66bc89a` 的四处零长度 memcpy 保护（本分支 `71e1cfe`），它不是 packet 机制。只针对已有 window operation 准入和随后的 Buffer 消费重复解析，不改变 shader ABI、游戏模块或资源格式。未构建 Android canonical/NSO，未部署或测得 Android CPU/FPS 收益。

## 保留的合同

原 `pool_operation_pin()` 在 `V.resources` 锁内为每个精确需求解析 Pool/window，并独立增加 Pool 引用。本候选把该次成功解析的 owner 索引、backing、物理 offset 和 generation 保存在本 operation；只给普通 Buffer 需求保存几何，native-only image 需求仍不隐式创建 Buffer。成功准入后保存 registry epoch。

`pool_address()` 仍先执行 mip clear/shared linear alias 检查，再以当前 recording 的 registry epoch 查最内层 operation 的精确 `(address,size)`。只有完全相同范围可命中，不沿父 operation 搜索。registry 变化、哈希碰撞、未列举范围或子范围全部返回原路径。Pool 独立 pin 保证内部 commit/completion/reopen 后 pointer 存活；它不等于旧 recording 的访问 ledger。新 recording 首次访问仍在资源锁内校验 epoch、初始化原 CPU 元数据并调用原 `record_pool_use()`。

原 CPU dirty/upload、range/stage/initial-contents、native alias、hazard、writeback 和每 submission 页集合继续执行。没有缓存 writer epoch、ready/visibility 或 GPU 查询结果。`buffer_recording_owner()` 的地址复用歧义检查与 named dependency 保持原样，不能直接用当前 owner 替代。原 operation pin/unpin 与 `pool_cv` 广播未改。

## 成本和适用边界

每 range 增加 32 字节几何；每个曾使用的 depth 增加固定 256 槽、2 KiB direct-map。scratch 按高水位惰性增长，最多 4096 range × 8 depth，几何上限 1 MiB、槽表 16 KiB，另少量 plan 字段和分配器元数据；原 ranges/pins 不重复复制。每次准入仍保存每个普通 Buffer 的几何并写一个哈希槽。32 位 stamp 避免每 draw 清 2 KiB，只有回绕时清表。

消费增加一次哈希及精确/epoch/extent/generation 校验，减少再次判断多个 Pool 区间及解析同一 window。当前 recording 已有 owner lookup 时仍最多遍历原 8 个 owner 槽以取得该 recording 自己的 use。未命中多付一次有限探测，然后原路径照常执行。它不消除 CPU 脏页工作、GPU barrier 或整体 operation 收集。当前 profile 的 inclusive 父函数百分比不能当作可回收时间；真实净 CPU 成本需后续现成 Simpleperf/Perfetto 验证。

## 验证范围

新 HOST-only 用例使用原 NVN Pool/operation/实际 VkBuffer copy/fill/完成机制，覆盖两个窗口 offset、精确/零/子范围、CPU 改写后重新提交、GPU-only 内容、独立 pin 跨完成和 reopen、8 层嵌套、direct-map 碰撞、registry 变化及同地址删除/重新注册。原 production policy 严格 oracle 覆盖 graphics/compute/UBO/SSBO/typed-texel/VBO/index/ghost 与显式 promotion。软件 Lavapipe Core/SYNC 与 sanitizer 结果、原始失败和负控见独立 source-ready 报告。

测试 getter/实际消费计数只在 `DM_VULKAN_TEST_EMBED && !DM_ANDROID_APP` 编译；APP 预处理核查排除了两者。计数只验证功能路径使用，不作性能测量。

组合 UBSan policy 在原 HLE `nvnCommandBufferBindVertexAttribState(0,NULL)` 的零长度 memcpy nonnull 合同处中止（`hle_nvn.c:842`），初次失败原样保留。随后纳入上面的独立已审 HLE 保护，以完整 ASan+UBSan 重建并复测原 oracle；最终 source-ready 仅据真实新结果记录资格，不把原失败改为成功。

尚缺 Android 实际构建/栈和真实游戏功能/CPU 对照。本次没有执行真实游戏模块，也不将软件 GPU oracle 当作 Android 原生游戏进展。
