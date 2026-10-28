# SDK热点锁的身份：原工具寄存器诊断与反证

2026-10-28。6秒官方Simpleperf诊断找到两个SDK Mutex和一个Event。
13个合格中断态支持快速成功获取、没有经过ArbitrateLock的SVC分支；
不能仅因叶PC落在SDK Enter就把整个热点解释为忙等或跨线程抢锁。

## 方法和范围

Q735真实游戏，f357 APK／26a1 host／aacd2701…69712 SDK，12b8/cf83/BC3
不变，PID649、主游戏owner718、CPU7均核准。仅普通ready，未采正式FPS或
GPU性能窗口。官方record的核心选项为：

```
--duration 6 --clockid monotonic -e instructions:u -f 199
--call-graph dwarf,4096 --no-unwind --post-unwind=no --no-callchain-joiner
```

先查实际官方help和源码，再保留原REG_USER/STACK_USER。单独禁post-unwind
仍可能在采集中展开并删除寄存器。原883个样本、首末跨度5.831986秒，
record退出0，丢样/栈截断为0。整理器只解析官方dump文本，不解码perf或
手工恢复栈；核ABI64、原PC一致、精确SDK中x19/x20的有效区间。

## 实际身份与分支证据

| SDK内部锁地址 | 外层对象 | 直接caller | 样本数，非调用数 |
| --- | --- | --- | ---: |
| 0x75fe701bd8 | MutexType 0x75fe701bc0 | LockMutex，内部偏移24 | 9 |
| 0x757dcb0938 | MutexType 0x757dcb0920 | LockMutex，内部偏移24 | 2 |
| 0x751703de3c | EventType 0x751703de20 | TryWaitEvent，内部偏移28 | 2 |

均位于这次实际guest-heap映射，不是宿主翻译器mutex；requester tag均0x1001。
没有证据其他游戏worker持有这些对象，更上层用途也尚未知。

13条PC都在SDK+3517b0，x19尚未恢复，x0仍等于完整mutex指针。精确SDK
机器码的快速成功分支保留输入x0；若经351780调用SVC26，原HLE正常返回
regs0=0，到该epilogue间没有恢复指针。因此这13条中断态提供未走该SVC
路径的反证。它不证明此前没有exclusive重试、其他时刻没有争用，也不将
约2.5M SDK采样预算精确归为无争用获取成本。PMU precise_ip=0及skid、
抽样偏差仍在，不能推争用率、调用率或整个线程的同步耗时。

## 已尝试的工具与尚缺信息

官方report-sample --show-callchain和debug-unwind对13个原时间戳实际执行，
使用精确host/driver symfs且不覆写raw。两者退出0但仍只有leaf；13次展开
均error4、SDK map/dso unknown。原13678条MMAP没有覆盖该SDK PC，
另存的真实/proc/maps则有匿名SDK执行映射。现有记录缺少它可用的展开
metadata；不能用手解stack或补造调用链填成任务队列/资源/内存用途。

当前没有依据改游戏锁或删除HLE同步。后续应把频繁同步/TLS的执行成本
与真正竞争等待分开，继续由整轮原工具结果排序。

原始数据在`.local/logs/sdk-mutex-register-recipe-v1/SDK-REG1/`：原record/dump、
recipe、实际maps、寄存器、caller官方LLVM、离线展开失败及REPORT.json
哈希索引。fresh原生命周期save2后停止游戏，认证collector退出/清理，
perf_harden原0恢复。本次入口22用户文件及c09最终恢复另由
`.local/logs/store-forward-read-reuse-device-v2/final-restoration.json`确认。
