# 实验：共享图片源的非一致性写前准入

本阶段独立于主线，固定共享模式仍默认关闭。没有修改系统驱动、游戏指令、任意分配器或 guest 地址布局。

问题是：GPU retile 只写一个 word，但完成后的 `Invalidate` 会扩到整个 `nonCoherentAtomSize`。CPU 若在 GPU pending 时改同一 atom 的另一个 word，后续 Invalidate 可能破坏它。仅拒绝自发布 ACK 不能解决缓存冲突。

本实现只对**已存在 image dirty scope 的真实 host 页**建立提交租约。普通 linear buffer 不新增整池写保护、比较或上传。页边界来自真实 host page size，不能由 GPU 访问位图的 4KiB 粒度代替。

提交顺序：

1. 对将保护的完整 host 页，等待旧提交交叠的 GPU 访问和 CPU effects；CPU effects 按绝对地址查，GPU bitmap 映射回各 Pool。仅等相交提交的原完成序号。
2. 在原 dirty slot 锁内发布 ARMING；阻止新的 kernel-write lease，等待已经授予的交叠 lease 完整结束。
3. 等旧 `PG_BUSY` 发布完成，再把实际 scope 页设为只读。Fault/preparation 在抢到 BUSY 后二次检查 gate，关闭晚到 CAS 重新打开 RW 的窗口。
4. 原缓存 Flush、原命令提交；提交成功后将 ARMING 转为 submitted。失败先取消并唤醒，再沿原错误退出。
5. 原 completion 等真实 fence，完成所有 shared publication Invalidate，清 gate 并 futex wake，之后才运行 CPU effects。

Linux fault 路径只用 lock-free atomics、原 mprotect 和 raw futex syscall。不在 signal 中拿 mutex、分配、递交或调用 Vulkan；GPU 等待不是持续 yield 自旋。Futex 处理 EINTR/EAGAIN/丢失唤醒；元数据页数组不释放，等待者不引用可释放的 submission token。TLS lease 链用 atomic release/acquire 发布，先 unlink 后 free。未提交 owner 不允许等待自己的 gate。

宿主 `pread` 不会因用户态 SIGSEGV 自动重试。依赖单独的配对 `dm_guest_write_begin/end`，覆盖原 `h_fs_ReadFile` 的整个读取循环与所有退出；既有 `dm_guest_write_prepare` 不变成永久租约。Cold 注册必须在首次 guest/FS 前完成。

## 验证入口

复用原 `dm_vulkan_test_main`，不启动游戏模块：

- `DM_SHARED_IMAGE_NC_GATE_FIXTURE=1`：仅 Linux、真实非 CPU GPU、异步 timeline、实际 HOST_VISIBLE|HOST_COHERENT=0|HOST_CACHED（flags 0xb）及 atom64。真实 GPU semaphore 挡住 retile，CPU 线程直接写同 atom 邻 word 并经过原 fault，真实 fence/Invalidate 完成后放行。逐字节检查 GPU word、CPU word及后续物理图片读回。其它能力返回77/unsupported，不能报 PASS。
- `DM_SHARED_IMAGE_DIRTY_FIXTURE=1` 与 `DM_SHARED_IMAGE_COPY_TEST=1`：原 retile/同值 CPU 更新/多 mip layer/原采样回归。
- `DM_FIXED_SHARED_DEPENDENCY_TEST=page-effect`：旧 CPU-only completion 写同页另一 atom，先完整页等待再准入。
- `page-effect-roi-negative`：保留旧 ROI 等待的必要失败对照，原严格断言必须失败。
- `dm/gfx/tests/t_dirty_gpu_gate`：真实 Linux mprotect/fault/futex、提前/延后 kernel 写租约、PG_BUSY 延迟发布、cancel 与 owner 自锁负例。它明确不执行 GPU。

LVP 软件驱动自己写同一个 imported CPU VMA，会被保护阻塞并等待自己的 fence。该平台不支持本 gate 的真实 GPU 验证；不能按线程名称绕过保护。非一致性实际结果必须由 Android 原 GPU fixture 给出。

## 成本与边界

Cold 启用才分配每个 image-scope bucket 每页4字节 gate（4KiB 页时16KiB/16MiB bucket）；普通 dirty range 增加一个指针，fault/prep 多一次空指针分支。每次 gate arm 使用有界页向量、去重和页保护；每次 kernel 读取有一个完整区间租约，普通 CPU memcpy 不新增租约对象。成功路径不新增 CPU 字节比较或池镜像。

保护范围是页，可能保守等待同页无关访问。新增等待的作用是保证实际 NC 同步正确，不代表帧率已经提升。仍要求原 NVN 资源/存储寿命及 CPU/GPU 访问同步；无 scope 的 linear 页不由本机制补造所有并发规则。非 Linux 和 atom 大于 host 页的能力不启用此实验。主游戏与实际 NC GPU 验证均另行记录，宿主 coherent 通过不能代替。
