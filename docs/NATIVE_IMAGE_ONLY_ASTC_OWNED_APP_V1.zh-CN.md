# 原 ASTC native-family 的 image-only 普通 UID 验证

基于冻结 bcbd751，复用 0b1993e 原独立 Activity/JNI、真实AA185资源与原九mip native/reference GPU oracle；不改生产代码、不重烘纹理、无新渲染器。

原 reference B2T 和 VBO 都在不同的逻辑 range。原 native ReadTexels 与全部九mip FS 采样之后，新增精确 UNBACKED/materialized=false 断言；在仍无该 range 的 Buffer 时做原 NVN Invalidate、同值 prepare+payload 重发、Flush，再检查原块/像素。随后调用既有 pool_window_read 实际 VkBuffer copy 全135168B，检查原 payload，确认该 range 此时才 SINGLE/materialized。原两次 union、tail5、线性/GPUwriter、CPU恢复、TRANSFER/COMPUTE previouswriter changed FS/CS/T2B 和最后退休所有断言保留。

该新门只证明 native marker 真进入新 image-only 分支，不把只跑RGBA或059mixed当实际ASTC。AA185真实类型可 coherent；不据此声称实际非coherent调用。普通UID与首次PID/精确driver路径dev/inode/SHA/maps、actualASTC、三UUID、无游戏模块和原包只读/清理门保持。新 node metadata 是184B，不沿用旧120B。

原 same-value CPU 生产如触发实际原保护则保留失败，不关闭 authority/dirty/alias gate。只作为实验验收，实际游戏净RAM/热/FPS另测。
