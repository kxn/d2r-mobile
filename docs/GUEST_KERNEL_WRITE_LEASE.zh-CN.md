# 原宿主 kernel guest write 配对租约

隔离变更，基于冻结 b730ff4，不改变该树。它只补宿主边界，不能单独证明共享图像 NC cache 语义或真实游戏通过。

原 `dm_guest_write_prepare` 保持单向、用户态写前准备语义。新的 `dm_guest_write_begin/end` 专用于内核直接写客体缓冲：先调用一次冷注册的静态 hook pair 取得 opaque cookie，再调用原 prepare；调用方在实际 kernel write 完成后释放 cookie。未注册 hook 时只有缓存原子 pointer 读取/分支与原 prepare，没有 getenv、分配、额外线程或采样。注册是一次性原子发布，重复同一 pointer 幂等，不允许替换另一 pair；hook pair 必须 immutable/static lifetime。16B lease 单所有者，end 清空后调用对应释放函数，并保留原 errno。

生产接线只改原 `h_fs_ReadFile`，租约覆盖整个原始 `[buf,sz)`，跨 `dm_data_pread` 的 partial/EINTR 重试循环；正常、EOF、错误出口均释放。原 pread/holes 数据、返回值和输出参数的错误路径行为不变。空 handle 保持原立即失败，不创建租约。FS 结束 kernel write 后释放，后面的原诊断/日志行为不变。其它普通 memcpy、完成回调、ReadTexels 等仍沿原 prepare/fault 路径，不对所有 malloc 接入。新 API 不是 signal-safe，不能从缺页处理器调用。

宿主 hook 只提供配对生命周期。实际 gate 的 cookie 边界、pending GPU/fence/Invalidate、对新增 image scope 的覆盖、CPU fault/prep 握手由独立 dirty/Vulkan 实现负责；不得以 begin 时尚无 scope 而漏掉仍进行中的 kernel write。hook 必须在 shared cold 决策后、首次 guest IO 前注册。cookie==NULL 仅表示无相关租约，不能把 gate 分配失败冒充 untracked。

独立回归直接包含原 FS 与同一个生产 lease 实现，实际写临时文件并 pread 到普通/只读 mmap 页；控制 EINTR、短读与 partial 后错误，验证租约在整个循环内有效、所有出口无泄漏、原 errno 映射、空租约/重复 end、pair 拒绝替换。删除生产错误出口 end 的隔离源故障版本确实失败。没有 CPU 模拟、游戏模块、设备或自定义计时/采样。

额外成本不能隐去：固定 hook pointer 8B，ReadFile 局部 lease 16B；NDK28.2/clang19 API29 O3 中 ReadFile frame 从80B到96B。begin/helper 与有租约的 end 有自身调用帧；gate 的锁/等待/opaque cookie 成本另由其实现报告。未做全游戏性能或散热声明。
