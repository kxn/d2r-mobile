# 只读 Buffer 窗口共享实验（默认关闭）

本实验只改变普通 Buffer 的 backing。它不运行游戏、不更改游戏地址、不调整 malloc，不承诺 optimal VkImage 零拷贝。当前仅完成宿主功能验证；不能把它描述为游戏提速结果或默认方案。

## 同步合同与证据边界

上游 `dm/gfx/nvn/nvn_mtl.m:439` 直接以 `newBufferWithBytesNoCopy`、Shared 模式包装池所在系统页；`mtl_addr` 返回该对象及偏移，没有逐提交的 CPU 内容快照。删除对象仍延迟到使用它的提交完成。这是可检查的上游实现事实，不等同于取得了 NVN 闭源规范。

[Apple 官方同步说明](https://developer.apple.com/documentation/metal/synchronizing-cpu-and-gpu-work)要求应用管理 CPU/GPU 对共享 Buffer 的访问，示例通过多缓冲与完成信号回收。共享物理页不会自动保留每次提交的旧字节。[Vulkan 外部内存说明](https://docs.vulkan.org/guide/latest/extensions/external.html)同样把内存共享和执行同步分开。

本次公开检索未取得 Nintendo/NVIDIA 正式 NVN 内存一致性规范；不把其他 NVIDIA API 文档或模拟器实现当成该规范。实验采用与上游 Shared 路径相同的可验证合同：GPU 读取前 CPU 发布，CPU 重写相同字节前等待相应 GPU 完成；同页不重叠字节可并行访问。它不承诺在 GPU 被刻意挡住时保留 CPU 随后非法竞争写入前的旧值。

原默认 Vulkan 镜像还提供了一部分更强的逐提交快照行为。原 `queued_upload_overlap_test`、同页邻居和 queued-effects 断言全部保留、默认路线继续通过。这里不删除它们，也不把其更强保证强加到 Metal 原始共享路径。宿主自己承诺的合法快照（如 known CopyBuffer 来源）则必须保留：来源是 imported 窗口时，禁用旧 shadow 的已知值捷径，回退原 GPU copy/readback。

## 范围与回退

- 冷启动 `DM_VULKAN_POOL_HOST_READONLY=1`，且显式启用 canonical windows，实际驱动公布标准 `VK_EXT_external_memory_host` 后才尝试。
- 只在新窗口、未初始化、页对齐且普通外部 Buffer 查询允许导入时尝试。CPU_NO_ACCESS 等旗标不充当“永不写”的证据。
- 单窗口最多 16MiB，存活和待销毁 imported 内存累计最多 32MiB；内存真正 `vkFreeMemory` 后才归还账本。压力下保留普通镜像，不等待预算。
- 保留原 guest 映射所有权，import 不经过 VMA；释放 Vulkan 对象不 `munmap` guest。
- 普通 writable GUP 是实际权限检查。已有图像/dirty tracker 可能把页保护成只读；失败只尝试一次，按 pool identity/window sticky 回退，不整体解保护、不提前 pin 整池、不换 ioctl 绕过。
- 完整 operation plan 的 writable 范围必须在借用 handle/view/descriptor 前转普通镜像；未分型的旧入口借用也保守转镜像。counter/event/ReadTexels 的 CPU 完成目的先转镜像，写回和效果元数据保留原实现。
- 首次写入转换先结束已有录制、等真实提交及资源退役，读取当前 CPU 权威字节建立镜像，然后更新 generation/registry/buffer epoch。只在首次用途变化发生，不在 fault handler 里等待。
- imported 只读 Buffer 不再扫描或复制冗余镜像；图像上传、Shader CPU 读取证明仍保留原 dirty 与依赖逻辑。GPU 可写共享尚未实现，不能顺便删 readback。

## 功能验证入口

复用原 `native_vulkan` 测试、原 NVN MemoryPool 注册及 canonical-window 操作规划，存储为测试自行 mmap 的有限页，不接游戏池。

```sh
LP_NUM_THREADS=2 VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json \
 DM_VULKAN_VALIDATE=1 DM_VULKAN_SYNC_VALIDATE=1 DM_VULKAN_ASYNC=1 \
 DM_VULKAN_POOL_WINDOW_BYTES=4096 DM_VULKAN_POOL_HOST_READONLY=1 \
 DM_POOL_HOST_READONLY_FIXTURE=1 ./vulkan_host_test
```

Android 普通 app UID 的显式入口：`run_game=false, run_vulkan_tests=true, pool_host_import_test=true, host_import_opt_in=true, async_submit=true`。与 `host_import_test` 标准接口模式互斥，provider 沿用已有 adreno/panvk。direct ICD 未装配验证层时必须明确 `vulkan_test_validation=false`，结果只能算字节/生命周期功能覆盖。

已验证：CPU→GPU 读取、fence 后 CPU 重写、同页不重叠 CPU/GPU 访问、写用途的预借用镜像转换、host event/counter 目的转换、imported 来源的已知值回退、queued 使用后销毁与原映射存活。该入口没有帧率计时、采样器或性能结论。

预算拒绝和合成分配失败注入已有独立功能检查，注入结果不能充当 KGSL 内核错误证据。剩余门槛：审查所有隐式 writer/CPU effect 入口、真实 KGSL 已保护页失败的具体错误和 sticky 路线，以及 Android 新 NVN 窗口 fixture。未完成前不接真实游戏、不默认开启。


## 拷贝计数与受保护页边界

`initial_mirror_bytes` 只累加实际执行 memcpy 的普通镜像段；imported 段为零，首次 CPU shadow 保留并独立记入 `initial_shadow_bytes`。只读→镜像交接的两份实际拷贝也分别记入 `mirror_bytes` / `shadow_bytes`。功能入口显式启用既有 copy accounting，不把这些逻辑字节称为 DRAM 流量。

额外功能案例仅对测试自行 mmap 的有限页调用原 `nvn_dirty_protect`，在首次 Buffer 使用前形成只读页；实际 writable host import 只尝试一次，失败则镜像，重复使用不重试。该测试既不访问游戏地址空间，也不整体解保护或改用其他 ioctl。驱动可以合法接受或拒绝这种映射；结果必须记录实际 VkResult/驱动诊断，不能把 Lavapipe 的接受推断成 KGSL 的接受。普通 NVN Finalize 保留其原有页保护清理。

无 validation layer 时，若驱动支持 `VK_EXT_debug_utils`，实验启用官方驱动诊断 callback 以保留原始错误原因；这不是 VUID/同步验证覆盖。若驱动不支持 debug-utils，不影响 Buffer fallback 逻辑，也不得伪造底层 errno。

Android fixture 使用原冷启动 window setter 明确选择4KiB，避免 Activity 普通16MiB默认值优先于环境变量；只改变测试模式，普通游戏默认不变。


成功 pin 后另有两条有限页功能路径：先等原 GPU Fence，再原 `nvn_dirty_protect`，分别通过原 `dm_guest_write_prepare` 和原 SIGSEGV dirty hook 合法写入同一字节，然后 GPU 读取原 imported VkBuffer。检查 handle/generation 不变、import attempt 不增加和 GPU 字节等于新 CPU 值。这验证该次实际缓存/映射行为，不读取 PFN，也不把任意 fork/COW/madvise/unmap 的稳定性概括为已经证明。已经受保护导致首次导入失败的页面仍不解保护重试。


## 非一致性 HOST_CACHED 受限接入（独立实验，默认关闭）

`DM_VULKAN_POOL_HOST_NONCOHERENT=1` 只扩展已显式启用的只读窗口实验；
原默认路径及仅启用 `DM_VULKAN_POOL_HOST_READONLY` 时的 coherent 要求不变。
仍优先选择 HOST_CACHED|HOST_COHERENT；额外选项才允许标准导入查询返回的
HOST_VISIBLE|HOST_CACHED 非 coherent 类型。不是修改属性或强行声明一致性。
独立 PanVK 驱动包及标准自有页字节验证不等于这一 NVN 池路径已在真机通过。

导入和长期映射都在发布 backing 前完成。非 coherent 的 allocation 全生命周期
保持 Vulkan mapped，释放前先 unmap；guest 映射仍归原调用方。只接受导入窗口
起址和长度都符合 host pointer alignment 与 `nonCoherentAtomSize` 的情况。
每次原提交边界的 `sync_used_run`，对该提交真正读取的 imported 页执行标准
`vkFlushMappedMemoryRanges`。范围按现有4KiB使用位图收集，再以独立 allocation
起点计算偏移并向 atom 边界取整；不能跨 backing、扩大到另一个 allocation 或
把 pool 的逻辑偏移当作 VkDeviceMemory 偏移。迁移提交跳过普通 upload sync，
因此其初始化页 GPU copy 来源也显式 flush；仅补普通提交会漏掉这条读取路径。

第一版每次刷新实际使用的页，不做 memcmp、mirror 上传，也不使用 dirty 代数
跳过刷新。原 `page_upload_stamps` 是镜像上传的证明，并非缓存清理完成证明；
直接重用会遗漏尚未保护时的普通 CPU 写。未来减少 flush 必须另建 per-host-page
证明，先保护再取用并与脏页发布的顺序配合，不能提前推进代数后漏掉并发写入。
现有图像 dirty 跟踪、原宿主写前准备与原缺页处理保持原行为。

GPU 仍只读，无 CPU invalidate 或共享 GPU 写入。CPU 覆盖 GPU 在读的字节前必须
等原 GPU 完成；非 coherent 时独立并发字节还必须落在不同 atom 内，因为
[标准 Flush 规范](https://docs.vulkan.org/refpages/latest/refpages/source/vkFlushMappedMemoryRanges.html)
将发生写入的整个 atom 纳入同步访问。此路径沿用上游 Shared NoCopy 合同，不能
提供旧镜像对非法同字节并发覆盖的逐提交快照。已知 CopyBuffer 快照捷径仍按
imported 来源回退原 GPU copy/readback。需要 GPU 写或 CPU 完成回写的操作仍在
借用物理 handle 前执行原 quiescent demote；共享 GPU 写入不在本实验范围。

不符合类型、对齐、预算、首次 GUP、bind 或 map 的窗口都在发布前 sticky 回退
普通镜像；不会解除已有 dirty 保护重试。提交边界 Flush 失败时原录制已借用了
handle，不能偷偷替换 backing 或继续提交旧数据，按原 Vulkan 错误路径停止。
同样不在 fault handler 中导入、分配、做 Vulkan 调用或等 GPU。

现有 copy accounting 新增 `host_flush_calls` / `host_flush_bytes`，只计实际标准
调用和逻辑范围，不是 DRAM 计数器、计时器或涨帧证据。额外成本包括一次导入
pin/map、存活期 pinned 页与每次所用页的缓存清理；初始 shadow 仍保留，并未声称
整池所有副本都已消除。没有实际 profile 之前不推断游戏收益。

Android 受限 fixture 追加 cold Intent `pool_host_noncoherent=true`，只允许同时
显式 `pool_host_import_test=true`；普通启动默认 false。它不加载真实游戏。
宿主同一原 fixture 可配合环境变量运行，输出实际 memory flags、coherent 与 atom；
Lavapipe 选到 coherent 内存时不能算非 coherent 路径通过。后续真机验收必须包括
先成功 pin、原 dirty protect、原 prepare/direct fault 合法 CPU 写、原 GPU 读，以及
跨 imported backing 迁移读；Mali 测试须另协调租约。


### ab9bcd0 的 Mali 首次非 coherent 池验证：失败，继续保持默认关闭

使用固定 PanVK USER_BUFFER 驱动 `bdd9df32` 与 APK `c20b2096`，普通 UID10174
实际选择 flags0xb、coherent=false、atom64。首次 GPU 读取正确，初始镜像复制为0、
shadow为4096字节；首轮标准 Flush 的原计数断言通过。在 GPU 完成后经原 dirty
保护、原 `dm_guest_write_prepare` 和32字节 CPU 改写，第二次 GPU 读取的原256字节
memcmp断言失败（测试原第76行）。未放宽断言、未重试 GUP；后续 direct-fault、
迁移与正常退役案例尚未执行，不可称为通过。游戏 APK 与267个原私有文件的哈希
已恢复，测试进程退出。设备不空跑。

这尚不能证明 mprotect 本身改变了 GPU 映射，也不能认定必须给普通 Vulkan upload
额外增加 HOST barrier。标准 QueueSubmit 对已发布的 HOST 写提供依赖；kbase 队列
源码也已有提交前 GPU 缓存失效。需继续用已有字节 fixture 核对每次实际 Flush、
修改字节及队列缓存处理，区分漏刷新、驱动隐式依赖和映射问题。原失败产物保留。


### 隐式 HOST 发布的独立标准接口对照

标准自有页 oracle 沿用 `07ec8d8` 的真实 memory type 查询、长期映射及非 coherent
Flush/Invalidate。默认保留原 HOST_WRITE→TRANSFER_READ barrier；单独 cold 环境
`DM_VULKAN_HOST_IMPORT_IMPLICIT_HOST=1` 或 Android 测试 Intent
`host_import_test=true, host_import_implicit_host=true` 只省掉这一冗余 barrier，
其余 transfer hazard、GPU→HOST barrier、timeline gate、Fence、生命周期和所有
原字节断言保持不变。独立 JNI 配置入口不改 `nativeTests` 的现有参数。

[Khronos 的 HOST write ordering 规范](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-submission-host-writes)
和[同步示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)说明：
已 Flush 的 HOST 写通过 QueueSubmit 获得到设备访问的依赖，普通 transfer 不必
额外补 HOST barrier。这个对照是检验驱动是否满足该合同，不是改变一般 API 语义。
同一原只读池 fixture 另记录已有逻辑 Flush 计数差值及首个不符字节，保留原 memcmp
断言；这些是有限页功能证据，不是新计时器或采样器。宿主 Lavapipe 官方 VVL/sync
两种标准模式各57,747检查、原只读池118检查通过，但实际均为 coherent 内存。
Mali 上的隐式依赖或 dirty 失败原因仍需另行验收，不能据宿主结果认定修复完成。
