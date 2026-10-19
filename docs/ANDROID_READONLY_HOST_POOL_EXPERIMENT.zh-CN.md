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
