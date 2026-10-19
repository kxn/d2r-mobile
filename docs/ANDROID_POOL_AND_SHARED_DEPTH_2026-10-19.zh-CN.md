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
尚未取得该缓存的 Android CPU 时间或帧率改善数据。

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

下一步只用相同包、相同 shader 链的普通路径、子集 input attachment、共享
深度三种模式，检查原厂 fragment job、task、processed tile、外部读写和周期。
这些计数器含义不同；单个 draw 的区间时间不能作为 TBDR shader 成本分摊依据。
是否默认启用须等待真机画面与原厂数据。
