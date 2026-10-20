# 原 ASTC native-family 的 image-only 普通 UID 验证

基于冻结 bcbd751，复用 0b1993e 原独立 Activity/JNI、真实f830资源与原九mip native/reference GPU oracle；不改生产代码、不重烘纹理、无新渲染器。

原 reference B2T 和 VBO 都在不同的逻辑 range。原 native ReadTexels 与全部九mip FS 采样之后，新增精确 UNBACKED/materialized=false 断言；在仍无该 range 的 Buffer 时做原 NVN Invalidate、同值 prepare+payload 重发、Flush，再检查原块/像素。随后调用既有 pool_window_read 实际 VkBuffer copy 全94208B，检查原 payload，确认该 range 此时才 SINGLE/materialized。原两次 union、tail5、线性/GPUwriter、CPU恢复、TRANSFER/COMPUTE previouswriter changed FS/CS/T2B 和最后退休所有断言保留。

该新门只证明 native marker 真进入新 image-only 分支，不把只跑RGBA或059mixed当实际ASTC。f830真实类型可 coherent；不据此声称实际非coherent调用。普通UID与首次PID/精确driver路径dev/inode/SHA/maps、actualASTC、三UUID、无游戏模块和原包只读/清理门保持。新 node metadata 是184B，不沿用旧120B。

原 same-value CPU 生产如触发实际原保护则保留失败，不关闭 authority/dirty/alias gate。只作为实验验收，实际游戏净RAM/热/FPS另测。

## v2：区分 Pool 级 UNBACKED 与精确无 Buffer 交集

6febdbf 的首次 AA 验证失败于 line168 的 UNBACKED 断言，原日志/结果保留。公开 vk_pool_span_query 仅当整个 Pool 的 backings 为空才返回 UNBACKED；同 Pool 先有 reference B2T 和 VBO 后，即使 native payload 全是空洞也只返回 CROSSING。因此该断言必不能表达本测试意图，实际旧日志未记录返回值/交集，不能归因为生产物化 bug。

v2 只增加 DM_VULKAN_TEST_EMBED 下的布尔物理交集 oracle，invalid=-1/none=0/any=1，不测字节/计时，不改生产逻辑。原两个同一 CHECK 改为 CROSSING 且精确交集0，materialized=false 保留，36新增检查和447总检查不变。宿主 lane 先用原 helper 真实写同Pool不相交 Buffer，再用 native-only GPU 像素证明查询CROSSING但交集0；晚实际 Buffer 后另核更宽 range 仍CROSSING/materializedfalse但交集1，防将任何partial Buffer误判为没有Buffer。不是仅把断言放宽成非SINGLE。

后继 query v2 的 LVP/LP2 校验：同池已有真实无关 Buffer 时，native 范围为 CROSSING 且 intersection=0；真实部分覆盖为 CROSSING、materialized=false、intersection=1，负对照仍拒绝。async/serial lane 各10876、原 pending 各13937、native-only writer fallback 各13937、window5761/5759、mixed 各4100，均无 VUID/SYNC；LVP 不覆盖本次 AA ASTC 实机分支。原6febdbf失败记录保留，不能以此次测试修订宣称已修生产物化或已降 RAM。
