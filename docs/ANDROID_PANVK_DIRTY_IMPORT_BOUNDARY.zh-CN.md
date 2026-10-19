# PanVK USER_BUFFER 与原脏页保护的边界（实验，默认关闭）

本记录对应源代码 `e56be10fdfe1ef43daa8ddc21b223e6a3a5edaae`，不修改生产路径。
自有页实验没有执行游戏模块、导入游戏地址或改变主线驱动。失败断言未放宽。

## 真机结果

普通 Android app UID、Mali-G710、同一 PanVK 驱动 SHA256
`bdd9df32c38f7c82fb58d815d6a021e8641a545f9fdb1b401416d6908d265d5d`，
实际 type 2 / flags `0xb`，HOST_CACHED、非 coherent、atom 64、页 4096。
直接 ICD 的 Android validation layers 未启用，不能把错误计数零当作 VVL 验收。

| 自有页控制 | 结果 |
| --- | --- |
| 普通 RW 匿名页标准导入，不调用 dirty/mprotect，显式 HOST barrier | 此前 57,820 检查通过 |
| 相同控制，仅依赖合法 QueueSubmit 隐式 HOST domain 操作 | 此前 57,820 检查通过 |
| 标准导入成功后，原 dirty protect → 原 write_prepare → CPU 改写，隐式 HOST | round 0 / pass 0 首次 GPU source 读取失败：CPU 首字节 7，GPU 回读 0 |
| NVN readonly 首次正确读取后，原 dirty protect → prepare → CPU 改写 | CPU 166，shadow 145，原有 GPU copy 的原始回读 145，原 completion 输出 145 |

最后一项 byte 31 也是 CPU 166、shadow/GPU/out 36；原 GPU 回读与 completion 输出完整
256 字节相同。原实际范围执行 Flush 1 次 / 4096 字节。这是现有逻辑计数，不是独立
驱动调用跟踪或总线计数器。没有新增 GPU copy、Barrier 或等待来改变这个实验。

标准控制不包含 NVN 池或 shadow，而且在首次 GPU source 读取前就失败，故 NVN
回读时直接返回旧 shadow 不能解释这组结果。修改当前 CPU VA 的 cache 后仍然读旧字节，
与匿名页 COW 后 GPU 保留旧 pin 的解释一致；**没有读取实际 PFN，尚不能称为物理别名分离已证明**。
后续 direct-fault、迁移和 retirement 断言因原失败未到达，不能宣称通过。

宿主 Lavapipe + 官方 VVL/sync 的两种 owned-dirty 模式各 57,772、readonly 124 检查通过。
宿主是 coherent `0xf`，不能代替这条 Mali 非 coherent 路径。

原 81e8529 APK SHA256 `5d0de0cb0a48275b5037cdaff64f659574cc2ff826669f68659b11e7e07021d2`
及 267 个原私有文件已逐项恢复，fixture/日志/derived 文件也恢复，进程为空。
modules、游戏资源与存档未改动。控制器完成两次控制并成功恢复，不表示功能检查成功。

## 官方源码支持的候选原因

以下是公开的对应设备家族源码，**没有证明它与手机已安装内核二进制逐字一致**。

* [OnePlus 内核](https://github.com/OnePlusOSS/android_kernel_5.10_oneplus_mt6983/tree/8f47a137616e17b8292ea6c8e1d3dbd40c3c783f)，固定 `8f47a137616e17b8292ea6c8e1d3dbd40c3c783f`，Makefile 5.10.236。
* [OnePlus kbase](https://github.com/OnePlusOSS/android_kernel_modules_oneplus_mt6983/tree/63502090421af828a2f44715ff20ea116c24b28f)，固定 `63502090421af828a2f44715ff20ea116c24b28f`，下列文件位于
  `vendor/mediatek/kernel_modules/gpu/gpu_mali/mali_valhall/mali-r38p1/drivers/gpu/arm/midgard/`。

完整调用链：

1. `mali_kbase_mem.c:4937–4947`：CPU_WR 或 GPU_WR 决定 FOLL_WRITE，5.9 及以后调用
   `pin_user_pages_remote`，把真实 page 数组保留给导入对象。
2. `mm/gup.c:2938–2949`：该 API 添加 FOLL_PIN；同文件 101–154 描述/实现 pin 引用计数偏置。
3. `mm/mmap.c:99` 的 private writable protection map 指向 `__P011`；
   `arch/arm64/include/asm/pgtable-prot.h:94` 定义为 `PAGE_READONLY`，不设置 PTE_WRITE。
4. `mm/mprotect.c:511–517` 通过 `vma_set_page_prot` / `change_protection` 更新页表。
   私有页即使 VMA 恢复 VM_WRITE，PTE 仍需要首次写故障完成可写/复用判断。
5. `mm/memory.c:3868–3898`：普通匿名页 `page_count != 1` 走 `wp_page_copy`。
   retained pin 可使计数不为 1。该结论受实际内核版本、页类型和厂商 hook 影响。
6. `mali_kbase_mem.c:5014–5023`：GPU 映射使用之前保存的 pages 的 DMA/物理地址。
   没有证据证明当前安装的 USER_BUFFER 实现会跟踪此后 CPU PTE 的 COW 并重绑 GPU。

如果 CPU 的同一虚拟地址换成新物理页，对该 VA 执行正确 cache maintenance 仍不能更新
GPU 原先固定的旧物理页；这解释了为什么额外 Flush 不一定能修复。逐 Flush 拆 GPU PTE、
重新 pin 或全局等待既没有获得正确性证明，也不作为解决方案。

[Qualcomm 官方 KGSL 参考](https://github.com/qualcomm-linux/kgsl/blob/2b2d762c0b843c560d2ae2203f6b3cfe1474fb3f/kgsl.c#L2986)
`memdesc_sg_virt` 则调用 `get_user_pages(FOLL_WRITE)`，为 FOLL_GET 引用；清理使用 put_page。
FOLL_GET 也增加引用计数，所以不能把高通真机通过简单归因于 FOLL_PIN/FOLL_GET 区别。
尚未证明这个公开 KGSL commit 就是实测 Vivo 的内核来源；高通已 pin 后两种 dirty 周期通过
只是那台设备/该字节场景的实际结果，不是所有内核上 COW 行为相同的证明。

`BASE_MEM_IMPORT_SHARED` 在 kbase 头文件里指 CPU/GPU VA zone，不能当作 Linux CPU VMA
的 MAP_SHARED。标准 oracle 的 CPU 分配明确为 MAP_PRIVATE | MAP_ANONYMOUS。

## 不再保护有限只读 imported 页：可审查方案，尚未实现

现有 `nvn_dirty` 没有 volatile/excluded 子范围接口。`remove_range` 是整池销毁操作，不能
用于部分页：它会失去纹理增量上传，且需要正确处理既有 range proof。仅在 `protect`
里跳过若干页也不正确：`dirty_since` 会错误返回 clean，缓存还能无限复用旧证明。

第一版只允许有上限的 GPU-readonly buffer 页，保持默认关闭，不扩展到共享 GPU 写：

* 使用页级、持有到 import 真正退休的排除凭证。首次 import 前只允许原状态为可写 PG_NONE
  的页进入排除状态；与原 protect 的 PG_BUSY/CAS 协调，使检查和 pin 间不能被重新保护。
  已受保护的页拒绝，禁止解保护后重试 GUP。多 import 重叠要有引用计数或直接拒绝。
  同一 CPU VA 可能由多个 NVN pool/range 登记：排除必须覆盖所有页别名，或拒绝歧义/重叠
  登记；仅在 find() 返回的第一个 slot 放标记，其他 slot 仍可能保护同一 VA，不正确。
* 排除页保持可写，protect 不改变其 VMA；dirty_since 对交集永远返回 dirty，visit 必须访问
  全部交集。进入和退出均发布 dirty/proof 失效。不能靠一次 generation 增量表示以后任意 CPU 写。
* `nvn_dirty_range_proof` 当前只有 slot/lifetime/changes，没有原查询范围。局部排除时旧 token
  无法判断是否与排除页相交。保守第一版应让含排除页的 slot 的 refresh/observe 返回 unknown，
  或另行设计带范围的 token；不能返回有效但不再变化的 changes。
* texture 的全局 epoch 快捷命中发生在 dirty_since 前。**所有图像访问必须在任何 image 借用、
  upload 早退和 proof 命中前，把重叠 imported 页 quiescent demote**，发布 epoch 变化，释放
  pin 后恢复原 dirty 保护流程，并对该窗口 sticky 禁止再导入。这样同池其他纹理仍可缓存，
  不应把整池视为永久 dirty 导致每 draw 上传纹理。原候选仅覆盖 writer/CPU 目的地，未覆盖所有
  sampled image 的首次访问，不能直接复用其 admission 当作已经完整。
* dirty-generation 支持的 CPU/shadow/常量证明对排除页返回 unknown；确实只需当前 CPU 值时
  可以读 live guest，但仍核对 pending GPU writer、NVN ordering、变体完整键和边界。
  `vk_pool_try_read` 目前读 live guest；shader specialization/predicate/zero-draw/bloom 都通过此入口。
  UBO `constant_copies` 是 submission 内 GPU snapshot，以 V.buffer_epoch 为键，不能误以为是
  dirty-generation 缓存；必须单独保留原 snapshot/写失效契约。
* GPU writer、CPU completion effect、图像用途或释放之前，先按原 quiescent 路径退休全部借用，
  然后解除排除并恢复 mirror/dirty。不得在 fault handler 调 Vulkan、分配或等待 GPU。
* 非 coherent 仍需全生命周期 map，在原 QueueSubmit 前对实际使用范围按 atom 对齐 Flush；
  不因排除保护或 CPU authority 证明省略 Flush。失败保留原 sticky mirror 回退。

这只能在 CPU allocation/VA/物理页持续有效、没有其他 remap/COW 操作的受限条件下工作；
不能当作已经满足任意应用 Vulkan EXT 外部内存全部契约。需要内核 notifier 等能力时应明确
内核责任，不能用用户态缓存 API冒充物理映射跟踪。目前不修改通用 dirty 或主线，也不重新
占用设备。

## 本地证据

* `.local/logs/panvk-owned-dirty-oracle-v1-actual-validation.json`
* `.local/logs/panvk-owned-dirty-oracle-v1-functional/` 原日志、失败断言、事务与恢复核对
* `.local/logs/panvk-host-import-sources-v1/` 固定 primary sources 与 SHA256

无 FPS、带宽或发热收益结论；该方案修复前不应默认启用。
