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
