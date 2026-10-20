# Android 缓冲分段与共享深度附件

## 当前默认策略

Android Activity 冷启动未收到 `pool_window_bytes` 时，默认按 16 MiB
窗口建立游戏 Pool 的 Vulkan backing。`--pool-window-bytes 0` 保留整池参考路径；
原生命令行及 Linux 的默认值没有改变。运行中不允许切换该策略。

已存在 backing 的跨窗口绑定会在原有 GPU 完成、CPU effects 和资源回收边界，
合并覆盖该绑定所需的有限范围。只复制已初始化区间，保持区间外 backing 的身份。
无法证明范围、参考同步、首次宽绑定或节点预算回退仍允许使用整池 backing。
这不是驱逐、丢弃游戏数据或跳过 CPU/GPU 同步。

同一 APK、分辨率及营地路线的短测结果：

| 设备 | 整池 PSS | 有限范围 PSS | 减少 | backing 容量变化 |
| --- | ---: | ---: | ---: | ---: |
| Mali-G710 手机 | 4,092,956 KiB | 3,333,864 KiB | 741.30 MiB | 1113 → 419 MiB |
| Adreno 735 手机 | 2,658,182 KiB | 2,577,068 KiB | 79.21 MiB | 1113 → 435 MiB |

backing 的声明容量不是物理驻留内存。以上 PSS 来自每次行走后的一次系统
`dumpsys meminfo`，采样前后 backing 状态稳定。两次场景动画、PSO 状态不完全相同；
Mali 后一次触发已有公开热状态策略、目标帧率变为 30，因此不能把此对照用于
证明帧率或持续散热改善，也尚未证明 6 GB 设备能完成启动。

本机原始报告（不包含在仓库）为
`.local/logs/pool-finite-v1-mali-actual-memory-comparison.json` 和
`.local/logs/pool-finite-v1-q-actual-memory-comparison.json`。

## 减少重复物理范围查找

当前 Adreno 真游戏的独立 simpleperf 与 Perfetto 捕获中，NVN 翻译线程的完整
replay 平均 Running 25.6554 ms / wall 28.5226 ms。481 个线程样本中，
22 个 self 样本落在 backing 范围解析；其中 19 个调用点持有资源注册锁。
样本数不能推导缓存命中率或可以节省的毫秒数。

这些已持锁的调用现在复用每个 Pool 的最后一个物理 backing，核对完整范围、
buffer 容量和非零 generation；命中时减少从链表头开始的查找。每个 Pool
增加一个指针，未命中时多一次检查。单 backing、空范围及未持锁的原有
recording/completion 路径沿用原解析器，没有增加锁、原子操作或热路径分配。

迁移发布及销毁前在同一资源锁内清空缓存，避免引用已释放 backing。
缓存只证明地址映射，不证明数据新鲜；脏页、上传、GPU authority、写回及 hazard
处理均继续执行。测试覆盖非零物理起点、跨节点、洞、尾部、迁移、同地址重建及
真实 GPU 字节；故意破坏命中偏移的负对照会在完成后的字节校验失败。
后续同 Adreno、同 shader 链及设置的短对照中，锁内解析调用族的 self 样本
从 19 减到 8；这不是精确调用次数或命中率。完整 replay 的 Running 为
25.6554 → 25.5545 ms，OS 所选加权频率高约 1.15%，variant、冷池合并及
纹理准备次数也有差异，尚不能证明净 CPU 时间或帧率改善。

## 合并同页上传后的归属更新

`sync_queued_page` 已经计算出同一页、同一 recording cutoff 的精确 changed runs。
上传或记录不可变上传快照、更新 shadow 后，批量从 GPU-only unknown 区间中扣除
这些实际上传字节，将原先每个 run 的加锁、扫描及表复制合并成一次处理。
没有把两个 run 中间未上传的字节算作 CPU-known，也不清除 cutoff 之后的 GPU 写入。
单范围完成写回仍使用原 helper。

归属表仍限制为 256 项。批量结果装不下时，放弃未发布的临时结果，解锁后按
原顺序、原 cutoff 调用逐范围 helper。这样保留原路径能恢复的已知前缀，避免因
过度保守而新增 GPU 回读。容量测试逐一核对 512 个修改 word：前 256 个 word
共 1024 字节保留 CPU-known，其余继续使用 GPU 路径，最终实际字节相同。

正常适用时减少重复锁和扫描；新增一次输入校验，超限时另付一次失败合并。
当前回退实现还可能同时保留两个约 6 KiB 临时栈数组，没有新增持久内存或堆分配。
该机制有当前 Android simpleperf 热点依据，不将局部工作减少直接写成涨帧。

同一 Adreno、同一 shader 链及设置的后续真游戏对照中，完整 replay 的平均
Running 为 25.5545 → 25.3338 ms，wall 为 27.9586 → 27.8674 ms。
归属更新的 inclusive 样本为 13/481 → 1/457，采样丢失为零；两次的 PSO、
冷启动 backing 布局和 OS 所选频率仍有差异，不能把时间变化全部归因于此修改。
两家设备均已执行真实游戏模块，完成原生命周期存档并停机；持续散热尚未验收。

## 地址解析只证明一次物理范围

窗口模式的 `pool_address` 原先先持资源注册锁调用 `vk_pool_span_query`，
之后在 retained owner 路径或再次持锁的慢路径中，又解析一次相同物理范围。
现在在原 mip-clear 回调之后，使用原 epoch 核验和 submission owner 引用，
用同一次精确物理解析完成 admission 与返回值准备，再初始化实际范围。
洞、跨 backing、活动操作内未预留的范围仍拒绝；未知 Pool 和空范围保持原回退。

返回的 backing 由原 submission 引用保护到真实 fence、CPU effects 完成。
初始化过程没有提交、回调或迁移，因此可以沿用刚证明的物理偏移。
不新增地址缓存、锁、原子操作或持久状态，不减少脏页、上传、authority 或 hazard
检查；减少的是重复注册查找和物理解析。新增 guarded 分支，真实 Android 的
CPU 时间、整帧和散热收益仍待候选对照。功能测试包含真实 GPU 字节、非零物理
起点、同地址重建和 admission 失败前不初始化；这些检查不代替真实游戏验收。

随后两台真机均通过真实游戏执行和原生命周期存档。Adreno 官方 CPU 顺序对照
Running 为 25.3338 → 25.2002 ms，OS 所选频率近乎相同，但变体和场景仍有差异，
不作隔离净收益结论。实际 ARM64 栈帧 144 → 192 字节，新增 spill 是额外成本；
原重复 query → address 的调用链在新采样中未观察到。

## 实验：两个子 Pass 使用同一个只读深度附件

`--pixel-local --pixel-local-shared-ds` 是独立实验，两个开关均默认关闭。
目的是验证移动驱动能否将已使用 input attachment 的相邻子 Pass 保留在同一
物理 tile pass。先前实际启用 input attachment 后，原厂计数器没有证明实际
processed tiles 或外部读写量下降；不能将 API 上的合并当成硬件合并成功。

仅对已经通过全部 pixel-local 约束、逻辑上第一 Pass 没有深度附件、第二 Pass
只读深度的情形，物理上为两个子 Pass 声明同一个深度附件。第一 Pass 的有效
深度/模板测试及写入全部禁用，并进入完整 PSO key；不修改逻辑 NVN 状态。
深度 LOAD 保留，真实前序写入依赖在进入合并 Pass 前处理，depth bias 等不能
证明安全的情况回退。producer MRT 之外的历史纹理仍使用普通 sampler。

若 producer 的 PSO 创建失败，已录制的 producer draw 先按真实 STORE 完成，
仅当前 draw 用普通路径重试；尚未录制时释放合并对象后重试。不得重放之前的
draw、提前消费后续 clear，或保留失败的两批合并计划。测试同时覆盖首次和
后续 producer PSO 失败，以及真实 FragDepth/discard 与深度/模板内容保留。

实际 Pass 结束后，即使消费者未执行且深度使用 STORE_NONE，也立即登记物理
DS LOAD 的读取历史。该登记不进入待提交或待放弃的 shader 批，也不触发资源
创建、布局转换或回调。新增用例从 transfer 初始化、片元采样的只读深度前史
出发，故障回退后再次写入深度：旧实现的实际 barrier 漏掉 E/L 阶段，修复后
包含该阶段。Vulkan 同步校验层未报告这个旧缺口；负对照依据实际 API scope，
不能写成校验层检出了问题。

共享深度的启动查询声明放在运行时头文件。离线 proof/module ABI 的头文件
保持原字节，原 shader 工具自然计算相同链号，无须改写缓存身份或任何产物。

三种模式已使用相同包、同一完整验证 shader 链，在 Mali 真游戏中完成普通画面
检查和原厂 Streamline 捕获。普通运行使用 Android 默认 16 MiB 窗口；三段
GPU 对照均显式使用整池参考，其他设置相同，不能混用两者的内存数据。

按 2–10 秒窗口内完整 CAM 帧数归一的结果：

| 每完整帧 | 普通路径 | 子集 input attachment | 再启用共享深度 |
| --- | ---: | ---: | ---: |
| 外部读写 | 367.755 MB | 354.795 MB | 367.629 MB |
| Fragment jobs | 140.779 | 139.896 | 140.593 |
| Fragment tasks | 53,300 | 51,353 | 51,741 |

子集路径观察到外部读写减少 3.52%、fragment tasks 减少 3.65%，但 processed
tiles 仅变动约 −0.10%，总 GPU 周期增加约 1.98%。这支持局部访存工作减少，
不能证明总 GPU 提速、物理 tile pass 合并或持续降温。新增的有界命令分析和
变体选择还有 CPU 成本，不能只用 GPU 指标忽略它们。

共享深度段在捕获内新增 PSO/变体，读写量约回到普通路径水平，尚不能证明它的
独立收益或回退。两项开关目前仍默认关闭。三段均出现公开热状态策略的目标
60 → 30 变化，GPU 频率计数不可用；不据此宣称实际帧率被硬限制为 30。

上述计数器是系统 GPU 数据；起止还有两个不完整边界帧，不是应用独占计数。
PA pixels 由 fragment tasks × 1024 派生，不是独立的片元 Shader invocation。
单个 draw 区间时间也不能作为 TBDR shader 成本分摊依据。完整本地报告为
`.local/logs/shared-ds-v3-mali-arm-three-phase-official-comparison-v1.json`。

当前 16 MiB 窗口的额外 Mali CPU 对照中，OFF/ON 均为 238 个完整 replay，
Running 为 25.2247 → 27.6931 ms；PSO/变体为 182/33 与 178/31，热状态也不同，
开启段后续目标由 30 变为 20。不能把这个差值全部归因于 planner，也不能忽略
CPU 成本。实现保留，当前仍默认关闭，继续分开记录带宽节省与 CPU／整帧代价。

## 普通 Pass 分别处理深度与模板 Store

普通物理 Pass 记录不可逆的逐平面 may-write，覆盖 load CLEAR、中途 clear 和
实际 draw 的深度写入／模板操作。支持 `VK_EXT_load_store_op_none` 时，若深度与
模板只有一方可能写入，另一方保持 LOAD、布局不变且原本 STORE，可将未写一方
改为 NONE。保留原布局与保守 hazard，不用 DONT_CARE 代替需要保留的内容。
已有的全只读 NONE、未来完整覆写的逐平面 DONT_CARE 继续使用原逻辑。

这项新增优化不扩展双 subpass 的拓扑。另修复一项既存融合边界：producer
实际写入的 DS 与 consumer 只读 DS 相同时，缺少内部布局／LOAD 历史证明，
保守回原分 Pass。此前已执行的 clear 会留在普通 Pass 或先完成物化；未来
clear 不能越过前瞻证明。真实 GPU 回归同时检查 CLEAR 前后、颜色和两个 DS 平面。

当前 Mali-G710 驱动 1.1.177 没有 NONE 扩展，仍使用 STORE；Adreno 735 驱动
1.3.128 提供该扩展，可能适用。不可将此候选算作 Mali 带宽或发热改善。
普通／共享只读、PSO 失败、无扩展回退的实际 API 参数及 GPU 内容检查通过，
Android 真机带宽收益仍待验证。

## 初始共享分配的独立功能验证

上游 Metal 在游戏原内存上建立 `newBufferWithBytesNoCopy`。当前 Vulkan 使用
独立 backing 与 shadow，CPU 脏页比较、实际更新、GPU 写回的工作量需要分别统计。
`pool_uploads.used_bytes/protected_bytes` 不是实际扫描量或上传量；GPU 回读的
internal/cached 字节是总回读的子集，不能相加。已录制的逻辑 copy 也不等于
已完成的两个 CPU 目的地复制，更不等于硬件 DRAM 总线流量。

两台驱动均无 `VK_EXT_external_memory_host`，但从分配之初共享仍有可能。
独立 ARM64 小程序分别验证普通 HOST_VISIBLE Buffer 与 AHardwareBuffer BLOB
导入，CPU 写入、GPU copy/fill、fence 后 invalidate／CPU 校验均通过，没有 AHB
lock 或游戏模块执行。Mali 两条路径均得到 Cached、非 Coherent 类型；高通普通
分配可用 Cached+Coherent，AHB 导入在该测试中只提供未标记 Cached 的 Host-visible
类型。因此 AHB 不是必然更适合游戏 CPU 堆的入口。

测试声明 1 MiB、仅检查其中 16 KiB，运行于 shell UID，没有请求验证层。
它不证明实际 app 分配、Shader 读取、多 GiB 驻留、固定 guest 地址、mprotect、
在飞版本或游戏性能。源码与结果保存在本机
`.local/logs/ahb-shared-allocation-probe-v1-both-summary.json`。


## 实际 Pool 拷贝量与驻留内存（1786d52）

同一 APK、各机既有 Shader 链、16 MiB 窗口、0.75 固定 3D、无抓帧层，分别完成
营地静止及原摇杆行走。字节诊断只在该次冷启动开启；普通运行默认关闭。
以下分母是 **guest submission**，不是显示帧；两机窗口都没有 backing migration。

| 每次 guest submission | Mali 静止／行走 | Adreno 735 静止／行走 |
| --- | --- | --- |
| 比较输入各自的逻辑扫描跨度 | 4.113／4.168 MiB | 3.893／4.052 MiB |
| CPU 直接写入 GPU mirror | 403.0／426.0 KiB | 362.8／413.3 KiB |
| 已计入的 CPU 复制目的字节总量 | 6.454／6.517 MiB | 6.419／6.484 MiB |

约 72% 的复制目的字节来自完成后的 readback 与 known 数据发布到 guest、shadow
两个目的地；known 不可变快照另占约 1 MiB/guest submission。字节数不是耗时，
也不是 DRAM 总线流量。很小的 queued staging→buffer GPU copy 字节不能替代上述
HOST_VISIBLE mirror 的 CPU 上传量。普通窗口没有排空 fence，记录与完成计数可在
边界相差一份在途发布，不将其算作额外复制。原始数据及归一化报告位于本机
`.local/logs/pool-copy-accounting-v1-two-device-normalized-analysis.json`。

随后在默认关闭字节诊断的独立运行中，用官方 LLDB 一次 all-stop 读取 typed Pool
地址表，并在同一 stop 读取一次内核 smaps；没有 inferior function call、驻留采样
或 target memory probe。按每个 VMA 的页大小取覆盖区间并合并，得到：

| 页覆盖范围的内核 RSS 界限 | Mali | Adreno 735 |
| --- | --- | --- |
| active guest heap | 1335.93 MiB | 1334.56 MiB |
| Pool shadow 合集 | 65.07–148.46 MiB | 128.21–129.19 MiB |
| Pool mirror CPU 映射 | PFN 映射，实际物理驻留未知 | 116.86–143.30 MiB |

Shadow 1113 MiB 的虚拟容量不是已经占用 1113 MiB RAM。Mali 403 MiB backing 的
PFN 映射在 smaps 中 RSS=0，不代表 GPU 物理驻留为零。高通 backing 容量为435 MiB，
也不能直接当成全部驻留。Pool guest 是 active heap 的子集；不同虚拟映射也可能
物理别名。上述界限不能相加成唯一物理 RAM，也尚未证明每个相同逻辑字节在两份
内存中同时驻留。因此当前没有精确的重复 RAM 总量。两次采集均已原生命周期新
存档两次、停机并关闭本次调试器。v2 页覆盖报告位于本机
`.local/logs/pool-residency-onestop-v1-{mali,q}-1786/`。


## 当前提交保留范围内的物理分段提示

已有 01ff 的 Simpleperf 官方调用栈仍在 retained-context `pool_address` 分支看到
`pool_span_resolve`（Mali 6、Adreno 735 11 个样本）；此前只优化了持注册锁的解析。
现给每份提交的 8 个既有 lookup slot 各加一个私有 node 指针。引用和 registry epoch
仍由原机制保护；命中校验不可变 node 的逻辑边界、generation/handle 和物理 Buffer
extent，每次重算 local offset。whole backing 及未命中保留原 resolver；不共享
`pool.locked_span_hint`，也不跳过初始化、脏页、GPU ownership 或阶段依赖检查。
16 份提交最多增加约 1 KiB。迁移原先要求引用排空，不能在 live hint 下退休 node。

新增实际 GPU 字节检查覆盖同 node 不同偏移、跨 node 切换、GPU-only 值与 CPU
局部修改共存。owned async/serial 及全套 async Vulkan 检查通过，VVL/SYNC 零错误；
隔离负例将 cached local offset 错用 0，真实 GPU 结果比较如期失败。未预留／空洞／
跨 node 的五种拒绝测试都在 CPU 初始化前拒绝。现阶段只证明少做链表解析的机制
和功能正确性，尚未取得该候选 Android 真机耗时、整帧或发热净收益。


## 复用 Uniform 更新的不可变暂存源

`OP_UPDATE_UB` 已先把录制时数据复制进该提交持有的 upload temporary。若它是
当前 recording 新建且不可变的私有源，并具 HOST_COHERENT、HOST_CACHED 属性，
完成发布可直接读取这份已有源，省去第二份 known snapshot 的 payload malloc／
memcpy／free 和对应 free callback 节点；原 GPU copy、fence、guest/shadow 发布
与 authority 更新不变。temporary 仍由原提交保留到全部完成副作用和 reset 后。
分配放在可能结束 Pass／回放 callback 的依赖处理之后，确保属于实际发命令的
那份 recording。任意 caller bytes、旧提交、复制 wrapper、越界与非 Cached 源
不借用；普通 copying API 和原 CPU-copy reference 路径保留。

当前 Mali 常见 upload memory 为0x7，缺 HOST_CACHED，继续原快照回退；高通已
观察到0xf，可适用。此改动不消除主要 deferred B2B 的约1MiB快照，不宣称 Mali
或整帧已经提速。独立原 NVN UPDATE_UB、延迟真实fence、后续部分GPU写、CPU局部
修改、任意源修改与跨提交拒借的 GPU 数据检查通过，合入后仍需统一检查与真机验收。


## CPU BC 解码的压缩中间数据不再使用 VkBuffer

仅当设备缺少对应 BC 采样格式、原后端已经走 CPU decode 时，deswizzle 输出的压缩
raw 数据只供同步 CPU decoder 消费。原实现仍为它申请映射 VkBuffer，并保留到
submission fence、随后可能进入全局 temporary cache；GPU 实际只读取解码后的
另一个 staging。现把该 CPU-only 中间数据改为精确大小的 CPU heap scratch，沿用
原 deswizzle／decoder，全部 depth slice 解码完立即 free。最终 GPU source、格式、
布局、dirty tracking、上传与 fence 完全沿用原路径。

此路径每个 decoded mip/layer 增加 malloc/free，省去原 raw VkBuffer/VMA 操作及
GPU allocation 的提交／idle 缓存保留；没有增加数据复制或永久 CPU scratch cache。
释放后 allocator 是否立刻归还 RSS 未证明。ASTC/native BC 路径不变，不能把当前
ASTC 游戏资源都算成命中。独立 fixture 用 TEST_EMBED 能力屏蔽强制原有 decoded
分支，并保留 native BC 对照，检查4种BC格式、3 mip、2 layer、首次／不变／一子
资源脏重传的全部 GPU 输出像素。合入后统一回归与真机验收另行记录，尚无该候选
在真实游戏减少多少驻留内存或发热的结论。

## e7b1331 真机窗口对照及默认策略

同一冻结 APK、原有 Shader 链、固定 0.75 3D、原生输出、bloom 开启，关闭
字节诊断及抓帧层。两台分别完成原摇杆营地行走；每次只取一份系统 meminfo，
前后核对位置及 backing 不变，结束后原生命周期新存档两次并停机。

| 单次营地快照 | Mali 16 / 4 MiB | Adreno 735 16 / 4 MiB |
| --- | --- | --- |
| PSS（KiB） | 3348237 / 3230772 | 2645751 / 2690167 |
| Graphics（KiB，已包含在 PSS） | 1543140 / 1414216 | 567920 / 601156 |
| backing 容量（MiB，非物理驻留） | 451 / 279 | 435 / 291 |
| 启动及行走累计 promotion | 5 / 19 | 6 / 18 |

Mali 的 PSS 少 114.7 MiB，高通反而多 43.4 MiB。每机各一对快照，驱动分配
历史和实际场景工作量仍可能不同，不能把容量差直接当作驻留收益或因果证明。
高通第一次 4 MiB 路线未在 5 秒内达到原 radius=4，保留失败记录；只增加原摇杆
操作时间预算到 12 秒后的独立重试通过，没有放宽距离断言或修改游戏状态。

随后 Mali 的官方 Perfetto / Simpleperf 独立对照中，4 / 16 MiB 的 NVN replay
Running 为 27.87 / 29.54 ms，运行时加权系统频率为 1571 / 1518 MHz。16 MiB
后半段发生热等级上升和目标节奏 30→20，4 MiB 保持 30；两者不能直接比较净
CPU 或帧率收益。两个工具窗口中都没有 promotion 或 backing 变化，因此没有
测到较小窗口增加合并时的额外成本，也不能排除行走／新资源加载时卡顿。
当前继续保留 **16 MiB 默认值**，4 MiB 为显式对照选项；没有宣称已解决持续
发热或实际 6 GiB 设备启动。数据分别位于本机
`.local/logs/pool-window-memory-v3-two-device-pair-analysis.json` 和
`.local/logs/current-cpu-e7b-mali-window-pair-v1-analysis.json`。

## 只在实际 PSO 创建时构造 Shader Stage 参数

`VkPipelineShaderStageCreateInfo` 原先在每次 draw 的资源循环中构造，即使最终
命中已有 PSO。现延后到真正调用驱动创建管线的 miss 分支；所有逐 draw 资源
准备、依赖、完整 key 和缓存失效规则保持。hybrid→原 specialization→base 的
失败回退每次先查缓存，只有真实 miss 才按当前 module／完整 tuple 重建参数。
保留 pixel-local 替换 Shader 前的 specialization 判定，不能从 clone 重新猜测。

命中时省去 stage 参数写入，常见 VS+FS 为 96 字节栈结构；不是 GPU 带宽节省。
真实 miss 多一次最多五级的扫描，没有新堆分配或持久缓存。原 NDK O3 编译
的栈帧与保存寄存器不变。GPU 字节测试同时检查实际驱动参数、缓存命中和两级
创建失败回退；不以静态指令数或功能测试数冒充 Android 整帧收益。

## 保留原 NVN Pool flags，暂不改变可见性策略

HLE 已保留原 MemoryPool flags，但此前向后端注册时丢失。新增独立入口传递
原始 32 位 flags 和 known 状态，明确区分显式零与旧调用方未知；每个私有 Pool
增加 8 字节，无逐 draw 开销。Metal 委托原 NoCopy 分配，Vulkan 只存元数据。
usage、分配类型、脏页、GPU 依赖、CPU 发布和写回都不读取这些新字段。

这些字段供官方 LLDB 在一次 all-stop 读取真实 Pool 意图，不新增计时／采样器。
尚不能据 flags 或未观察到 CPU read 就删除回读：游戏仍可经普通指针读取，
完成后的 shadow 也是后续 CPU 修改比较的基线。任何共享分配或可见性省略都
需另证 NVN 合同、实际读写依赖和在飞生命周期；本改动本身没有内存或帧率收益。

5b6f822 在两台真实游戏中完成原营地路线后，各一次官方 LLDB 挂起读到相同的
七个原始 flags；全部为 known，未猜测未识别位：

| 注册容量 | NVN flags | 仓库枚举名称 | Mali / Adreno backing 容量 |
| --- | --- | --- | --- |
| 40 MiB | 0x22 | CPU_UNCACHED / GPU_CACHED | 0 / 0 MiB |
| 2 MiB | 0x12 | CPU_UNCACHED / GPU_UNCACHED | 2 / 2 MiB |
| 710 MiB | 0x21 | CPU_NO_ACCESS / GPU_CACHED | 224 / 256 MiB |
| 1 MiB | 0x14 | CPU_CACHED / GPU_UNCACHED | 1 / 1 MiB |
| 400 MiB | 0x22 | CPU_UNCACHED / GPU_CACHED | 176 / 176 MiB |
| 10 MiB | 0x64 | CPU_CACHED / GPU_CACHED / SHADER_CODE | 0 / 0 MiB |
| 512 MiB | 0xa1 | CPU_NO_ACCESS / GPU_CACHED / COMPRESSIBLE | 0 / 0 MiB |

这是实际 Pool 意图与分配状态，**不是**各 Pool 的回读量、独占物理驻留或完整
CPU 访问证明。下一步需将真实 writeback／known-copy 的来源与目的区间连到
Pool 身份；不能把 710 MiB 注册容量当成每帧复制量。两台存档两次并退出，没有
native 错误记录。取证记录为本机
`.local/logs/pool-residency-pool-flags-v2-both-analysis.json`。

## 同页多段 CPU 修改只解析一次 backing

e7b 官方 Simpleperf 在 `sync_page` 及逐 run 的物理 span 查找中仍有样本。
仅在原非 pending-GPU、非 dual-copy、非 reference 扫描分支，且一页有多个
changed run、整页能精确落在同一 mapped backing 时，解析一次该页的物理
映射，再按每个原 run 的相对偏移复制。单 run、跨 node、空洞和无法证明的
范围走原路径。每次 retry 后重新证明，不留跨页／跨提交的指针缓存。

只省去同页 count−1 次链表解析，不减少脏页比较或复制字节。增加一次完整
范围证明及 run 边界验证；证明失败多一次查找。原 pool 引用防止 backing
退休，原 publication mutex 防止 CPU 发布交错；两者职责不同。逐 run 的
shadow 更新、authority cutoff、pending CPU wait 和 GPU ownership 保持。

真实 GPU 检查包含非零 backing 起点、多段修改之间保留的 GPU-only 新值、
换 node、193 字节尾段及 reference；测试入口对实际独立 Buffer／空洞拒绝
整段证明。两个隔离负例（丢失物理偏移、错误整页复制）均触发 GPU 字节断言。
独立全套异步／同步检查通过；当前没有此项 Android CPU 耗时或帧率净收益。


## 按 Pool 的写入完成序列复用纹理未修改判断

当前 aa0fc00 真机 Simpleperf 仍在 `image_upload` 与其 `nvn_dirty_since`
调用中保留样本。此前负缓存由全局写入完成序列失效：其他 Pool 修改也会让
本纹理重新查页。新增第二级判断，在原纹理内存范围、原上传 stamp、Pool
生命周期和本 Pool 写入完成序列均有效且未变时复用此前“未修改”的结果。
原全局快路径保留，不把 Pool 的最大页代数或当前时间当作完成证明。

快照取在原查页之前。每次页发布都递增完成序列，较小代数晚于较大代数发布
也会使缓存失效。槽复用先改变生命周期，再发布序列；热刷新只读原子字段，
冷观察仅尝试注册锁，失败便走原查页。生命周期／序列饱和永久拒绝该证明；
全局序列饱和会禁用两级证明。原 reference 选项绕过两级。上传 stamp、页代数、
保护状态和 CPU／GPU authority 均不由此缓存替换。

成立时减少重复页代数扫描，不减少真正发生的上传字节。代价为每 image 24B、
4096 个静态槽合计 64KiB，以及每次受保护页发布一次额外原子 RMW；全局递增
改为饱和 CAS。全局 miss 的热刷新最多四次 acquire 读取，同 Pool 写入后仍需
原查页，因此其新增成本与实际跨 Pool 命中比例都需真机评估。大小来自独立
host DWARF，尚不是 Android 物理驻留量或运行时间结论。

独立真实 Vulkan 像素测试覆盖跨 Pool 复用、同 Pool 失效、实际保护页写故障、
乱序发布、同槽复用、未跟踪范围和溢出。仅在最大代数改变时递增完成序列的
错误负对照确实触发 GPU 像素断言；候选修复后异步／同步全套通过。源码和
证据冻结于本机 `.local/logs/texture-range-dirty-proof-v1-source-ready.json`。
这是底层纹理脏页检查减负，没有改游戏模块、Shader 或玩法；整帧和发热收益
仍待当前 Android 包验证。


## 脏页扫描共用两块循环控制

64498d2 的官方 CPU Profile 在页比较的循环与成对读取位置仍有样本。保留每个
64B 块原有 NEON 比较、mask 与 append 次序，只将两个已证明在范围内的块
顺序放进同一轮 128B 循环；剩余 64B 块和有限尾段走原算法。不增加读取范围，
不包含未改动的 GPU-owned 字，不改变上传、shadow 或 authority。

独立 NDK O3 编译确认，全等核心每 128B 从两轮 40 条静态指令变为 35 条，
主要省循环判断和基址运算；不含一次性 setup、尾段及不同 mask 分布的成本。
`sync_page` 代码增 516B，栈帧少 16B，SP/FP 静态地址位置少 4 处。代码增大会
增加指令缓存压力，这些静态数值不能代替 Android 耗时或帧率对照。

真实 Vulkan 尾页 128/132/192/196/256B、64/128B 跨块合并、GPU-only 未修改
间隔及不可访问 guard 页均已验证；漏掉第二块的错误负对照触发实际 GPU 读回
字节断言。独立异步／同步全套通过。记录在本机
`.local/logs/changed-word-scan-v2-source-ready.json`；本项不减少实际比较／上传
字节，不宣称已降低温度或解决整帧瓶颈。


## 实验：CPU_NO_ACCESS 的未知 GPU 结果按需回填

5b6f822 两机官方 LLDB 在一次实际内部提交中，各观察到 710MiB、flags=0x21 的
Pool 有三个未知 GPU 写回区间，合计 1,320,960B；另有 160B 已知 CPU 拷贝。
它只是一个真实提交批次，不代表整帧或实际 DRAM。此前未知写入总会安排
GPU→缓存 staging→guest/shadow 回填，即使后续仅由 GPU 使用。

新增**默认关闭**实验 `--defer-pool-publication`。仅已知且 CPU 位恰为
CPU_NO_ACCESS、范围对齐的 Pool，在无 CPU 曝光页重叠时延后未知 GPU 结果
回填。已知 CPU 拷贝继续原发布；所有 GPU writer、资源依赖、NVN fence 与
事件／查询效果保留。被跳过的范围不产生 CPU 可见性证明，不把旧 guest 或
shadow 冒充新值。后续 GPU 读取仍通过实际 backing 与原 hazard 链路。

CPU 曝光使用每 Pool 的懒分配 4KiB 页位图；纹理按真实内存大小曝光，不整池
曝光。曝光控制未来写回，当前来源还须通过持久 authority 判断已新鲜。必要
CPU 读取通过原翻译队列在安全边界提交并等待真实恢复，随后再借 descriptor、
image 或临时 buffer。任意源部分重叠／跨 Pool 分解处理，并校验 Pool 身份。
ReadTexels 实验路径整体进入同一队列，不在 guest 临时录制区另加等待。

纹理入口先检查来源可见性，再走 has_layout 与两级脏缓存早退。可选覆盖证明
和原 UI 跟随重放在未知或 pending 输入时拒绝增强，不插入新提交。跟随绘制
的实例扩展按真实 stream/divisor/span 再验证，内部不走强读或 pending 提交；
原 NVN draw 保留，后续普通 draw 仍真实恢复。不能把少一次可选增强当成提速。

实验目前仅支持异步 GPU 提交及正常 HLE worker。串行、同步 HLE、worker
创建失败、全池上传参考或全提交屏障参考启动组合实际关闭；生效后拒绝这些
不兼容参考模式的热开启。Android 选项仅冷启动，已有进程不会热切；现有
1Hz game_state 只报告实际 mode，未增加采样器、计时器或诊断温度读取。

新增成本包括每 Pool 16B、32 个队列 payload 合计 512B；710MiB Pool 曝光
位图懒分配约 22.2KiB，whole 模式额外 authority 约 6KiB。ON 增加来源枚举、
短锁和真正需要时的提交／等待；OFF 也仍有 TLS／条件分支成本，不能声称零
开销。authority、位图超界或分配失败保守回退，不假造可见性。

v2 的 GPU 字节验证覆盖 late exposure、跨 Pool、compute、known-copy、BC
CPU 上传和 ghost 捕获后 texture/VBO 改写，supported ON、默认 OFF、串行
请求 ON 的实际 OFF 全套均通过，零 VUID/SYNC。原参考用例只有在实验实际
启用时改为验证拒绝热切；OFF 保留原 whole/reference 断言。详见本机
`.local/logs/pool-publication-defer-v2-source-ready.json` 和独立 review。

原生 ARM 任意指针读取 NO_ACCESS 内存并不可观测；flags 不能独自证明游戏
永不如此读取。因此保持默认 OFF，需真机 ON/OFF、画面与实际 readback／CPU
工作量对照；未宣称整帧、发热或内存净收益。工具链与原游戏 binary 未为此
叠补丁，继续从原始 ExeFS 经同一入口生成并核验部署。


## 当前同包真机对照与默认路径调用开销

01de8df 的同一 APK / ELF / 原始模块在 Mali-G710 与 Adreno735 上均完成
实验 OFF 和 ON 的原生游戏营地步行、截图检查及原生命周期两次存档后停机。
两机分别沿用已核验的 Shader 链；实际 getter 与请求一致，没有用宿主测试
替代真实游戏。当前记录见本机 `pool-pub-v2-{mali,q}-{off,on}1-*`。

Mali 同为 305 次 guest-submit 的功能计数区间，录制的 readback 字节由
428,072,560B 降为 28,313,200B，即每次约 1.40MB → 0.093MB（减少 93.4%）；
已知 CPU 副本发布字节基本不变。这是待执行读回工作的减少，不是已完成
memcpy、物理 DRAM 或显示帧计数。官方 Streamline 独立导出中，系统 GPU
外部读写速率分别下降 12.3% / 15.5%；两轮 pacing target、variant 和迁移
状态存在差异，不能将每秒速率下降直接写成同帧带宽、帧率或发热净收益。
部分 GPU 等待指标仍上升，也不能声称 CPU/GPU 互等已解决。完整边界在本机
`pool-pub-v2-mali-off-on-analysis-ready-v1.json`；实验继续默认关闭。

检查最终 Android ELF 还发现，OFF draw 会调用立即返回的 CPU 来源 helper，
但编译后的函数在判断前已经建立 320B 栈帧。调用端增加同一不可变选项判断，
OFF 跳过 helper 及 ghost getter，ON 顺序保持不变。没有新增环境变量读取或
改动资源依赖。独立 NDK 代码同时增加 36B、draw 栈帧增加 16B，不能称为
整体零开销；独立真实 Vulkan OFF、ON、串行回退及专有字节用例全部通过。
这是调用减负，不声称已经提高 Android 帧率；01de8df 的上述真机数据不包含
此后的一行调用端修改。


## Android 内存压力下回收已完成的闲置缓冲

6GB / Adreno650 两次真实原生游戏运行达到营地，但出现画面错误并被系统以
LOW_MEMORY 终止；它们不是可玩验收。缺少稳态进程 meminfo，退出 PSS 也
不是峰值，不能据此认定哪类内存最大。现有代码会保留最多 256MiB 可复用
临时 buffer，以及最多 32 个常量 arena（通常约 16MiB）；这些是逻辑上限，
并非这台机器当时实际保留量。

新增 Android onTrimMemory/onLowMemory 请求；回调只设置合并原子标志，不
初始化 GPU、不分配、不持后端锁或等待。原翻译线程在无 recording 借用的
job 边界，回收已完成 CPU effects 与 driver reset 的 retired 资源，再销毁
闲置 temporary 和 available driver arena。保留当前和在飞资源、Pool/
shadow、image、PSO 及原缓存默认预算。请求未满足安全条件时继续保留。

原后台先请求回收，再保持原存档与挂起顺序；已存在的异步 owner 在挂起后
增加独立控制 job 收尾。不为回收创建 owner/GPU；DM_NVN_SYNC 或 owner
创建失败的 inline 回退不执行回收、不改变原调度，保留 pending 请求。
后台收尾不能保证避免存档前的 LMK，系统也可能在通知内存压力之前杀进程。

独立 GPU 字节用例覆盖 active recording、GPU 未完成、GPU 完成但 reset 未
完成时均不误回收，以及后续复用、回收与重建；真实输出字节保持正确。
VMA 现成统计显示用例 allocation 从 3 降至 1、逻辑 bytes 从 524800 降至
256，而 block 仍为 1。VMA 的 32MiB block 与空块保留策略使逻辑回收不等于
物理释放。普通异步 job 增加缓存标志读取，真正回收时销毁成本及未来重新
分配可能增加 CPU 工作；未新增生产计时器、采样器或逐帧环境变量判断。
源码与有限测试记录在本机 `android-idle-cache-trim-v1-source-ready.json`。


## 不可变 Pool binding 筛选

Shader 的最终资源表构造时生成两个 64 位 eligibility mask。每次逻辑范围收集只
遍历确实可能读取 NVN Pool 的绑定；host c0/c1/c2、普通图像和采样器不再重复
经过 count/kind/register 分类。仍逐次读取当前地址、完整长度、偏移、writable、
texel descriptor 和 alignment，并保留原 pin、迁移、上传、RAW 与回退流程。
它不缓存动态资源，也不跳过 CPU 修改或 GPU 依赖。

Base、texel alternative 和 pixel-local alternative 都在最终 schema 的同一构造点
重新生成 mask。未知 kind、数组、非法 register、未完成构造的对象保留原完整扫描，
包括遇到后续错误前已产生的 sink 结果；空表及索引 0/63/64/127 已覆盖。

当前地牢日志有 158 个已加载 shader；其中 54 个能以日志 active nb 与离线表完全
对应，980 个绑定中有 637 个可排除。这是未按实际调用加权的 schema 计数，不能写
成 CPU 降低 65%。旧官方 CPU 样本中该收集函数 self 为 9/625，且早于后续优化；
只能支持这是一个有实际适用对象的局部减负，不能预报明显整帧或散热收益。

新增 owned GPU 用例 async/sync 各 856 项，验证 sampled→SSBO→compute RAW、跨 Pool、
descriptor retarget 和随后 CPU 编辑；完整 async/sync 分别 22,031,792/21,929,813 项，
官方 VVL/SYNC 均无错误。相同 NDK 编译中每 shader 增加 24B，范围收集栈增加 16B；
被保留绑定增加 set-bit 选择成本，全部都是 Pool 的 schema 没有排除收益。保留此
优化基于减少已证明多余分类的机制，并不声称已经测出 Android FPS 或温度净收益。

冻结源码、指令与测试：本机 `.local/logs/pool-binding-mask-v1-source-ready.json`；
独立源码审查：`.local/logs/pool-binding-mask-v1-root-review.json`。

## 2026-10-20：共享堆整游戏首次验证

以下是独立实验分支 `codex/shared-guest-game-combined-v2` 的结果，实验源码
`03cbdba5be720393efa6648bb8f4a7b9f5feed70` 尚未合入生产默认路径。
983 项 APK 构建输入、6 个原生库已逐项校验。游戏模块仍由原始 NSO 经统一
入口生成并验证，使用 `elfdesc-gate`、`touch-run-v4`、private gates 和 x18
group size 3；没有叠改已加工的二进制。

此前普通应用 UID 下，Mali-G710 MC10 的有限 4MiB 全 Pool 导入用例通过：
Buffer 659 项、Event 117 项、Query 7827 项。实际内存属性为 `0xb`，即
HOST_CACHED、非 HOST_COHERENT；用例执行了 Flush/Invalidate。这些是功能
字节验证，不是游戏、整 Pool 内存开销或散热验收。直接 PanVK ICD 的这些
Android 测试没有加载 VVL，不能写成真机验证层无错误。

真实游戏 PID 32621 冷启动使用固定驱动 `bdd9df32…`，日志确认
`actual_shared=1`；实际 timestampPeriod 为约 76.92308ns、valid bits 为 64，
原模块开始原生执行，但在主菜单约第 60 帧触发：

```
Shared CPU packaged source aliases GPU-owned image: partial freshness/retile not implemented
```

因此共享 Buffer/堆已实际启用，但整游戏共享路径没有通过。现有错误只证明
图像 CPU 源的 dirty proof 失败，不能认定图像字节确实被 CPU 改过。源码有
一条需修正的路径：首次 GPU 完整清屏可跳过 CPU 上传，图像已 initialized，
但尚无 CPU 源 dirty scope/stamp；后续采样会将 untracked 保守判为 dirty。
同页邻居写也可能导致页级误报。不能简单删除拒绝条件，或把 baseline 字节
相等当作 GPU-owned 区域没有 CPU 后写的证明；CPU 写回与旧值相同的字节，
仍可能是在覆盖 GPU 新值。任意 partial CPU 编辑与 optimal/linear 别名的
发布规则仍未完成，不宣称已经消除了所有回写或等待。

失败证据位于本机 `.local/logs/shared-game-03cbdba-mali-v1/`。进程已自行
退出，没有为存档重新启动；已回读校验恢复 c8 APK、原有私有驱动和全部
28 项模块，并确认游戏停止。原 full ASTC4 资源与存档未参与部署。没有取得
本次共享游戏的稳定 PSS、FPS 或温度对照，不能据此宣称内存、帧率或散热
改善。下一步先修正图像源追踪生命周期，再验证真实游戏；生产仍保留原路径。

后继独立候选 `c075d41` 补齐首次 GPU 定义的源追踪、shared/defer0 下的
draw/dispatch 准备、ReadTexels 原 owner 任务路由，以及 raw/image 别名检查和
用途升级后的 CPU authority。983 项输入和六库再次闭合后，已从原始 NSO
重新生成并验证模块，执行第二次真实共享游戏测试。它仍在主菜单失败：
serial 2267、格式 `0x79`（ASTC 4×4 UNORM）、源范围 24064B；此时 scope=1、
stamp=3404，所以首次未跟踪并非全部根因。Android 系统 tombstone 与同
BuildID ELF 的 LLVM 符号化确定失败调用为 `vk_texture_copy_t2t` 的
`image_upload(s)`，即纹理复制的源对象。此前具体哪个操作取得 GPU 写入
权属仍待查明，不能据格式直接断言源来自 CPU，也不能直接删除保护条件。
没有取得内存端点或游戏性能对照；已再次校验恢复 c8、全部模块及旧驱动，
游戏停止。失败和系统栈证据在本机 `shared-game-c075d41-mali-v1/`。

### 独立 ASTC 原生图像存储导入

另一个默认关闭的实验已通过普通应用 UID 真机功能验证：驱动源码分支
`codex/panvk-native-image-import-v1` 的 `3446abb`，独立测试应用分支
`codex/panvk-native-image-app-v1` 的 `948d702`，驱动 SHA `6b15cf08…`。
它使用原 Mesa Host Image Copy 生成目标原生排布，不另写 vendor swizzle。

有限对象为 13×9、四级 mip、单层 2D 的 ASTC 4×4/6×6 UNORM 和 sRGB。
两个独立进程分别生成文件、导入并采样；四格式各覆盖两次数据发布和两次
导入生命周期。实际使用 HOST_CACHED 非一致性内存、非零 Pool 偏移及
Flush；采样输出、前后邻居、文件不变性与原映射释放后仍存活的检查通过。
消费端没有第二次图像内存分配，也未调用 Host Image Copy 恢复图像。
初始内容依据明确的私有 producer GENERAL/FOREIGN 交接合同，不依赖从
UNDEFINED 转换保留旧字节。Android 直接 ICD 仍明确未启用 VVL。

本机验收记录为 `native-image-app-948d702-mali-v1-root-review.json`。
原游戏包、模块、六项资源、shader chain 与原私有驱动均经前后校验未改，
独立应用、临时文件与进程已清理。这证明的是有限图像直接导入机制；尚未
实现游戏 packaged 8B layout、StorageSize、完整/缩略 mip 表及流式发布协议，
没有游戏 PSS、带宽、FPS 或散热收益结论。

后续 `86381c7` 资源工具与独立应用 `3324edf` 已使用真实游戏
`firepit_a_ALB`（256×256、九级 mip）执行普通 UID 真机对照。原始资源经
现有工具生成 ASTC4、ASTC6 两种输入，分别烘焙原生排布、在另一个冷进程
导入。每种输入均另外通过标准 `vkCmdCopyBufferToImage` 建立 GPU 参考，
全部九级 mip 的 87,381 个采样 texel 与各自参考相同；这不是 ASTC4 与
ASTC6 有损编码之间的画质比较。消费端仍没有第二份导入图像存储。

实际原生图像存储需求为 ASTC4 90,112B、ASTC6 45,056B；对应 NVN 排布
资源为 89,600B、64,000B。这些是单资源排布大小，不是进程 PSS、实际
DRAM 流量或整游戏节省量。该驱动目前只接纳 SAMPLED|HOST_TRANSFER；
真实游戏已经使用纹理到纹理复制，因此 TRANSFER_SRC 用途与流式写入
发布合同也必须补齐，不能将当前有限采样验收写成游戏直接导入完成。

验收记录为本机 `native-image-resource-app-3324edf-mali-v1-root-review.json`。
游戏 APK、模块、资源、shader chain 与原私有驱动均保持不变；独立应用
已经卸载，测试进程与临时文件已清理。下一步是在默认关闭的独立路径中
接入通用 NVN 元数据与资源工具链，再验证真实游戏内存和持续游玩散热。

复制用途不能继承上述采样验收。独立 `4093920` 测试将用途明确扩为
SAMPLED|HOST_TRANSFER|TRANSFER_SRC|TRANSFER_DST，并增加非零 block ROI
的 Buffer→Image、局部 Image→Image 和全 mip Image→Image 对照。标准 BC3
宿主路径在官方 VVL/SYNC 下通过，分别故意漏掉两类复制的负例均触发原
数据断言。988 项 APK 输入、1,733 项实际编译依赖与新驱动输入均已闭合。

Mali 普通 UID 真机使用 `5709f74` 驱动时，两套真实 ASTC 资源烘焙通过；
导入却在首张 ASTC4 的 `vkBindImageMemory` 失败，尚未采样或复制。源码
审查找到驱动内部 meta 为压缩 transfer 图像追加用途和 create flags 后，
Bind 又误用这些内部扩展值验证原应用声明的严格合同。修复须保留 Create
时已验证的原合同，不能放宽成任意 Storage/Attachment 导入。内部 meta
也可能改变物理排布，必须使用新 recipe 重新烘焙，不能复用旧采样产物。

此失败不算整游戏运行或性能结果。独立应用已卸载，原游戏与资源保持不变，
游戏进程停止。记录为本机 `native-image-resource-copy-4093920-mali-v1-root-review.json`；
驱动修复与非一致性内存 CPU 写入保护仍在独立分支推进。

后继驱动 `01efc12` 已通过同一 `4093920` APK 的真实复测：两次冷进程分别
烘焙、导入，ASTC4/ASTC6 均完成初始九级 mip 对照、非零 block ROI 的
Buffer→Image 更新、Image→Image 局部更新，以及导入图像作为源的全 mip
复制；每步采样与标准 GPU 上传参考一致，实际修改像素且未改动其余区域。
真实内存属性为 `0xb`、atom 64，未分配第二份导入图像存储。CPU 没有在
GPU 运行期间修改导入存储，因此不能用此证明并发写入保护已经完成。

新 transfer recipe 的基础 rowPitch 为 ASTC4 1024B、ASTC6 704B，旧采样
recipe 为 4096B、2816B；总图像分配仍为 90,112B、45,056B。实际排布变化
进一步说明旧原生 payload 不能直接复用，也不能从这个功能结果推导带宽
或散热收益。原游戏包、模块、资源和旧驱动未改，独立应用已卸载。
本机验收为 `native-image-resource-copy-4093920-binding-v2-mali-root-review.json`；
可复现 controller 在 `codex/native-image-resource-copy-app-v1` 的 `36c0896`。
真正的游戏 NVN 资源表、缩略 mip 和流式更新集成尚未验收。

### 共享 Pool 的快照与非一致性写入保护

共享分支不分配整个 Pool 的 `shadow`，也不通过内容扫描决定 Buffer 上传。
保留的是访问范围、版本、Fence 和有限 image-scope 页的保护信息，并非整份
数据快照。真正需要保留历史值的异步命令输入另算；NVN 排布与私有 VkImage
之间的转换存储也不能冒称已经消除。原生图像直接绑定是后一项的独立工作。

写前保护核心 `b1ea0ef` 与应用包装组合为 `14dae0e`。Mali 普通 UID 两次
独立冷进程验证均通过：实际 `0xb` 非一致性 HOST_CACHED、atom64 下，GPU
写一个 word、CPU 写同 atom 邻 word，CPU 经过保护等待，Fence/Invalidate
后双方字节及后续 VkImage 读取均正确；原 NVN Buffer→Texture→Texture
回归同时覆盖原生 ASTC、局部区域、多 mip/layer 和 CPU 写回原值的情况。
同一包六库与生产签名、990 项构建输入已经核对，两次测试均退出和卸载，
原 APK、模块、资源及 shader chain 前后未变。独立审查与实际记录在本机
`shared-image-gate-14dae0e-nc-mali-v1-independent-review-panvk.json`、
`shared-image-gate-14dae0e-retile-mali-v1/result.json`。直接 ICD 的 VVL 明确关闭，
不以宿主 coherent 或统计检查数量代替这些真实 NC 结果。

该组合也已从原始 NSO 重新加工、单独核验并执行真实游戏。运行不再首先
停于此前的共享图像源保护条件，但在菜单加载约第 62 帧遇到新的实际阻断：
`Vulkan format conversion unavailable: 157 -> 137`，即 ASTC4 UNORM 到 BC3
UNORM。系统 Mali 驱动与私有驱动的 BC 能力声明影响目标物理格式选择；
原 Metal 的 `no_bc` 路径原本处理的是 ASTC 到解压目标，不能直接把这里的
ASTC 数据当 BC3 使用。需核实完整资源、子资源和 CPU 后续写入合同后修复。
本次没有到达原游戏 HUD，不提供内存、FPS 或持续散热收益结论；现场保留在
`shared-game-14dae0e-mali-v2/`，原 c8 APK、28 个模块及旧驱动已经恢复并停机。

当前已部署 fullASTC4 资源缓存的离线清点为 31,072 个描述，其中 27,195 个
符合现有原生图像格式与单层 2D 形态范围，27,194 个还含缩略图。因此必须
区分完整 opaque 原生资源与缩略图原有上传格式，不能仅凭尺寸或文件来源
假定只读或直接导入。1040B/entry 的当前表预计占 28,282,960B 元数据；全部
资源压缩块总量不代表运行时驻留量，也不能据此计算已经实现的内存节省。
