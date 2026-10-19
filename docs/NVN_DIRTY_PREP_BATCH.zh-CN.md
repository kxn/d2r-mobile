# 连续宿主写前解保护

0104c1b 的原游戏官方 Simpleperf stat 在同一约 8 秒窗口中记录了 GPU 完成线程
176,318 次 minor fault，其内核周期占该线程已报告用户态＋内核态周期的 59.42%。
另一次官方 minor-fault 记录中，138,684 条完成线程记录定位到写回 memcpy，六个
实际 libc 叶 PC 都是目的端 STR/STP。此前仅用户态 cpu-clock 样本不能覆盖这部分成本。
这些证据尚不能区分 guest/shadow 地址，也不能把缺页直接认定为 mprotect、COW、
首次分配或驱动原因；minor-fault 记录实际 period=1 且有丢失，不能用作 FPS 对照。

源码中，原 host `prep_write` 对每个 PROT 页各调用一次 mprotect(RW)。这项修改仅
合并连续、成功由本线程 CAS 成 BUSY 的页，每批最多 64 页：

1. 遇到已可写页、别人的 BUSY、批大小上限或 range 边界即结束当前批。
2. 先对已占有的前缀执行一次 mprotect，再按原顺序逐页发布 generation、latest、
   range/global changes 和 NONE；宿主准备函数返回前全部完成。
3. 遇到别人 BUSY 时，自己的前缀必须已经处理完，不能带着前缀等待。
4. 单页写故障路径不变。不增加锁、堆分配、线程、时钟或采样器。
5. 新的宿主批调用若失败，立即报错退出，不能把仍受保护的页发布成 NONE。其余
   原 mprotect 调用的错误处理未在本项扩展；有效 live range 的原生命周期约束不变。

减少的是连续 PROT 页上的系统调用、每次调用附带的原诊断时钟/原子、内核进出和
潜在的分次页表/TLB 维护。不声称取消逐页内核处理、minor fault、脏页 generation
或 GPU/CPU 数据复制。每页原来的状态与脏代号发布仍执行；准备整批期间 BUSY
窗口可能变长，最多涉及 64 页记账，并不是墙钟时间上限。可写洞的边界可能多一次
失败 CAS；`g_preps` 诊断计数改为每批合计，非事务快照的瞬时粒度会改变。

新增纯语义 oracle 使用真实 mprotect、带 guard 的自有内存和 /dev/zero 内核写，覆盖
非对齐范围、257 页、多批、已可写洞、相邻独立 range、单页 fault 混用、BUSY
后缀与前缀必须先完成的并发情形，以及受控批失败不得发布成功。候选与原冻结
源码的相同语义都通过，连续 257 页的 RW 调用分别为 5 与 257。独立 ASAN/UBSan
检查也通过；它们不是实际游戏性能数据。

NDK 28.2 / Clang 19 原 O3/TLSdesc 配置编译通过，`prep_write` 栈仍为 128 B，
单页 `on_fault` 栈仍为 80 B。宿主 GPU 测试继续使用原 NVN、Lavapipe 和官方
validation/synchronization validation；Android 实际 CPU、稳定性与散热验收另记。
本次不修改图形/玩法、CPU 模拟、游戏 binary、默认画质或提交节奏。
