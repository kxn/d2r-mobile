# 自有页只读导入与脏页保护排除组合实验

默认关闭，仅开发测试入口 `run_vulkan_tests=true`、`host_import_test=true`、`host_import_owned_readonly_exclusion=true` 可选择。本实验从 e56be10 原标准 HOST import 字节验证器追加独立分支，原 `host_import_round` 和失败断言保持不变；dirty API 仅复用 fe0fa31 的 `nvn_dirty.c/.h`，不接入生产 NVN 池。

每轮自有匿名页登记后，在仍可写时取得独占 exclusion，才进行标准 HOST_ALLOCATION 导入。只创建 TRANSFER_SRC imported buffer；GPU 不写 imported 页。保持 Vulkan memory 全生命周期 map；非 coherent 类型按原 atom 对齐 Flush，每次 CPU publication 使用原两 pending submission/timeline/fence 协议，并逐字节核对独立 GPU copy readback。采用合法 QueueSubmit 隐式 host→device 域操作，不添加 GPU 全局等待或 HOST barrier；可复用原显式 HOST 对照选项。

三次写依次为原 write_prepare、直接 store、原 write_prepare。exclusion 期间 protect 跳过这些页、全局 epoch 返回 0、range proof 不可用、dirty_since 返回 unknown/dirty；fault/prepare 不得增长。最后实际 fence 完成后，先销毁 buffer、unmap/free imported memory（驱动退 pin），才释放 exclusion；随后证明原 prepare 与真实写故障恢复。

初始已 PROT 的独立负例必须在 exclusion 失败处停止，不尝试 import、不解保护后重试；只在终结释放 CPU allocation 时调用原 remove_range。全程无 guest 地址、真实游戏模块、allocator hook、GPU 写共享或游戏修改。调用者自有分配与独占 VMA 是 API 前提，不能据此支持任意 malloc/游戏堆。

宿主 Lavapipe + 官方 Khronos validation/synchronization validation 的结果仅验证 coherent API 路径与状态机；Android 非 coherent、kbase 物理页寿命必须单独在固定实验 driver 和普通 app UID 验证。任何通过都不证明实际游戏节省内存、带宽、FPS 或发热，也不能把旧 dirty-COW 失败删掉。

生产路径仍未启用：有限只读池要补齐所有 image/GPU writer/CPU effect/alias 的 quiescent demote、历史 submission 引用与失败回退。首层 API 在普通路径新增静态空间和原子读取成本尚无收益闭合，保持独立，不合主线。
