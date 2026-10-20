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

### CSF 超时：完整队列输出与证据边界

已有 Mesa 故障解码器的累计 ring 游标错误已在 `e8fc0ae` 修复；
`8a73429` 只补充故障后对三条已知根命令流的有界解码，不改变 GPU 指令、
正常提交、缓存操作或等待策略。原游戏 `277f74f` 真机复测得到约 20.5MB
现成解码输出，VT、片元、计算三条根流均在内，原解码器 SIGSEGV 未再出现。
游戏自身的 CSF progress timeout 仍然存在，尚未到 HUD。这是工具修复通过，
不是游戏故障修复或性能提升。

本次 job4351 的快照中，VT 已完成，片元与计算进入 CALL 但未观察到返回。
两个独立审查对齐了 119 段时钟推进和 5,961 个跨队列等待；按源码定义的
共同基准、寄存器保存及有限 GPU 工作条件，依赖图没有缺失的信号生产者，
也没有固有循环。这排除了该条件下的静态计数依赖环，不能证明实际硬件
寄存器相同，不能定位当前阻塞 PC，也不能排除 shader、scoreboard、堆或
内存数据阻止命令完成。静态指令数量不是执行次数、耗时或带宽。

下一证据缺口是已有内核／厂商工具提供的指令指针、等待对象和值、scoreboard
或 blocked reason。普通 shell 的 dmesg 已被拒绝，预期 debugfs 节点未暴露；
不能把应用映射的 user-IO 页面当成内核 CSF 寄存器区猜偏移读取。没有这些
现场证据前，不根据“片元和计算都没返回”删除同步或改某个 shader。

实际记录为本机 `panvk-csf-tool-8a73429-actual-v5-root-review.json`、
`shared-compute-csf-v5-agent-v1/report.json` 及
`shared-format-277f74f-driver-diag-v5-fragment-independent-agent.json`。
本轮原 c8 APK、模块、资源和驱动已恢复，四项 Mesa 属性恢复原值，游戏已停止。

随后只关闭私有驱动 BC 模拟的对照先触发另一处共享纹理 CPU 新鲜度检查，
没有走到原 GPU 超时点；关闭共享的对照又先遇到 ASTC→BC3 格式检查。
该驱动关闭 BC 模拟后仍可能声明原生 BC3 支持，不能把这个选项等同于系统
Mali 驱动的 RGBA 回退。这两次均已恢复生产状态，不构成 GPU 超时根因结论。

为独立验证压缩数组的实际价值，系统 Vulkan 候选核心 `8205364` 已完成：
默认关闭，仅对整页、可跟踪且尚无内容的新数组沿用实际 ASTC4 编码；
不满足条件仍走旧 RGBA 路径，普通非候选纹理不套用共享分支的全局限制。
原始 CPU 字节暴露、Storage、其他 GPU 写入和未知编码更新受到单独保护。
精确 CPU 暴露限制区保留至 Pool 关闭，直接裸指针 CPU 读取不在保证范围。

该核心已独立审查、通过 NDK ARM64 构建和宿主功能回归；LVP 不支持 ASTC，
宿主通过不代表物理压缩路径通过。测试包装 `1f29f2a` 显式控制验证层，复用
已有 fd 日志出口，运行时渲染核心保持 `8205364`。994 项 APK 输入、六个运行库
与生产签名核对；游戏模块在单独冻结的精确源码树中从原始 NSO 重建，再单独
verify-only。控制器后续提交影响过一次源码指纹校验，旧失败保留，不曾部署。

系统 Mali 驱动、普通应用 UID 的新独立 GPU 测试已经通过：42 层数组确实由
decoded RGBA 路径变为实际 ASTC4，保持逻辑 BC3、mip/layer、局部复制、线性与
sRGB 采样及原 Pool 字节；CPU 观察还验证了旧 GPU 快照、新 CPU 更新和下一次
上传的顺序。验证层明确关闭，不把功能检查宣称为 VVL 或性能验收。独立包已
卸载，生产文件前后相同。记录为 `legacy-astc-stock-1f29f2a-actual-oracle-root-review.json`。

同六库的真实 ARM64 游戏随后在纹理绑定中触发
`Shared CPU conversion missing page-level ordered preflight`。Android 崩溃栈与
原 ELF 的 addr2line 已定位到 draw → shader bindings → texture view → CPU
转换；draw/dispatch 的外层准备及公共源检查没有启用 legacy 模式。还须核对
optional replay 禁止内部等待、已初始化干净纹理上的待完成 CPU 写回两类边界，
不能仅删深层检查绕过。此轮没有到 HUD，原 c8 APK、模块、资源和驱动已恢复，
游戏停止。不能据此声称已降低驻留、带宽或温度。

后续修复 `1a5280a` 已补齐普通 draw、dispatch 和公共 helper 的 legacy 准入，
并在借用资源前处理同 host 页尚未发布的 CPU effect，避免已初始化的 clean
图像沿版本缓存跳过更新。可选 ghost 重播保守拒绝 tracked packaged 来源，
实例展开的晚 pending 重验也保持非阻塞；原 NVN draw 不因此丢弃。

同源新包的系统 Mali 普通 UID 独立 GPU 检查通过 1,040,777 项，新增 outer
preflight 的七项条件均实际通过，包括 clean 图像后排 CPU effect、同页邻字节、
不同页不额外等待，以及原 shader 的 GPU 像素。验证层和同步验证明确关闭，
不将功能断言数量冒称为 VVL 验收。它与随后游戏使用相同六个运行库。

该版本随后执行从原始 NSO 重新加工、单独核验的真实 ARM64 游戏，系统
Vulkan、原 private guest heap、16 MiB 复制 Pool、完整 ASTC4 资源及
`cbd8d2da0b0f` shader 链。serial19 的逻辑 BC3、42 层七级 mip 数组实际采用
ASTC4，越过旧错误并到达原选角和营地 HUD；所留截图中地面、人物与 UI 正常。
原生命周期确认 fresh save 两次提交成功后停机，生产 c8 包和原文件已回滚核验。
新 APK SHA256 为 `d9171b7f17b48094ba4359dcf3f437b6446a09f2995d51f41f070ccf6eef0b2e`；
宿主 ELF 为 `fb03f5d8065cec293ae04114e498b548cfaef8e29493389a5b6ddd5edd5c9f8b`。

这是默认关闭候选的一次短营地功能通过，不是完全共享路径通过；独立 Pool
backing、shadow 与复制仍在。截图的 30.4 只是该时刻显示值，没有受控对照，
不能算涨帧；驻留、带宽净收益及持续散热尚未验收。后续只准备同 APK 的
OFF/ON 官方工具对照，不把准备或连接重建写成已经采集。记录为本机
`legacy-image-cpu-preflight-1a5280a-mali-v1/result.json` 和
`legacy-image-cpu-preflight-1a5280a-original-mali-v2/root-result.json`，
后者同时保留 launch、native log、HUD 截图和 fresh-save 证据。

### 共享 Pool 的快照与非一致性写入保护

共享分支不分配整个 Pool 的 `shadow`，也不通过内容扫描决定 Buffer 上传。
保留的是访问范围、版本、Fence 和有限 image-scope 页的保护信息，并非整份
数据快照。真正需要保留历史值的异步命令输入另算；NVN 排布与私有 VkImage
之间的转换存储也不能冒称已经消除。原生图像直接绑定是后一项的独立工作。

对生产 `c8e7a25`、共享 `277f74f` 和原生图像 `fbd09b2` 的分配与使用点复核：
生产路径仍有延迟分配的 Pool CPU shadow 和独立 Vulkan Buffer 存储；共享
路径在分配 shadow 前返回，直接导入原 guest 存储，known-copy/B2B 的 CPU
内容快照也已关闭。因此，完整共享不要求再留一份全尺寸比较快照。
版本、页状态、访问范围和 Fence 是元数据，不保存第二份内容。

普通 Tegra 纹理仍同时保留原始排布和真正参与采样的 optimal 图像，短期
转换 staging 也仍被使用；不能把它们按“没用的跟踪快照”删除。匹配原生
资源的 full 图像已直接绑定原 Pool 内存，跳过第二份图像存储和初始上传，
缩略图及未覆盖资源仍走原路径。这里只确认源码分配行为与有限真机功能，
不把 VkMemory 声明容量、calloc 虚拟容量或离线资源总量当作实际 PSS 节省。
本机源码审计为 `shared-mode-content-copies-source-audit-agent.json`。

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

后续仅增强原错误输出的 `451b9b1` 实际复现确认：源为 128×128、八级 mip
的 packaged ASTC4；目标为 128×128、42 层、七级 mip 的非 packaged BC3
数组，尚未初始化且未向外暴露原始 VkImage。这次向第 41 层的第 0 级做
完整同尺寸复制。它支持继续验证“新目标沿用源物理压缩编码”的受限方案，
不允许将已有 BC3 内容或未知 CPU 更新当 ASTC。原可玩版再次恢复并停机。

修复核心 `b8a727d` 与严格测试包装组合为 `277f74f`：仅允许已导入共享
Pool 上尚无有效内容的新数组采用已知 ASTC4 物理编码，保留逻辑 BC3 描述
及 Pool 原字节；观察 CPU 版本、保留旧存储直到 GPU 完成，并拒绝无法解释
的原始字节、Storage、Attachment 和 CPU 后续更新。Mali 普通 UID 实测通过
128×128×42、七级 mip 的整面/局部复制、mip/layer 保持及默认、线性、sRGB
视图采样；新的独立检查项确实执行，不以旧 retile 通过冒充新路径。记录为
`shared-format-277f74f-retile-root-review.json`，原生产包与资源前后相同。

同版本从原始 NSO 重新生成并单独验证后运行真实游戏，serial19 的数组已
成功采用 ASTC4，越过原格式错误。之后角色场景加载时，GPU 完成线程收到
`VK_ERROR_DEVICE_LOST`（Fence wait -4），尚未到 HUD。Android 退出记录是
该进程主动 abort；现有日志没有 GPU fault 地址或具体提交证据，不能据此
归因最后一个 shader、ASTC 修复或 OOM。kernel dmesg 读取被系统拒绝，后续
优先使用驱动已有诊断能力。现场 `shared-game-format-277f74f-mali-v1/`；原
c8 APK、驱动和 34 项模块／资源校验恢复，进程已退出，不提供性能收益结论。

同 APK／驱动的现成 Mesa 日志已进一步捕获 kernel CSF group1 progress
timeout；带 `sync` 和不带 `sync` 各一次均复现，最先报告的子队列分别为
1 和 0，不能仅据此归因片元 shader。四项 Android Mesa 诊断属性均已恢复，
游戏及原驱动再次恢复并停止。现成故障解码器另有独立缺陷：直接将累计
extract 游标加到 64KiB ring 地址，运行多圈后越界并在写出详情前崩溃。
正在修复已有解码器的环形范围和错误检查；这项工具修复不等于 GPU 根因
修复。记录为 `shared-format-277f74f-driver-diagnostic-pair-root-review.json`。

另有已确认的保护边界：原 packaged 图像直接 `mprotect` 的两个入口未等待
同 host 页上已有的 FS 写租约；kernel `pread` 与新保护交错可能返回 EFAULT。
新观察握手必须等待旧页依赖、已有 FS 租约和 BUSY 发布后再完成只读保护，
不能把一次保护成功或未遇到错误当作全部并发安全。此缺口与性能收益分别验收。

当前已部署 fullASTC4 资源缓存的离线清点为 31,072 个描述，其中 27,195 个
符合现有原生图像格式与单层 2D 形态范围，27,194 个还含缩略图。因此必须
区分完整 opaque 原生资源与缩略图原有上传格式，不能仅凭尺寸或文件来源
假定只读或直接导入。1040B/entry 的当前表预计占 28,282,960B 元数据；全部
资源压缩块总量不代表运行时驻留量，也不能据此计算已经实现的内存节省。

后续 `fbd09b2` 已将原生资源表、整图/缩略图规则和真实共享写前门控组合，
并在 Mali 普通 UID 下通过原 NVN 接口执行两次独立冷进程测试。ASTC4 与
ASTC6 各 307 项检查通过：九级整图的块读取与采样、五级缩略图原 packaged
上传及 Buffer→Texture、原生图像的首个 Buffer 别名写入和后续复制/发布。
实际共享内存属性为 `0xb`、atom64，4MiB 有限 Pool 没有整池快照；这次
执行的是原 NVN Builder、Texture、命令录制/翻译及队列入口，较先前仅
独立 Vulkan 导入测试更接近运行时合同。直接 ICD 的 VVL 仍明确关闭。

测试使用 `01efc12` 驱动和重新烘焙的两套真实资源。原 APK、28 个模块、
六项资源和 shader chain 前后完全相同；独立包、进程及临时文件均清理。
本机记录为 `native-family-fbd09b2-mali-root-review.json`。这是有限对象的
真实 GPU 正确性结果；原游戏 Builder 的查询/分配顺序、原 packaged 保护
握手以及单资源真实游戏加载仍需验证，没有整游戏内存、FPS 或散热收益结论。

## 1a5280a：系统驱动 ASTC 开关的官方工具结果

同一 `1a5280a` APK、原始模块、全 ASTC4 资源和 `cbd8d2da0b0f` shader chain，
在 Mali 上分别关闭、开启 legacy ASTC copy，各采集独立的 gatord 8 秒和
Simpleperf stat 8 秒。保持系统 Vulkan、非共享 guest heap、固定 0.75 比例及
2772×1240 输出；两侧均通过原摇杆营地路线、原生命周期 fresh save2 后停止，
最终恢复并验证 `c8e7a25` APK、模块、资源和私有驱动文件。系统截图命令超时的
首轮在采样前退出，失败记录保留；有效数据是本机 `legacy-astc-1a5280a-mali-arm-pair-v2`。

Streamline 官方导出的整窗 GPU 外部读/写，OFF 为 4.300/2.766 GB/s，ON 为
4.233/2.630 GB/s；纹理外读分别为 2.740/2.756 GB/s。按 fine quad 作描述性
归一后，纹理外读仍基本不变。OFF 的温控目标在 20/30 FPS 间切换，ON 为
20 FPS，Uniform 特化变体数也为 33/31，故这些数字不能作为 ASTC 的净收益或涨帧结论。
完整 0–8 秒及 1–7 秒区间均保留；没有选择最好区间，也没有以内部 L2 流量
冒充 DRAM：LS→L2 的写流量约 6.2–6.4 GB/s，与外部写是不同测量边界。
本机分析记录 `legacy-astc-1a5280a-official-descriptive-comparison-agent-v2.json`。

随后对同一 1a 进程补充官方 Simpleperf 99 Hz、8 秒用户态调用栈，原菜单通过
正常触摸进入世界，采样后 fresh save2、停止并恢复 c8。1163 个样本没有丢失或
截断，原始保留栈深度仍有差异；519 个翻译线程样本中，脏页代数查询自身仅
4 个，不能据此把整池代数扫描当作主要 CPU 热点。绑定及访问依赖管理仍值得
继续减负。完成线程的 40 个用户态样本里，17 个驱动叶样本的宿主调用点对应
`vkResetCommandPool`，14 个 `memcpy` 叶样本对应 CPU effects 发布；内核没有
被此次 `cpu-clock:u` 覆盖，不能把这些比例当作线程墙钟占比，更不能将全部
minor faults 归于 mprotect。采样记录 `legacy-astc-1a5280a-mali-CPU99-recovered-v2`。

## 36a9781：完成线程批处理与空预检减负

当前 Mali 可玩包已更新为 `36a9781`，使用系统 Vulkan 驱动、非共享 guest heap、
原 fullASTC4 资源；legacy ASTC copy 实验仍默认关闭。保留原同步与数据依赖，
新增两项减负：真实 fence 完成后，将最多 16 个原 VMA allocation 的 invalidate
合并调用，尾批仍执行；非 packaged、空内存或空布局的纹理，在原注册锁和有效性
检查内提前退出 CPU 输入预检查，避免复制整个纹理描述及无效预检。

前者不减少缓存维护范围或 GPU 写回字节，只减少驱动入口；后者不增加预测缓存，
真实 packaged 输入的晚到 CPU effects 仍须发布。两项均不跳过实际 GPU 完成或
数据新鲜度检查。完成线程增加有界临时栈空间，没有新增热路径堆分配。

独立普通 UID 的 Mali 测试确认真实 `0xb` 非一致性内存上的 35 个写回范围正确，
覆盖尾批、fence 后发布和命令池重置顺序。合并包的独立实际 GPU 回归也通过，
新增预检检查覆盖描述符重新注册、空输入、不相关依赖及真实 draw/dispatch 采样。
这些是字节和生命周期验收，不是性能测量。

同一合并包随后分别以 legacy ASTC copy 开启及默认关闭运行原始 ARM64 游戏，
均到达营地 HUD，截图未见明显黑块或几何损坏，通过原生命周期 fresh save2 后
停止。默认关闭的一次已保留为手机当前安装包；APK、28 项模块、资源、shader
chain 与私有驱动文件均已核验。原始 NSO 由该冻结源码重新加工并单独 verify，
没有向历史加工 binary 叠补丁。记录为本机 `stock-cpu-batch-36a9781-owned-mali-v2`、
`stock-cpu-batch-36a9781-original-mali-v1` 和
`stock-cpu-batch-36a9781-default-promote-mali-v1`。

这次只证明短营地功能及减少重复工作的机制；截图 FPS 不能证明净涨帧，持续
游玩散热仍未验收。PanVK 共享内存实验不属于当前默认包。此前私有驱动对照含有
本地修改，且在格式或保护条件处提前失败，尚未证明未修改 PanVK 能运行完整
游戏；因此角色加载 GPU hang 不能归因于共享接口，需先补齐干净驱动基线。

## full6 实际采样链核实

`36a9781` 的一次官方 GFXReconstruct 捕获已完成：原营地第 739 帧，官方
info 确认一帧非空 Vulkan 记录，原始文件 254,721,664B。使用现有 full6
完整资源闭包，固定 0.75 比例、系统驱动及非共享 guest heap；没有再采集
重复的 GPU 计数器基线。捕获关闭后先完成原生命周期 fresh save2、停止进程，
再恢复 full4 资源、35 项生产文件哈希及调试层设置，最后才传输并官方转换。

捕获前的同次运行截图中，3D 营地可辨，但底部血球、蓝球和药水图标明显异常。
此时 full6 资源与 GFXR 层均与默认运行不同，尚不能归因其中任意一项；不作为
full6 画质或性能验收。已有完整调用记录将用于核实上传源、数组复制目标和
最终 sampled image 的实际物理格式，不能以离线转换数量代替最终压缩采样证据。
原始记录在本机 `full6-adoption-36a9781-mali-api-v1/result.json`，手机已恢复
默认 full4 并停止。截图上的 FPS 不参与性能比较。

随后一次必要的无调试层 full6 短跑（不再抓帧）中，底部血球、蓝球及药水
图标正常。原存档停机、默认 full4 和 35 项文件恢复均通过；这只能将异常
收窄至带工具层运行或其时序交互，不能据此认定 GFXR 自身有缺陷。
记录为 `full6-plain-36a9781-mali-visual-v1/result.json`。

官方 API 和实际 shader 模块的关系已核实：主 HDR 组的三个主要材质槽，在
586 个 draw 中共出现 1,758 次绑定，其中 788 次为最终 ASTC6 图像视图，
其余 970 次仍为 ASTC4。此数只证明最终绑定覆盖，不是动态采样次数或 DRAM
份额，不能将 44.8% 的绑定换算成带宽收益。当前证据排除了“资源虽转了、
主场景完全没绑定 ASTC6”的猜测，仍未证明实际纹理带宽或持续帧率改善。

同帧实际提交中没有 `vkCmdCopyBufferToImage`，故本帧的图像上传及其 CPU
解排布不是主要渲染流量来源。离线生成可移植的上传块排列，能针对加载和
流式更新消除 CPU 解排布，但不能据此认定能降低稳定画面每帧的采样带宽。
GPU 最终 optimal 存储布局仍须与可移植上传布局区分；可移植上传源的加工不必
依赖私有驱动。详细绑定记录为 `full6-adoption-36a9781-actual-resource-adoption-v1.json`。

## 宿主同步验证记录纠错

此前 `0856` 两份 source-ready 将 `official_host_VVL_sync` 记为 true，但旧成功
日志没有启用同步验证的明确标记，只能支持 core VVL。后续显式启用同步验证
发现测试自身将临时 buffer 的 `readback` 分类改为 1 后漏恢复，污染后续复用。
`022d625` 仅修测试恢复原字段，不改运行时、不加屏障；完整 async 与 owned
回归在明确启用 VVL+sync 后通过。原失败、成功日志和冻结报告均保留，新纠错
记录为 `readback-host-sync-validation-erratum-agent-v1.json`。此纠错不撤销已执行
的 Mali 非一致性 GPU 字节验收，也不能将宿主同步验证称作 Android 同步层验收。

## 原 PanVK fork 的非共享真实游戏对照

纯上游 Mesa `5a07217f` 不含此手机所需的 vendor kbase 后端，不能称作可用的
零修改 Android 基线。可对照的是原 `zenithblue-oss/panvk-kbase-android`
`5b87b74d` 及其原有 179 项补丁，未加入本项目的 host-import 或诊断修改。
独立重新构建与最初原 fork 的 `c5ecf526…` 驱动逐字节相同。

在同一 `36a9781` APK／原始模块上，关闭 shared guest heap、legacy ASTC copy
及 decode8，使用原 BC 资源和原触控 UI。离线核实 31,093 个纹理及描述缓存的
CFT 条目与原 RomFS 相同，避免此前 ASTC→BC 格式检查提前阻断。普通 UID
真实进程确认加载 driverID20 的原 PanVK、private-anonymous guest heap。

该次运行越过主菜单，打开选角期间仍出现 `Vulkan GPU fence wait: -4`
及 SIGABRT，未达到营地 HUD。证明此次 GPU 失效不需要新增共享导入；不能
据此证明与共享版同根因，也不能排除 Vulkan 后端的错误用法。当前 direct ICD
入口绕过 Android loader，既有 validation 开关主动拒绝该组合，尚无官方
校验层证明此次调用有效，下一步需先接通现成工具入口。

进程已自行退出，未为存档重新启动；原 35 项文件、完整旧驱动及缓存目录恢复
核验通过，临时资源清理完成。本机记录 `panvk-original-fork-ui-game-v4/result.json`。
此前 v3 在启动前遇到缓存文件清单过长导致 ADB `error: closed`，未运行游戏，
失败记录保留；v4 仅将哈希请求分批，并非另一项驱动改动。本次不提供性能结论。

## 022d625：图像绑定的静态分类

Mali 当前默认包进一步更新为 `022d625`，源码保存在
`codex/readback-fixture-class-restore-v1` 分支。它在 `36a9781` 上增加图像 binding
索引的构造期分类：三个每次绘制访问图像的入口只枚举已知图像索引，仍执行
当前句柄、注册、脏页、CPU effects 和 hazard 检查，不预测动态资源内容。
全图像 schema 保留连续扫描，未知或无效 schema 保留原错误路径；每个 shader
增加 16B，NDK 编译确认相关热函数栈不变。测试自身的 readback 分类恢复修复
包含在同一提交历史中，与运行时优化分开记录。

完整 canonical APK、994 项输入、1744 项实际 NDK 依赖和六库已归档；原始 NSO
重新生成并单独 verify。普通 UID 真机回归通过原 legacy ASTC／CPU prefilter
字节与生命周期检查；Android 没有另跑宿主 1030 项分类边界测试，也没有启用
Android 校验层。真实游戏默认 OFF 路径到达营地，截图中的人物、地形、血球和
药水正常，通过 fresh save2 后停机并保留为当前包。记录为
`image-binding-022d625-default-promote-mali-v1/root-result.json`。

该修改省去重复分类，不减少实际纹理上传或 shader 采样；截图中的 48.7 FPS
不是与旧包匹配的工具对照，不能报告为该修改的涨帧收益。官方 Simpleperf 的
翻译线程候选对照另行记录，持续游玩散热仍未验收。


## 原版 Turnip：真实游戏基线已通过

在 Adreno 735 上，未加入本项目 host-import 修改的 Mesa 26.2.5 KGSL Turnip
已由原 APK、原始 ARM64 模块、原 BC 资源和四项上游触控 UI 运行到营地。
源码归档的 12,808 项原文件相同、补丁列表为空；实际驱动 SHA256 为
`032b7fd9457f1846aebecb0c9cbc1124dc3200272ddd7e397d784b3ca9d23a12`。
真实进程确认 FD735／driverID，选角到营地约 17.23 秒，所留截图中人物、
地形、传送门、血蓝球及药水正常。输出 2750×1260、固定 0.75 比例。

这次关闭共享导入，使用原 pool mirror，不是共享内存实验通过，也不是 FPS
对照。截图的 23.5 FPS 不能算驱动增益；GMEM/SYSMEM 未强制，默认按 Pass
自动选择。已完成原生命周期 fresh save2、停机、旧私有驱动及缓存目录恢复，
原 APK／宿主 ELF／模块／资源／shader chain 重验通过，清理无错误。本机完整
证据为 `turnip-original-q735-v1/actual-game-acceptance-agent.json`。

## PanVK 官方校验层：真机入口已核实

独立普通应用 UID 的工具包 `2a27b976` 在 Mali 真机通过 Android 系统 Vulkan
loader 与既有 libadrenotools DRIVER_CUSTOM 接入原 `c5ecf526` PanVK。实际
映射及 driverID20 已核实，没有 GPU_MAPPING_IMPORT，也没有驱动修改。
使用官方 Android Khronos validation layer 1.4.363；日志明确请求同步验证。

同一 UID10353／PID15895 中，原 NVN 路径的完整 GPU 写入再读取字节正确，
正例没有 VUID；随后刻意传入 size=0 的 VkBufferCreateInfo，官方层准确报告
`VUID-VkBufferCreateInfo-size-00912` 一次，并在调用驱动前阻断。不能仅凭环境
变量或可枚举层名称宣称成功，本次包括实际正负控及原生 terminal rc=0。

此工具没有执行游戏模块，不是 PanVK 游戏兼容或性能验收。原生产 `022d625`
APK、28 模块、六项资源、旧驱动及 shader chain 前后相同；独立包、进程、临时
目录和本次工具客户端已清理。实际记录为
`panvk-layered-vvl-tool-v1-mali-v1/result.json`。下一步用同一现成工具检查原版
PanVK 的真实游戏设备丢失，不将新增共享接口预设为根因。

## 36a→022 官方 CPU 对照的证据边界

两包已分别完成相同原摇杆路线后的官方 Simpleperf 8 秒 per-thread stat 和
8 秒、99Hz 用户态 DWARF 调用栈。每段采集后先 fresh save2、停机再传输，
两包间停机冷却 45 秒，最终恢复当前 `022d625` 并停止，原调试设置恢复。
记录为 `image-binding-36a-to-022-mali-cpu99-stat-v1/result.json`。

旧包 stat 窗端点温控目标 60→60，record 窗 60→30；新包两窗均 30→30。
PSO 数相同，但旧包 Uniform 变体从 31 变为 33，新包为 33。因此这不是稳定
温控下的净 FPS 对照，不能将显示差异归因于索引枚举改动。ADB 端点包围约
9 秒，而工具窗口为 8 秒，端点提交计数不能作为工具内精确帧数分母。
`shader_present.selected` 统计选择路径，不能称作成功出图数。两边实际
present_worker 均 reference=true／active=false，与 GPU 完成线程是不同职责。
后续使用精确 BuildID 的离线符号分析定位翻译 CPU 热点，不额外让手机空跑。


## 修改版 Turnip：共享启动与早期选角失败

高通用于激进共享内存实验，保留原 BC／四项 UI 资源及 `9507875007c6`
shader 链，输出 2750×1260、固定 0.75；不为此搬运 Mali 的整套资源。
原版 Turnip 对照后，改用既有 host-import `3c8710be`，没有混用两个驱动。

`c7a511b` 只在原 `022d625` 的冷启动阶段补齐 Turnip 对应 opt-in，并显式
开关 PanVK／Turnip 的导入请求。完整 canonical APK 与从原始 NSO 重新生成、
单独 verify 的 28 项部署文件绑定同一冻结源码。关闭共享的真实 ARM64 游戏
到达营地、人物／场景／UI 正常、fresh save2 后停止；一次官方 meminfo 的
TOTAL PSS 为 2,289,085KiB，只是此时快照，不是峰值或内存收益对照。

开启共享时，原 posix_memalign 的小启动探针导入失败，实际回退 legacy。
严格控制器拒绝把回退算作共享通过，存档停机并完整恢复原包及私有目录。
随后 `bcdf11a` 将此小探针改为与真实 guest heap 一致的 RW 匿名 mmap，
保留原 Vulkan API、能力门及导入失败策略，并记录失败阶段。该版真实
PID14638 启动字节检查通过，明确 actual_shared=1，不再是旧镜像路径。

加载选角期间仍触发既有保护：同一起始地址的新 BC3 sRGB 纹理（serial3841、
89,600B）与保留的 RGBA16F 图像（serial3290、98,304B）被判为 GPU 所有权
冲突。进程自行 SIGABRT，未为存档重新启动；生产 d7 APK、29 项模块、UI4、
shader chain、旧私有驱动及缓存目录恢复、清理通过。这不是 GPU timeout，也
不是营地通过；不能报告共享内存／FPS／散热改善。下一步区分逻辑纹理退役、
物理图像额外八提交宽限期及真实数据别名，不能将旧 HDR 数据写回覆盖新 BC3，
也不能直接删除保护。实际记录在本机 `turnip-shared-game-c7a511b-v1` 和
`turnip-shared-game-bcdf11a-v2`。

当前 `3c8710be` 只支持 Buffer host import，明确拒绝 host-backed VkImage。
纹理的直接共享与离线 native 排布仍是独立后继实验，不包含在上述通过范围。

## PanVK：官方 VVL 捕获到真实游戏的首个 API 错误

普通 Surface 路径下，系统 Android loader／校验层使用驱动的 Android native
buffer 导入接口，与原 direct ICD 的 Mesa 自有 WSI 不同。该诊断组合先遇到
`panvk_android.c` 的 INVALID_EXTERNAL_HANDLE，不能据此认定原 GPU hang
就是显示缓冲问题。此次官方 INFO 已明确列出 Core／Synchronization 等启用
项，但此前 draw 尚未执行，无 VUID 不能证明后续调用有效。失败完整保留，
原 `022d625` 和 35 项文件、旧目录已恢复，进程停止。

`7991333` 随后只在显式 layered-debug＋validate 冷入口保留真实 Surface 尺寸
而不 attach Vulkan Window，复用原 offscreen_present，首次读回之后不逐帧等待；
不是新渲染框架，也不是可玩或性能验收。原始 ARM64 游戏继续原菜单／选角
绘制，官方 VVL 首先报 `VUID-vkCmdDrawIndexed-None-06479`：片元 Dref 采样
绑定的 R16_UNORM 图像不具有 SAMPLED_IMAGE_DEPTH_COMPARISON 能力。其后
出现 kbase subqueue timeout 和 fence -4，实际复现设备丢失。

这提供了必须先修的明确 Vulkan 用法错误，尚不能证明它是所有设备丢失的
唯一原因。修复应核对 NVN 深度 format／view、比较 sampler 与 shader 映射，
不通过关闭比较、关闭校验或游戏 shader key 特判绕过。进程自行退出后，原
`022d625`、35 项文件、完整旧目录恢复和清理均通过；没有重启游戏存档。
本机证据为 `panvk-layered-offscreen-game-v1-mali-v1/native.log` 及其 result。

## Turnip：固定共享池已到真实营地，收益仍待同包对照

官方 LLDB 对真实失败现场的只读检查确认，冲突的旧 HDR 图像已经逻辑退役，
此前提交也已翻译，最后 GPU 使用序号 2307 已小于完成序号 2328；但原八次
提交的物理对象保留期尚未结束，别名表仍将旧地址判为该 HDR 的有效内容。
不能把这一保留期解释为新 BC3 仍须读取旧 HDR，更不能为绕过保护覆盖新数据。

`2f0d37d` 在翻译水位和 GPU 完成均满足时解除逻辑别名所有权，继续保留原
物理对象、描述符和相关作用域的释放条件。新 GPU 回归在旧实现实际触发
同类保护；修复后通过，并验证尚未翻译或 GPU 未完成时仍保留所有权。

同一修改版 `3c8710be` 驱动、原 BC／UI4／shader 链下，真实 ARM64 游戏
PID23854 已进入营地；截图中的地面、人物及 UI 未见明显异常。四个实际
导入池共 1,167,065,088 字节，即 1113MiB；当前池拷贝统计的扫描、镜像、
快照、上传及写回字节均为零。这里通过的是线性 Buffer 共享，纹理仍须按
现有 Vulkan 图像路径处理，并不意味着全部图像也直接共享。

一次官方 meminfo 的 TOTAL PSS 为 2,627,699KiB，而此前关闭共享的不同运行
为 2,289,085KiB；Graphics 分类则从 483,936 变为 246,744KiB。两个启动端点
不能作为稳定净收益对照，但足以说明目前不能宣称共享已降低总物理内存。
也不能用两张启动截图的瞬时 FPS 判断涨跌。下一步使用同一 `2f0d37d` 包、
相同路线的开关对照和官方 Simpleperf／内存报告，区分 CPU 工作量、映射
分类及实际驻留成本。此次 fresh save2 后停止并完整恢复原包、模块、资源、
私有驱动和缓存，清理无错误。证据：`turnip-retired-game-2f0d37d-v3/result.json`。

## PanVK：比较采样错误已修复，设备丢失仍复现

`c613662` 按 shader 反射中的 Shadow 类型处理比较采样。对于实际遇到的
R16_UNORM 颜色源，使用 GPU 缓冲中转，把相同 UNORM16 位值复制到 D16
比较采样图像；原生深度源保持原路径。首次使用或源实际被写入时更新，
未改内容复用已有图像；没有回读 CPU、关闭比较或按游戏 shader key 特判。

官方 VVL／同步验证下，原 NVN 调用的专项 GPU 检查通过 1574 项，包括
cube 六层、源位值保留、GPU 改写刷新和原生 D16；默认回归通过。负控强制
回到原颜色视图时，实际重现同一个 06479 并使检查失败。这证明修复命中了
该 API 错误。

冻结包随后已在同一原版 PanVK fork、原 BC、共享关闭的真实 ARM64 游戏中
复测。实际源为 128×128、六层、单 mip 的 R16 cube（serial44、descriptor754），
不能凭 packaged=0 认定是替代纹理。官方 Core／Synchronization 均开启，
06479 不再出现、VUID 列表为空，但仍出现原 subqueue1 timeout／fence -4。
因此比较采样修复成立，崩溃并未解决。原 `022d625`、35 项文件与完整旧
缓存已恢复，进程已退出；没有重启游戏存档，也未替换生产默认包。
证据为 `color-depth-comparison-c613662-mali-v1/result.json`。

后续先解读既有 Mesa 命令流转储。故障报告的 last job 不是挂起程序计数器，
应追踪实际 extract 到 insert 的待执行区间及 CALL 链；不能据此直接修改
提交同步，或把没有 VUID 当作 GPU 编译及驱动完全正确的证明。

## Turnip：同包共享对照及实际 CPU 大热点

同一 `2f0d37d` APK／驱动／资源的开关对照已完成。官方 8 秒 stat 中，NvN
task-clock 为 6302→6219ms，用户态 cycles 为 8.412→8.905G，内核态为
1.666→0.363G，上下文切换为 6255→310。关闭共享的窗口有 PSO／Uniform
变体编译变化，开启窗口稳定；不能将这些数值合成净 FPS 或单位帧收益。
同包 meminfo 端点 TOTAL PSS 为 2,356,125→2,701,559KiB。镜像消失与总内存
降低是不同结论，整池导入的驻留成本仍需处理。

随后只对开启共享、PSO／变体稳定的真实游戏做官方 Simpleperf 99Hz、约
8 秒用户态 DWARF 采样，使用精确 BuildID 符号化。NvN 共 577 个样本，其中
248 个、约 42.98% 位于线性 Buffer／图像别名保护。实际链为
`pool_address → shader_bindings_selected_impl → vk_draw_record`。LLVM 反汇编
确认其遍历 `shared_gpu_aliases`，逐节点读取 initialized，并重复查询固定
共享模式；这是地址重叠查找，不是纹理内容哈希或逐字节比较。

本窗口脏页／CPU 观察命名函数约 1.04%，图像转换／上传约 0.35%，只描述
稳定营地采样，不能外推首次加载／新场景，也不含内核态。这修正了“镜像
计数零就代表纹理跟踪已消失”的错误推论。池计数没有覆盖全部纹理处理。

后继 `7c60ff4` 已实现确定性的平衡区间索引，保留真实地址重叠、写入、退役
和原错误保护，减少每次 Buffer 访问的全链遍历。真实游戏结果见下节。
同一代码也适用于 Mali 共享模式；当前 Mali 生产包共享与旧 ASTC 复制实验
均关闭，普通 Buffer 访问不进入此扫描，不能将该 43% 归因于当前 Mali。
证据：`turnip-shared-2f0-on-cpu99-official-analysis-v1/final-hotspots-agent-v1.json`。

## Turnip：直接共享纹理的独立 App 检查已通过

`aa185374` 实验驱动已在普通 App UID 上通过 ASTC4／6、UNORM／sRGB 四种
格式、四级 mip 的导入及 GPU 采样检查。bake 的 280 项仅检查原 host-image
copy 排布和文件合同；后续 import 的 202016 项包括实际 GPU 采样、元数据
和哨兵检查，不是片元数量或性能数据。实际驱动 maps／完整 SHA 和三项 UUID
均核实；原游戏始终未启动、未修改，独立包已卸载。

这尚未把游戏的大部分材质转换为直接共享纹理，也没有功耗或内存收益证据。
下一步复用原资源加工及 NVN family 检查，先闭合一张真实火盆材质，不能将
PanVK 的 opaque payload／offsets 用到 Turnip。证据：
`turnip-host-image-app-v2-q-actual-evidence.json`。

## Turnip：别名区间索引的真实游戏采样

`7c60ff4` 使用同一 `3c8710be` 驱动、原 BC／UI4／950 shader 链，在普通
应用 UID 的原始 ARM64 游戏中完成选角、营地和原触控路线。共享确实开启，
地面、角色、建筑、火盆与 HUD 截图未见明显异常。没有启用 GPU 抓帧层。

官方 Simpleperf 同为 99Hz、约 8 秒用户态 DWARF 采样。将新索引查询和资格
判断函数也计入同一类别，别名保护的 self 样本从 `248/577`（42.98%）降到
`19/451`（4.21%）。这是同类 CPU 热点显著缩小的证据；不是精确的每帧耗时、
FPS、带宽或散热收益。两次窗口内 PSO 均稳定，但窗口间 PSO 数不同，也没有
记录内核 CPU；不能用截图的瞬时 FPS 把此结果解释成整帧涨跌。

每个图像新增 64 字节索引元数据及冷路径插入／删除维护，不新增图像内容
快照。原地址重叠判断、尚未完成时的保护、逻辑退役和八提交物理保留期
维持不变。真实游戏退出前完成原存档，之后恢复原 APK、模块、资源、驱动
和缓存，进程已停止。证据：
`shared-alias-7c60ff4-actual-game-CPU99-acceptance-agent.json`。

## Turnip：只用于图像 CPU 转换的池不提前注册

独立候选 `0b36faa` 包含上述索引，继续处理整池导入的驻留成本。图像 CPU
读取准备不再顺带创建整个池的 VkBuffer；真正的线性 GPU Buffer 访问才注册。
共享堆改用同一 memfd 的 guest 与内部 RW 两个映射，guest 保留原脏页写保护，
驱动通过内部映射注册相同物理页，避免首次 Buffer 访问时临时解除 guest 保护。
第二个虚拟映射不复制池内容，也不代表所有纹理都已直接共享。

冷启动两页 GPU 检查复用同一映射构造，先将 guest 设为只读，再导入 RW
映射、执行原 copy／fill 并从 guest 核对字节。host 真实堆、原 NVN GPU、
原保护负控及异步／串行回归已通过。冻结的完整 Android 包随后在 Adreno735
原游戏中完成同一 guest-RO 探针、实际 shared=1、营地和五段原触控走位，
画面检查通过；完成两次原存档后停止，生产 APK、资源、驱动和缓存精确恢复。

本次营地仍实际注册四池、共 1,167,065,088 字节；没有减少导入字节。官方
meminfo 的 PSS 为 2,777,433KiB，不同运行的旧 7c 端点为 2,747,035KiB，
不能当作受控内存对照。两个 memfd 映射已按相同设备号、inode 和文件偏移
核对，合计内核 PSS 为 2,169,068KiB；不得把两份 RSS 当成两份物理副本，
也不能遗漏导入视图的 PSS。线性池拷贝计数均为零，但不包含纹理转换成本。

机制只对未被线性 GPU 访问的图像池省掉提前注册；当前真实场景的混合大池
最终仍由 Buffer 访问整池注册，因此不能报告内存、发热或 FPS 改善。下一步
沿既有 operation admission 和 canonical span 收窄共享注册范围，不通过恢复
镜像或删除数据依赖绕过问题。原 ASTC 传播对未注册池仍保守回退，不能宣称
覆盖及性能不变。此候选未替换可玩版本。详见本机
`shared-image-cpu-only-0b36faa-actual-q-acceptance-agent.json` 及
`shared-image-cpu-only-0b36faa-root-actual-review.json`。

### 整池注册的首次触发已经用 LLDB 确认

同一冻结 `0b36faa` APK、原游戏及 `3c8710be` 驱动，官方 LLDB 在实际导入
调用点取得四个首次导入池的完整栈、NVN flags、pool 地址／大小和
`pool_address` 的原始请求。四次 `backings` 均为空：

| 整池容量 | NVN flags | 首次请求 | 触发路径 |
| --- | ---: | ---: | --- |
| 400 MiB | 34 | 3968 B | B2T 的线性上传源，池内偏移 512 |
| 710 MiB | 33 | 24064 B | B2T 后的 canonical retile，池内偏移 17978368 |
| 2 MiB | 18 | 16 B | 原 GPU 查询结果写入，池内偏移 528384 |
| 1 MiB | 20 | 32 B | Compute storage binding，池内偏移 816 |

这四次请求并没有扩张成 `pool_remaining`，整池大小来自
`shared_pool_materialize` 的 `p->size`。首次范围不是后续所有 GPU 访问范围，
不能据此只注册这几个字节便视为完成。特别是 retile 路径维护图像与线性
内存的别名写入语义，不能删除其实际 GPU 写入来降低注册容量。

窗口候选须复用操作预留、物理 span、真实完成及重收集流程，预留完整
descriptor／VBO／IBO 和 retile 目标及其 storage 对齐前缀；跨窗需求必须由
一个连续 VkBuffer 满足。后续候选及分阶段验证见下节。
LLDB 退出后原营地、存档、停机与完整生产恢复均已闭合，工具服务和端口
转发已清理。证据：`shared-image-cpu-only-0b36faa-materialize-debug-v1-analysis-agent.json`。

## Turnip：共享池按实际 Buffer 范围注册

独立候选 `eddb4ca` 在 `0b36faa` 的同一 memfd 上按需注册连续窗口，不创建
镜像或内容快照。新范围无重叠时直接补充；跨越旧窗口的完整 binding 必须先
完成已有 GPU 和 CPU effects、清理旧引用，再将必要范围合为一个 VkBuffer。
合并不搬运内容，但会等待并重新注册，不能称为无额外成本。既有 operation
admission 在借出物理句柄前收集完整依赖；不通过截短 descriptor 或忽略 retile
写入来减少注册量。旧节点退休后的真实内存依赖保留。

源码审查还修正了 CPU 观察者与首次窗口发布之间的竞态：在 resources 短锁内
取得 backing 存在性和属性快照，在锁外进入完成等待。五组精确版本的 host
GPU／失败回退检查通过；999 个构建输入、1749 个实际 NDK 依赖、APK 六库及
从原始 ExeFS 生成的 28 项部署产物已独立核验。旧 `982487c` 包只留作已知
竞态的参考，未部署。最终闭合记录为
`shared-import-windows-eddb4ca-root-closure-review.json`。

Adreno735 同一 APK、驱动、原 BC/UI4/950 资源、固定 0.75 倍率的营地对照
已经取得两个官方 meminfo 端点：

| 模式 | 活跃共享注册范围 | TOTAL PSS |
| --- | ---: | ---: |
| 整池，窗口 0 | 1113 MiB | 2,770,623 KiB |
| 按需，窗口 16 MiB | 499 MiB | 2,193,647 KiB |

观察到注册范围少 614 MiB、总 PSS 端点少 563.45 MiB。两次均执行原始 ARM64
游戏、原营地及五段触控走位、原存档后停止。窗口版还通过冷启动重叠导入的
双向 GPU 字节检查。第一轮窗口走位因脚本 5 秒时限未完成而保留为失败；仅
窗口补测将每段时限放宽至 15 秒，目标、半径和输入方式不变，未重采整池。
因此这仍是顺序端点比较，不是完全匹配的因果或峰值内存测试。

两个 memfd 映射仍须合计内核 PSS，不能把两份 RSS 当物理副本；活跃注册
范围也不等于唯一物理 RAM。实际双映射 PSS 为 2,164,444→1,525,728 KiB，
主要差异位于导入视图；宿主 malloc PSS 增加 23,960 KiB，Graphics 分组增加
37,660 KiB，不能忽略这些成本。线性池比较、镜像、快照及搬运计数均为零，
不包括纹理转换。原生产包、模块、资源、驱动和缓存已精确恢复，进程停止。

当前原 BC 真游戏检查不能代替原生 ASTC 窗口传播验证，更不能证明持续游玩
散热或 6GB 设备已经达标。原始工具输出、两份 VMA 的 inode/offset、相同
场景坐标及完整恢复已再次独立核验；记录为
`shared-import-windows-eddb4ca-q-root-actual-review.json`，分组数据为
`shared-import-windows-eddb4ca-q-pair-memory-analysis-agent.json`。

随后独立普通 UID 包复用原 NVN 窗口和 ASTC 用例本体，在同一 Adreno735 的
真实 memfd 堆、64KiB 窗口和 `3c8710be` 驱动上通过。实际 ASTC4 物理编码、
fresh BC3 array 转为 ASTC4、mip/layer/ROI 保留及 canonical 字节不变均命中，
不是 Lavapipe 的 unsupported 分支。终端 rc0；驱动两种私有路径按实际文件
设备号、inode、大小、SHA 及同一活进程 maps 核对。独立包与临时文件已清理，
原生产包和文件未变。记录为 `shared-window-android-oracle-v3-root-actual-review.json`。
前两轮仅工具路径后置条件失败，原结果保留；没有因此修改原 GPU 字节断言、
Java/JNI 包或渲染代码。这补齐独立 ASTC 路径覆盖，仍不是 ASTC 真游戏或
持续游玩验证，也没有新增 Android VVL/SYNC 或性能结论。

## PanVK：完整 API 重放已复现原 GPU 挂起

未修改的 kbase fork 驱动 `c5ecf526` 在关闭共享的原游戏中也会挂起。
官方 GFXReconstruct 已从 vkCreateInstance 开始取得完整启动前缀，没有
裁剪快照重建；独立普通 UID 重放不运行游戏模块和 NVN 翻译线程，仍能复现
最终第 186 次提交的 DeviceLost。Core 和同步校验没有 VUID，不等于已经
证明所有 Shader 或调用语义正确。

同一捕获、APK 和驱动，官方 `--sync` 的逐提交等待，以及再加
`--serialize-render-passes` 的应用绘制执行屏障，均未消除该最终提交的失败。
末次提交虽没有应用 Dispatch，但 52 次 B2T 全部上传七张 BC3 图像；原驱动
内部实际执行 compute raw copy 和片元 BC 解码。应用层计数和执行屏障不能
假定覆盖这些驱动内部调用，不能据此断言不存在 compute 或内部依赖问题。

最后一轮实际记录为
`panvk-gfxr-renderpass-serialize-v1-mali-v1/actual-independent-review.json`。
所有测试工具已退出、独立包已卸载，原生产包、资源及 46,011 项缓存核验未变，
Mali 游戏保持停止。接下来只针对原驱动现成的 BC 解码路线做合法对照：
路线变化会改变图像内存要求，必须先用官方 replay 的内存重绑定建立共同
基线，不能拿旧 allocation 直接套用，也不能把分配方式和解码路线同时变化
冒称单变量结果。

首次 gfx/rebind 实测还暴露了官方 SDK 的能力缺口：重放器给缓冲加入
SHADER_DEVICE_ADDRESS，但捕获设备未启用 BDA，出现真实
`VUID-VkBufferCreateInfo-None-09499`。因此本次不能作为合法解码路线对照，
compute 轮被拒绝、未执行。原包和完整库存的只读复核及属性恢复均闭合；
被 SIGINT 打断的首次收尾核验保留在原记录中，补充只读核验单独归档。
记录为 `panvk-bc-rebind-gfx-v1-mali-v1/actual-independent-review.json`。
需先解决现成工具的 feature/allocator 配置，不能据该 VUID 宣称游戏或驱动
新增回归。

## 共享窗口的官方 CPU 对照（2026-10-20）

同一 `eddb4ca` 二进制的整池与 16MiB 窗口分别采集了官方 Simpleperf 和
Perfetto。Simpleperf 的 8 秒窗口内，翻译线程 task-clock 为
5608.93→5772.78ms，完成线程为 316.18→305.27ms。两个模式的 PSO/变体
分别是 173/31 与 181/33，故这里只记录绝对 CPU 工作量，不能将差值写成
窗口管理造成的整帧退化。Perfetto 是独立采集窗，不能与上述时间相加。

窗口版 605 个翻译线程用户态样本中，准入调用链 inclusive 命中 44 个。
已有 fast pin 会跳过稳态 reserve、分配和排序；余下包括真实范围收集、
锁和覆盖查找，不能把这 44 个样本全算作可删除工作。另有 29 个 self
样本位于图像别名 AVL 重叠查找，提示继续减少不相交子树遍历；这与
vickwv 的脏区定位缓存是两处不同的开销。报告为
`shared-import-windows-eddb4ca-cpu-cost-v1-official-analysis.json` 和
`shared-window-eddb-steady-cpu-candidates-readonly-v1.json`。
采集后高通生产包、缓存和资源已恢复，进程停止。

## 原生图像的独立有限导入：Android 验证中

候选 `bb4ac54` 基于 `eddb4ca`。每张原生 VkImage 独立导入 payload
对应的原 memfd 页，图像绑定的 VkDeviceMemory 不随 Buffer 窗口 union
改变。长期图像引用与短期提交引用分开，保留原 NC 缓存、写入保护和
真实数据依赖；物理图像退休后才回收这份导入。没有额外纹理 payload
副本，但目前仍有与 Buffer 窗口重复的导入登记及缓存维护成本。

源码及实际依赖独立核验已闭合，七组 Lavapipe/VVL/SYNC 功能检查通过，
包括真实 RGBA 图像与两次窗口 union 的双向别名读写。Lavapipe 不支持的
ASTC 用例没有计为通过。此候选尚未完成 Android 真 ASTC 或真实游戏验收，
不代表已改善帧率或降低带宽、内存和温度。记录为
`native-image-window-bb4ac54-root-source-review.json`。

随后使用独立普通 UID 包 `8eee0ad` 执行未改动的原 NVN 用例，在真实
Adreno735/AA185 上通过一次完整验证：ASTC4 sRGB 的九级 mip、五级 tail、
双向别名读写、两次窗口 union 保持绑定及最后释放均正确。实际独立 import
为 135,168B，token 元数据 120B，未增加另一份纹理 payload。
原生产包和文件不变，独立包、进程及临时文件已清理。原始结果和 live maps
再次独立核验，记录为 `native-image-window-app-v1-q-root-actual-review.json`。
这次内存类型为 coherent 的 `0xf`，不能据此宣称实证了 NC 硬件维护路径；
私有 ICD 下 VVL/SYNC 明确未开启，也未运行真实游戏或采集性能。

配套 `ba0b943` 工具已生成一张真实火盆材质的原生资源 overlay，其余
原 BC 资源和上游 UI 保留。使用原 CASC 工具验证整个 VFS，仅该材质与
总贴图缓存两个条目变化；其余 31,071 个缓存条目和 133,198 个 VFS
条目不变。错误驱动、错误验收指纹、损坏布局表和已有输出均被拒绝。
工具、源码指纹与重建参数已保存；没有部署，不能把资源加工成功当作
原游戏已经使用它。记录为
`turnip-one-native-overlay-ba0b943-source-ready.json`。

PanVK 官方 replay 的 BDA 配置仍未闭合：最小调试桥接启用设备 BDA 后，
09499 消失，但 SDK 的内部 allocator 未同步启用相应分配标志，转而出现
`VUID-vkBindBufferMemory-bufferDeviceAddress-03339`。本轮立即停止，
未运行 compute 解码对照；不能据此评价解码路线。原生产库存、属性及
工具清理已经独立核验，Mali 停止。记录为
`panvk-rebind-bda-gfx-caf392e-mali-v1/actual-independent-review.json`。

## 原生图像：真实游戏单材质接入通过，尚未批量推广

`bb4ac54`、AA185 驱动与原 BC／上游 UI 基线的组合，已在 Adreno735
原生执行真实游戏模块。游戏程序从原始 ExeFS 经同一入口重新生成并独立验证，
没有叠加游戏 binary 补丁。官方 LLDB 实际取得 135,168B、4KiB 对齐的
完整九级 mip 图像绑定；资源表、三项 UUID、输入及 payload 指纹均匹配。
随后实际 B2T 更新的是 mip 4 的 16×16 区域，源行跨度为 256B。

同一 PID、图像 serial、分配 token 后续出现 FRAGMENT／SHADER_READ。
其真实调用栈是 `vk_texture_shader_record → vk_draw_record`，位于 Draw
录制之后。精确 canonical ELF 的反汇编确认所有进入该区间的路径都已经
调用 `vkCmdDraw` 或 `vkCmdDrawIndexed`；当前批次的 begin／abandon／record
清理规则也排除了准备重试或旧批次残留。它证明这张共享图像参与了真实
游戏的已录制 Draw，不能据此指认 GPU 上某条采样指令或所有 mip 的像素。

原工具只接受较早的 texture-view 调用栈，因此仍保存其失败结果；没有
改写原状态或重采。补充分析使用同次捕获和官方反汇编闭合上述合法路径。
两张原 HUD 截图及三段正常摇杆走位未见明显黑块或几何错误，但只是有限
视角观察。原生命周期存档两次后停止游戏，原 APK、全部六个私有目录及
shader chain 已直接核验恢复。根验收记录为
`native-one-game-bb4ac54-q-v3-root-functional-acceptance.json`；原控制器
结果保留在 `native-one-game-bb4ac54-q-actual-v3/result.json`。

这次调试器断点会扰动运行，截图的 FPS／电池温度不能当性能或持续散热
对照。目前只接入一张 135,168B 材质，不能声称全贴图共享或 6GB 已达标。
下一批选用现有 ASTC4 的 Act1 环境及人物资源，排除 terrain-array 后有
6,217 项，原 NVN 打包 payload 合计 414,950,400B；这不是驱动原生图像
大小，也不是预测可节省的 PSS。批量加工将复用原 producer、packer 和
CASC，逐条生成与释放工作集，再做真实批量运行及现成工具的内存／热验证。

另一个提前 acquire 候选 `c78704e` 尚未部署。审查发现首次 FS acquire
未必将依赖传到稍后读取未用 mip 的 Compute／Transfer，且独立性范围需
覆盖真实导入区间及别名粒度。修复在独立后继中进行，不能以主机原测试
通过代替这两个缺口的处理；当前游戏验收使用的是原保守同步版本。

## 2026-10-21：批量资源的真实复制阻断与同步后继

6,217 项已由同一 AA185 驱动、8064 producer 实际完成 49 批烘焙，原
packer/CASC 生成 native 与普通 ASTC4 两套同选集、同质量资源。原 NVN
payload 为 414,950,400B，native 为 696,385,536B；主要新增成本是
1024B 最小行跨度及小 mip 填充。压缩后的资源 archive 只增加约 3.1MiB。
展开体积既不是实际驻留量，也不能单独证明取消 GPU 副本后的净 RAM 收益。
原 24,855 项未选缓存及上游 UI 保留，所有构建记录仍不声明批量游戏通过。

同一 `bb4ac54` runtime、AA185、16MiB Buffer 窗口、固定 0.60 和
2750×1260 的真实游戏对照，普通 ASTC4 control 进入营地后即触发已有
ASTC copy 保护；native 阶段没有运行。随后官方 LLDB 只在拒绝点停一次：
目标是已有 GPU 写入的 128×128、42 层、7 mip BC3 数组，本次 ASTC
源要写 layer37/mip0。目标已有内容，不是缺 import/span 的未定义存储。
不能直接把全数组换成 ASTC，也不能跳过复制；兼容后备需保留其它层内容
及现有 CPU/别名保护，并单独核算可能增加的 decoded 图像内存。
原失败、typed 快照、调试器清理与生产目录恢复记录均保留在
`native-bulk-copy-bb4ac54-diagnostic-v1/`；不把此调试结果当 FPS 验收。

同步后继 `f637327` 已改为在 pending primary 上为已知旧 Buffer writer
建立原有 global memory dependency，再执行 native 图像 acquire；当前
recording、未知 writer 和 private optimal alias 仍保守拒绝。真实 AA185
的 ASTC4 九级 mip 测试通过新增 40 项及总计 412 项检查，两个正例要求
像素实际改变，并覆盖后续 Compute/Transfer 的不同 mip。测试包装曾因
宏未接入而只运行旧 372 项，该失败原样保留；修正后核对真实编译宏、
预处理体、NDK 依赖和最终 ELF，才执行新的真实 GPU 检查。当前仅证明
这个有限物理数据依赖场景，不是批量游戏或 NC 硬件、温控、6GB 验收。

Mali 的 single-CSG 驱动已由原官方完整 GFXR prefix 实际验证：确认新
驱动及三流同组后，仍在 API149328／submit186 报 DEVICE_LOST。此候选
未修复原挂起。完整 MineGraph703db fork 正独立构建；它还改变了 BC
图像布局和内存类型缓存属性，不能直接用旧 `none` 重放判定其好坏。

### 后续：定义过的 BC 数组与完整 PanVK 的平台入口

`56c91f7` 对上述混合复制增加独立的 decoded RGBA 后备：保留逻辑 NVN
描述，先在 GPU 上保留目标所有 mip/layer，再写当前复制区域。原有未定义
存储采用 ASTC 的规则不变。目标必须没有其它已定义别名，且完整逻辑范围
具有 CPU 版本证明；同值 CPU 写入也会使证明失效。无法正确表达原 BC
字节的 CPU 读回、原始 Buffer 访问及缓存出口仍明确拒绝，不能返回假数据。

主机 BC→RGBA 的 mip/layer/ROI 像素与负例已通过，独审未发现源码阻断。
Android canonical 的 1,008 项 producer 输入、1,759 项实际 NDK 依赖与
六个库已独立核验；原 NSO 已重新加工并执行单独 verify-only。ASTC 的
真实高通复制及这张 42 层数组仍待验证，不能写成原游戏已修复。后备图像
的有效 texel 存储约为 BC3 的四倍，迁移期间还短暂保留旧存储，须记入
批量资源内存对照，不能只统计共享导入节省。

整批资源的驻留实验已接入官方 LLDB 单次只读快照：普通路线结束后，
读取现有四个全局计数并立即 detach，再取系统 meminfo；不增加 runtime
计时器、逐帧日志或 payload 扫描。import 地址范围与元数据字节不能冒充
驻留 RAM，也不能单独与 PSS 相加。工具故障仍走原存档/停止路径，替代
PID 不会被停止。实际配置已通过离线检查，但尚未获批量游戏验收。

完整 MineGraph703db 已完成 native Android 构建，并在原 Mali APK、原
full ASTC4、固定 0.60、shared/legacy-copy 均关闭的条件下实际启动原
ARM64 游戏。首进程 13500 在加载 ICD 时明确失败：应用 namespace 无法
加载依赖 `libhardware.so`。尚未执行 GPU 渲染，不能将其算作新 PanVK
GPU 崩溃或 BC 错误。原包、资源、shader、PSO 及目录均恢复，游戏停止。
下一步仅修正可选 legacy gralloc 探测的平台链接依赖，保留明确的
unsupported 返回与公开 AHardwareBuffer 路径，不伪造 HAL 成功。

### 同日实际后续：批量游戏已运行，净内存目标尚未实现

高通 AA185 的独立普通 UID 包实际通过两个冷启动：原 42 层数组用例
保住 layer0 并正确写入 ASTC layer41；另一个 3 层、3 mip、局部 ROI
用例通过 4,100 项检查，覆盖其它 mip/layer 与区域外像素。两次均卸载
测试包、清理工具且不改生产包；没有将主机不支持的 ASTC 分支冒充通过。

随后同一 `56c91f7`、AA185、16MiB 窗口、固定 0.60、2750×1260 的
普通 ASTC4/native 6,217 项真实游戏对照完整结束。两端都进入原营地，
完成三段正常摇杆走位并原生命周期存档两次后停止，原生产目录与包恢复。
有限截图未见明显黑块或几何缺失；终点相差数格、相机时机不同，不能
当成相同帧或全游戏视觉验证。调试器暂停后的截图 FPS 不代表吞吐率。

| 一次营地端点 | 普通 ASTC4 | native 布局 |
| --- | ---: | ---: |
| 系统 TOTAL PSS（KiB） | 2,158,504 | 2,224,214 |
| 系统 Graphics PSS（KiB） | 281,920 | 264,448 |
| 实际 native image 数 | 0 | 1,290 |
| native import 地址 envelope（B） | 0 | 148,512,768 |
| 普通 Buffer window backing（B） | 523,239,424 | 590,348,288 |

这次 TOTAL PSS 增加 65,710KiB，不能宣称原生布局已省内存。Graphics
属于总数的一部分，import 地址也不等于新增物理 RAM，不能相加推断。
窗口 backing 恰好多 64MiB，且路线开始前已经出现该差异。源码还会为
已有独立 image import 的纯纹理消费者强制取得普通 Buffer span，值得
分离这项非必要物化；其与驻留增长的因果关系仍需修复后的实际对照。
CPU cache、别名历史和真正的 Buffer 消费者不能随之删除。批量 native
仍属未完成实验，不默认以省内存名义启用；持续散热与 6GB 验收尚未完成。

完整 PanVK 静态链接现有 unsupported HAL 后，实际已越过 namespace
并查询到新驱动，但原022/full ASTC4 随后拒绝 ASTC→BC3 的无编码转换。
新驱动支持 BC 采样，改变了原来系统 Mali 自动采用 RGBA 的能力分支；
这是资源包兼容路径缺口，不能当成 DEVICE_LOST。原包与 PSO 已恢复并
停机；下一步以有来源、UI 相同的原 BC 资源确认这套驱动的绘制基线。
