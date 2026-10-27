# 上游 Vulkan 快照：驻留内存与复制流量必须分开

## 范围

源码核对为上游 `f1e4b081af8ea65d95f0bf490a568cb657c3917e` 的
`dm/gfx/vulkan/engine/xbg_game.cpp`。真机数字来自此前已完成的
`b811b90172f4eb40ec1bc89d903308e0c5ab88d0` 同源码诊断构建：同一 APK
分别选择系统 Qualcomm 和 B3 Turnip，游戏输出 1968×900、3D 比例 0.85。
不能把旧构建的实测数字称为最新提交实测。

这里讨论兼容层额外保存的 Buffer 内容，不包含游戏原有堆、VkImage、
Render Target、Pipeline、驱动内部对象，也不是进程总 PSS。

## 已有实测能确定的量

原始日志：

- `.local/logs/remaining-upstream-gap-v1/DRIVER-S2-logcat.txt`
- `.local/logs/remaining-upstream-gap-v1/DRIVER-T4-logcat.txt`
- `.local/logs/q735-system-native-ir-v1/SYSTEM-IR2/native-logcat.txt`

| 部分 | 证据 | 占用含义 |
|---|---|---|
| wtrack 大 Storage | 三个不同地址，每个 `bytes=19573408`；源码每个范围建两个 Version | 额外 Buffer 有效容量共 117440448 字节，即约 112 MiB；尚未计入 Vulkan 分配对齐 |
| vcache 顶点/索引 | S2 最后两窗 `arena=27.9/28.8MB`；T4 为 `30.8/31.5MB` | 字段按 1048576 换算，实际是 MiB；是已使用内容量，分配块还可能留有空洞 |
| stream/readback arenas | 两个 Slot，各自持有独立 Arena；标准块为 32 MiB | 临时快照、Uniform、上传/回读共同使用，不是只有快照；日志未直接给出所有块的总保留容量 |

wtrack 的三个范围是累计建立的三个独立地址，不是同一地址记录三次。
`release()` 在 renderer shutdown 才统一释放。每个版本的最后使用序号与
Slot 退休条件保护仍在 GPU 使用中的内容；它们不是 CPU/GPU 零拷贝。
每帧仅复制约 22～27 KiB 的脏页，与完整保留约 112 MiB 的两份 Storage
快照可以同时成立。

vcache 的 64 MiB 是当前 arena **有效内容预算**，不是全进程严格内存上限。
越过预算时旧 arena 延迟到引用它的 Slot 退休才释放，期间可以新旧并存；
块粒度、对齐和弃用空间也使分配容量大于有效内容量。

因此，将这几类额外 Buffer 驻留概括成 **约两三百 MiB 的量级估算**有依据，
但不能宣称已经完整测出 200/300 MiB 的精确总量。现有日志缺少所有
Slot 的 stream/readback chunk 容量汇总及分配需求对齐量；启动上传峰值、
切场景以及 cache 退休重叠也可能更高。

## 容易误读的日志

S2 稳态一条 `frames=60 ... snap_mb=1682.0 arena_peak_mb=9.9`：

- `snap_mb` 是整个 60 帧报告窗累计的 snapshot miss **请求范围字节**，在
  wtrack/vcache 命中之前就累计。除以 60 约 28 MiB/帧，既不是同时驻留
  1.68 GiB，也不等于每帧真正 memcpy 28 MiB。
- `arena_peak_mb` 是各次 Slot 完成时 stream/readback 有效使用高水位的
  最大值，不是所有分配块或两个 Slot 的总容量。
- `host_mb=0` 对应宿主命令段 `host_segments`，不能据此声称没有 Buffer
  快照或额外 Vulkan 内存。

## 与本分支共享内存的关系

共享合格线性 Buffer 可以免去对应完整内容副本、CPU 写入跟踪与上传；
这依然不意味着无需执行期同步，也不意味着纹理、附件、格式转换、
非共享回退或动态数据的所有内存都消失。
更少驻留、复制和 CPU 工作是明确机制收益；它不能单独证明整帧更快，
更不能抵销实际 driver/shader 执行差异。应继续分别检查驻留量、真实复制量、
CPU 硬件计数以及 GPU 计数。
