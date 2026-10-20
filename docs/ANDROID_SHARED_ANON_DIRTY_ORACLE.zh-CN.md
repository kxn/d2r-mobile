# Shared 匿名页与原 dirty 周期：独立小对照

本分支基于原 owned-dirty fixture `e56be10`，只改测试。没有改变
`hle_svc.c`、guest heap、驱动或 NVN borrowed 策略，不执行游戏。

新增冷启动环境选项 `DM_VULKAN_HOST_IMPORT_SHARED_ANON=0|1`，默认仍为
private。它只选择该 fixture 自己 `mmap(NULL, ...)` 的匿名区类型；不改变
`vkMapMemory` 返回的驱动映射，不使用 MAP_FIXED、remap、PFN 查询或 GUP 重试。
每轮只导入 `2 * minImportedHostPointerAlignment`，另留一份对齐空间；原
LVP/Mali alignment=4096 时为导入8KiB、映射12KiB，不注册整堆。

保留原 `HOST_ALLOCATION_EXT`、两 buffer/同 memory 的非零 bind offset、
Flush/Invalidate、两次真实 queue submit 后再开 timeline gate/fence wait、
CPU→GPU CopyBuffer 和 GPU→CPU FillBuffer 全字节断言与析构后 CPU 可用性。
`DM_VULKAN_HOST_IMPORT_OWNED_DIRTY=1` 沿原 add/protect→prepare / direct fault /
prepare 周期，不放宽原 dirty 统计和 `nvn_dirty_since` 断言。private/shared
必须在独立进程中用同一驱动、memory type 和选项运行。旧 private 真机失败
记录保留；不能因为本机 LVP 两者都通过，就宣称旧失败已修。

## 有依据的假设与边界

已有固定 OEM family 5.10.236 源码（commit
`8f47a137616e17b8292ea6c8e1d3dbd40c3c783f`）中：

- `mm/mmap.c` 的 shared 匿名路径进入 `shmem_zero_setup`；private 路径不同。
- `mm/memory.c` 的 private anonymous 写保护路径可因页引用增加而复制；shared
  shmem 写保护故障另走共享页路径。
- 原 kbase r38p1 源码会 pin user pages 并保留 DMA/物理页映射。

这些只支持可验证假设，不证明所发 OEM 源码等于安装内核/驱动，也不证明
之前失败确为 COW/PFN 分裂。即使 shared 通过，也只建立这个 byte oracle 的
功能事实，不保证任意 VMA、fork、swap、madvise、长生命周期或整个游戏成立。

整个 guest AS 当前一次 private 保留5GiB，heap从1GiB起、容量4GiB，后续
SetHeapSize只改权限。不能直接换整个 AS 的 flag：shared 匿名会建立 shmem
对象，固定内核 `mm/shmem.c` 对未带 NORESERVE 的对象有全尺寸 commit 记账。
这不等于全量物理常驻，但4/5GiB的 commit、内存压力、增长/收缩和失败行为需
独立验证。不能静默加 NORESERVE 来绕过它（可能引入后续 SIGBUS 边界）。

成功也不免除 noncoherent Flush/Invalidate、所有在途 CPU/GPU 数据依赖、
owner/Finalize 寿命和 image dirty 的 N/N+1 发布规则；不能直接关闭全部脏页
或将任意 malloc 当成共享存储。当前 borrowed VMA/dirty exclusion 契约未改变。

## 离线运行与下一步

独立 host build、`LP_NUM_THREADS=2`、官方 Lavapipe/VVL+sync validation。
用 `DM_VULKAN_HOST_IMPORT_TEST=1` 提前进入原标准 API oracle，不初始化 guest。
对 `SHARED_ANON=0/1` × `OWNED_DIRTY=0/1` 各运行一次，保持显式 HOST dependency。

本轮没有 Android APK/JNI 的新 shared 选项，也没有手机测试。环境入口可由
后续独立 runner 在进程开始前设置；如果继续使用原 Activity fixture，需要
另审一次冷启动测试接线，不能假定新 Intent 已被支持。禁止先改活游戏 heap。
