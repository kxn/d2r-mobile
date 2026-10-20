# 普通脏页池的短时 CPU 读取阶段（独立实验）

基于 e2056e1 的共享上传互斥修复。这里只提供共用 dirty API 与必要生命周期协议，**尚未放行 legacy ASTC 格式传播，没有新 APK／手机／游戏验收**。默认不调用 enable，保留原脏页跟踪。

`nvn_dirty_cpu_observation_enable()` 必须在首个 FS 请求和范围登记之前冷调用；同一 dirty 模块持有唯一 static paired-write hooks，重复 enable 幂等。shared wrapper 使用同一入口；legacy 上层以后也必须在首 guest／FS 前选择，不能首次纹理使用时才注册。

`begin(address, bytes, &stamp)` 要求完整 OS 页包络有且只有一个 live tracker、无关闭／缺页／溢出；不支持时返回 NULL、不改 stamp。不增加 scope，不登记 GPU 资源、不导入内存，也不证明 GPU 已完成。调用方保留映射／Pool，并在借用图像前完成原 GPU／CPU-effects preflight。成功后，原 ARMING 协议排除新 kernel lease，等待旧 lease 与 PG_BUSY，建立最终 RO，返回该时刻 dirty stamp。只允许 CPU 读取。`end` 取消 gate 并留下 RO；后续 CPU 写沿原异常／prepare 发布新代数。

原 whole-range state/gen 各只有一份。opt-in 为其增加 4B/页 gate words，关闭后信号可能仍持有 futex 地址，因此 gate-enabled slot 的边界／数组终生不复用。4096 槽与 64MiB **完整 state+gen+gate** metadata 双上限；容量不足不登记该 Pool，沿原 untracked→保守重传，不签发观察证书。不是 GPU pin／显存预算。scope bucket 仍沿原独立上限。

关闭顺序：先置 closing 并使旧 proof 失效；拒绝新观察与 kernel lease；condwait 释放 slot 锁等待观察及旧 FS 真正 end；再等待无锁 fault/prepare/protect 状态访问退出；恢复整范围 RW、撤 live、发布 retired 并唤醒。旧 lease 可以完成其 prepare/pread，避免 arm→close→FSend 锁环。关闭期间的普通缺页不保留状态访问引用等待；唤醒后重新 find，不能用旧范围给同地址的新 Pool 发布代数。新同地址登记在旧 RW 恢复完成前等待。调用方仍须证明 NVN storage 的正常有效期；本 API 不允许释放正在被其它普通线程使用的任意内存。

成本：固定 range metadata 增加 observer_users/closing/accesses；gate-enabled plain fault/prepare/protect 多一对 lock-free SC 状态访问声明；关闭无并发访问时不睡眠。default 无 gate 时没有新增状态访问 RMW，但多条件检查、range proof 的 closing 检查；不能声称免费。启用后的 metadata 不释放／不重用，已有高水位负例。无内容比较、shadow／镜像、GPU wait 或全队列 drain。

owned 负例／正例使用真实 pthread、pread、SIGSEGV/futex 与原 state/gen：FS lease 早于首次范围；ARMING 与 Finalize 同时等待旧 FS；PG_BUSY 发布暂停；旧信号等待后 same VA 新 Pool；重叠／洞／overflow；槽和 metadata 容量回退。source fault 去掉唤醒后重新查找时，真实 CPU 写无法由新 tracker 接管而退出 139。原断言不弱化。

legacy renderer 尚需：冷入口、原 GPU／CPU-effects preflight、普通 raw Buffer／其它 identity 的编码 guard 与 fresh ASTC capability。不能仅因本层通过就把 shared-only flags 打开；也不把 private mirror 初始化／上传视作语义 raw GPU 读取。
