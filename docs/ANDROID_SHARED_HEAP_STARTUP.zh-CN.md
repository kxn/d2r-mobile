# 早期 shared guest heap 候选（默认关闭）

此分支基于固定 shared Buffer 实验 b54c7127 / core 200bc，而非主线全功能验收。
它只把原 HLE 所有的 heap backing 在第一次发布 guest AS **之前**选为 shared anonymous；
不寻找 malloc 来源，不按游戏调用地址分类，不在 NVN Initialize 后重映射已有数据，
不移动或保护 driver 的 vkMapMemory 映射，不自动导入整个 heap。
线性 Buffer 的 flush/invalidate/依赖仍由原固定 shared Pool 后端负责；
image-only dirty 注册是另一个候选，本分支不会把 packaged image 暂缺的 freshness 契约伪装完成。

## 启动与回退

- 默认 `DM_GUEST_HEAP_SHARED=0`，原 private heap，不提前创建 Vulkan device。
- Android `game_thread` 在所有路径、日志、provider、Surface/window 选择完成后，
  `dm_android_main` / 装载 / SDK / 首个 SVC 之前执行一次 cold selection。
- 显式 `DM_GUEST_HEAP_SHARED=1` 且编译 `DM_VULKAN_SHARED_POOLS=1` 时才调用现有
  `vk_pool_shared_mode()`；复用同一个 `mtl_init()` / device / immutable probe。
  backend cold probe 必须仅在成功后才选择 whole/window0/mirror0/defer0；失败必须保留原 window16 等配置。
  该后端修正在独立 PanVK 候选中，不能以仅 b54 构建当作已组合验收。
  probe 不支持（含同步 owner 不可用）则在任何 guest 执行前冻结整次 legacy/private，
  不以 capability 不支持导致游戏 fatal，不允许首 Pool 再 promote。
- 该基础的 shared request 还要求 `DM_VULKAN_SHARED_POOLS=1`；本候选不新增 Java/UI 默认。
  probe 初始化需要已选 provider、Surface 和 runtime paths，不能移到早于 nativeStart 的 JNI setter。
  本分支只有 cold env 接口，未另接 Android Intent，不能当作已部署游戏可选项。
- probe 的小块 CPU/GPU 双向验证不证明未来所有 Pool、内存压力或 image 契约。
  一旦实际选择 shared，后续资源错误不允许偷偷按 Pool demote 成未同步的镜像。

`mem_init` 仍先取得原 5 GiB private PROT_NONE reservation；仅在这块**新建且尚未发布**的自有 reservation 内，
以 MAP_FIXED 原子组合其中 offset 1 GiB 的 4 GiB heap 为 MAP_SHARED|MAP_ANONYMOUS|PROT_NONE。
这是初始 AS 组成，不是 live guest remap 或 driver 固定地址实验。
原 alias 512 MiB、stack 256 MiB 继续 private；基址/区间和 QueryMemory/GetInfo 返回值不变。
并发首 SVC 经 pthread_once 只初始化一次，成功 release 后才发布可用 metadata。

## 资源与权限边界

不使用 MAP_NORESERVE。共享 shmem 可能在创建时计入 **4 GiB 虚拟承诺**；这不是 4 GiB RSS。
原 private heap 在 SetHeapSize 放开约 3.7 GiB 时也可能承受 commit 会计，不能把全部承诺当新增物理开销。
此实现优先保守承诺，不隐藏 NORESERVE 导致的后续 fault 失败风险；具体 Android overcommit/内存压力仍待独立真机验收。
即便 mmap / mprotect 成功，仍不保证后续缺页不会 OOM/SIGBUS，也不安装吞掉此错误的 handler。
shared shmem 的首次读缺页也可能建立实际页，不能套用 private anonymous 共享 zero page 的 RAM 假设；
后续 whole-Pool import 还可能 pin 其整个范围，远大于当前稀疏镜像的 initialized 字节。这些成本必须后续单独验收。
shared mmap 失败在发布任何 guest 指针前清理整块自有 reservation 并 fatal；不是 capability fallback。

SetHeapSize 保留原 cap 和应用池检查。权限增长失败返回原有失败值 0xD401，extent 不更新，不能假报成功。
shared shrink 若 mprotect 失败可能已部分更改多 VMA 权限，因此 fail closed，不继续假报旧 extent 可用。
成功 shrink 仍是原 mprotect(PROT_NONE)，不主动回收 backing 或变更原 guest allocator 语义。
上层必须遵守原 heap/Pool 生命周期；本 provenance API **不允许**并发缩堆释放正在使用的 Pool。

`dm_guest_shared_heap_contains(address, bytes)` 只对成功 shared 发布后、完整落当前 committed extent 的非零范围返回真，
使用 overflow-safe 差值比较；alias/stack、越界、legacy、尚未 commit 都为假。
它只是 backing metadata 快照，不是 allocation provenance、lifetime pin、dirty proof 或 CPU/GPU 可见性。
image dirty 调用者必须核**整页 envelope**，并在首次 GUP/import 之后、任何借用之前建立自己的生命周期协议。

## 验证和成本

`python3 scripts/dev/test-guest-heap-backing.py --output OUT --ndk NDK` 只在主机执行真实 memory SVC/VM 调用，
并交叉编译 ARM64 Android API29 夹具，不执行 ARM 代码，不运行游戏或 CPU 模拟。
夹具覆盖 8 并发首 GetInfo、真实 SetHeapSize/QueryMemory、shared/private heap fork 可见性、
alias/stack 保持 private、未开放/缩小页访问 SIGSEGV、越界/溢出、保护增长失败 extent 不变、
shared shrink/mmap 失败 fail closed，以及请求/支持四组合的整次启动回退。
期望信号仅属于自有子进程断言。

新增成本：冷启动多一次 probe（仅请求 ON；复用原 device）、shared heap 多一次初始 mmap；
首次内存初始化加 once/mutex，SVC 热检查为 acquire-ready load；SetHeapSize 增加 mutex/原子 extent 发布，
provenance 两次 acquire load 和固定范围判断。没有每帧扫描、计时器、新 sampler 或全 malloc hook。
这不表示已降低 RSS、DRAM 或热，也不是原游戏 shared 路径验收。
