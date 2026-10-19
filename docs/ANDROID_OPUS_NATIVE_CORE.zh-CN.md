# Android 可选原生 Opus core

Android Activity 和标准启动脚本默认选择 native core，启动前固定选择、对象生命周期内不动态切换。可用 `--no-native-opus` 或 Android `native_opus=false` 保留原 SDK 对照；`--opus-shadow` 则只镜像校验、仍输出 SDK 音频。宿主库独立使用时未设置 `DM_ANDROID_OPUS_NATIVE=1` 仍为 SDK。环境判断仅首次构造/首次 math 入口缓存。沿用原 HardwareOpusDecoder HLE，并覆盖原 FMOD 已有的 Software OpusDecoder 命名构造入口，复用同一适配器；无游戏指令地址、游戏 binary 或声音时钟修改。

## 边界

原 SDK 的构造、Init、工作区、包头处理、输入长度、样本数与 PCM 容量检查、错误映射、延迟 Reset 和原 SDK global mutex 全部保留。只把受控对象的 core `nncodecLibopus_opus_decode` 和 `opus_decoder_ctl(OPUS_RESET_STATE=4028)` 转向独立 libopus 1.5.2 fixed-point state。Init 仍执行原 SDK；其受控 scope 捕获原 `opus_decoder_init` 的真实 state 参数，成功后才尝试 native 创建。无 state ABI 复制。Init 捕获缺失、多个/不一致 state、不支持 rate/channel、重复 state 或 host 栈不可用，在首包前保留原实现，直到下一次成功 Init 才重新选择。

一旦 native 被选定，直至 Finalize/成功 Init 不换回未推进的 SDK math。失败 re-Init 不解除旧关联：已核 SDK 在非法 rate/channel 时返回 8、于首次 state 写入前拒绝并保留旧 getters，候选也保留 native 解码历史，允许直接继续解码。其他失败保留关联并 fail-closed，直到真正 Reset/成功 Init；不能把失败 Init 当成历史重置。原 wrapper scope、关联 state、FEC=0 与 Reset=4028 必须匹配。异常关联调用返回 `OPUS_UNIMPLEMENTED` 并使该对象停用至明确 Reset/Init；未知 ctl 不经 C varargs 猜测转发。原 SDK 返回错误不切换旧状态。尚未关联的 state（含多流内部调用）原样尾转发原 SDK。针对本 SDK 的静态符号/重定位和调用约定证据位于 `.local/logs/opus-math-hle-v1-static-evidence.json`、`opus-math-hle-v1-decoder-api-inventory.json`。

所有 10 个虚表槽验证及 guest 所有权保持。D0/D1 先移除旁表并恢复原 vptr，再执行原析构；原 D0 独自释放 guest self。全局锁不覆盖原 SDK/native math，也不等待每对象锁；两个 decoder 不因候选新增全局串行。纯 host math 使用已经验证的 host-stack 桥，不能回调 guest 或 nonlocal unwind。

## 成本与计数

每对象 host 元数据为 264B（NDK DWARF），比 shadow-only 增加 40B；另有一个 native codec state，native 模式不分配 shadow 的 23,072B PCM 对照区。没有每包分配、线程、时钟或采样器。每包保留对象借用/锁、SDK wrapper，并增加透明入口的寄存器保存及受控 scope 判断、host 栈桥和低频功能计数锁。SDK/default 原 vptr 不动，但三个公开 math 入口在首次解析后新增固定 acquire-load/分支/尾跳；shadow 模式同样走这个透明尾跳，不执行 native 输出。

`native_packets` 只在原 wrapper 成功且恰好一次 native core 调用成功、count 相等后累计。`native_samples` 是每通道 sample 数，`native_scalar_samples` 另乘 channels；不能只拿包数比较不同时长音轨。`native_selected` 是成功选择次数，不是 live 对象数；`native_resets` 是真正执行的 4028 次数。`tracked_sdk_packets` 仅覆盖启用候选时被包装的对象，默认原 SDK 对象未计数。日志最多 64 条，native 解码仅首包/第 256 包报告，不增加性能计时。

## 验证状态与入口

owned SDK mock 的普通/ASAN/UBSAN fixture 已通过，覆盖持续 300 包、错误/capacity、Reset、重新 Init、首包前回退、state 不匹配和未知关联 API fail-closed。它验证机制，不能作为真实 SDK 音频验收。此前真实 SDK shadow 1024 包全部误差不超过 1 LSB，见 shadow 文档；这不是正式 native 输出通过。

ARM 尾 stub 已在 Q 验证 default/lazy/unowned，两条透明尾路保持 x0–x8、q0–q7、x18 和原 SP，含真实 C varargs 10 个整数、两个浮点与指针。证据 `.local/logs/opus-native-v1-tail-q-arm.log`。

实际 SDK oracle 使用原始模块的已核离线改写产物，由原 loader/HLE 原生执行；不调用 nnMain、模块游戏构造器、NVN 或世界。它通过真实 SDK packet setter 处理官方 host encoder 生成的 owned 数据，比较原 SDK 与候选的 8/16/48k、mono/stereo、2.5–60ms、Reset、长度/容量错误、析构和工作区/输出 guard。新入口：

```
native_opus_sdk_runner ./libnative_opus_sdk_fixture.so VERIFIED_MODULE_DIR
```

oracle 必须为 SHARED 并经 `dlopen` 运行，与 APK 动态 TLSDESC 一致。旧 PIE oracle 被 LLD 把 descriptor 地址表达式 relaxation 成 local-exec 零 offset，触发 loader 原有键页拒绝；这是 fixture 引导失败，未执行 SDK 对照。旧证据保留 `.local/logs/opus-core-v1-real-sdk-q-arm.log`。不得放宽 loader 或更改游戏产物规避。v2 新产物及源码哈希见 `.local/build/opus-core-v2-sdk-oracle-candidate/manifest.json`。

Q 真正 ARM SDK oracle v2 已通过：186 cases，72 success、114 errors，59,400 PCM scalar，3,779 个不同 scalar，最大误差 1 LSB；native_selected=6、native_packets=72。原 SDK packet setter、wrapper、Reset/error/guard、Finalize/D0 实际执行，nnMain=false、module constructors=false。证据 `.local/logs/opus-core-v2-real-sdk-q-arm.log`。这只覆盖该有限 owned packet 矩阵，不宣称全兼容。后续仍需实际游戏声音/Reset/生命周期回归，再由 root 执行受控 SDK/native 同包官方 whole-PID CPU 对照。局部 math 替换的目标是减少 guest 重写/TLS 与 codec CPU 工作，不预报 FPS、温度或功耗收益。

## failed re-Init 修复记录

独立审查发现初版在失败 Init 后也清理 native 关联，可能回到未推进的 SDK state；这是真实 P1，初版不启用 native。v3 将失败路径移到解绑前，保留历史或明确 fail-closed。owned mock 已改为失败前不 Finalize，并验证 invalid rate/channel 后连续 PCM、未知失败后不达 SDK math、Reset 恢复。实际 SDK oracle 新增先推进三包、44100/3ch 两次失败后不 Reset 直接解码、最后 Reset 对照；新冻结入口 `.local/build/opus-core-v3-sdk-oracle-candidate/manifest.json`，此前 v2 186-case 结果不能代替该新增序列。v3 已在 Q 真正 ARM SDK 上通过：216 cases，102 success/114 errors，62,100 PCM scalar，最大误差 1 LSB。native_packets=102 恰等于全部 success，确认新增失败重 Init 后的每次成功解码仍走 native。证据 `.local/logs/opus-core-v3-real-sdk-q-arm-result.json`。

## 软件构造入口覆盖

官方 whole-PID native-B1 留下一个原 SDK 音频工作线程。现有 callchain 中一条明确经过未关联 state 的 `dm_opus_decode_entry`；64/65 样本没有 guest 调用栈，不能把所有剩余工作都宣称来自某个构造器。三模块实际 import/静态调用和原 FMOD factory 证明同时存在 HardwareOpusDecoder 的 64B 与 Software OpusDecoder 的 48B 构造分支。此前仅包硬件构造，软件分支遗漏；证据见 `.local/logs/opus-core-v2-remaining-sdk-readonly.json`。

软件构造现在增加同一命名 HLE，仅调用真正原 SDK ctor 后复用原 adapter、10 槽校验和 Init state 捕获。硬件接管内部仍通过 `guest_sym` 直调真正原 ctor，避免经过新 force 入口二次 attach。默认模式不变；不接管 multistream、不改变解析或加载的游戏产物。额外成本仅发生于软件构造时：缓存配置检查；启用时分配已有旁表/锁/native state。没有新增每包锁、表结构或 math 路径成本。

Q 原生 SDK 新夹具按两构造入口、8/16/48k、mono/stereo 各 6 组执行，软件/reference 用真实 48B guest allocation。SDKdefault：432 cases/204 success/228 errors，124,200 scalar 逐位一致，原 vptr、0 attach/0 native。Native：同 432 cases，12 selected、204 native_packets 恰等于全部 success，7,558 differing scalar、最大 1 LSB；failed re-Init 后连续解码、Reset、容量错误、工作区/PCM guard、Finalize/D0 保持。`attached==12` 同时验证硬件调用原软件构造不会双 attach。

证据 `.local/logs/opus-soft-ctor-v1-q-{sdk,native}.log`；两进程结束后核实无 fixture/game 进程。实际三模块及两个独立测试二进制的设备 SHA 与原 verified manifest 核对一致，未执行 nnMain/世界/图形。不能将该矩阵通过写成真实游戏新软件音轨已经验收；后续由 root 用新 APK 采真实游戏功能计数与官方 CPU。

## 软件构造入口与实际 CPU 证据

原 FMOD factory 有 HW64B 与 Software48B 两个入口。仅接管前者会遗漏一条真实单流解码路径；新 SoftwareCtor 命名 HLE 调用真实 guest constructor 后复用同一 ten-slot adapter。HW 内部也直接调用该真实 constructor，避免双 attach。Q 实际 SDK oracle 两个模式各 432 cases：SDK 原 vptr/0 attach，native 12 次选择、204 次成功全部使用 native，最大 PCM 差异 1 LSB，包含失败重 Init 后继续 Decode、Reset、析构和 48B bounds。证据 `.local/logs/opus-soft-ctor-v1-source-ready.json`。

此前仅 HW 覆盖时的同 APK 官方 CPU 对照：目标音频线程 997.140→352.351ms，两相关线程合计 1544.839→1039.710ms；但全进程 2.533→2.608 核，频率、包内容、采集时间均非完全相同，不能声称整机 CPU、能耗或温度已下降。精确窗口与边界见 `.local/logs/opus-core-v2-mali-independent-cpu-comparison.json`。五分钟正常画质动态分辨率游玩仍触发系统热限制，晚期平均出图约 50.20ms、最大 99.08ms；已知主 PSO 无新增创建。因此仍需继续缩短翻译线程的实际 CPU 工作，音频局部收益不能代替散热验收。

## Native 独立容量上限

持续运行的最后集成快照显示 attached/native_selected=7,752、live=32。32 原本是 shadow 验证的资源预算，旧 `attach_mode` 对正式 native 也套用；其满额返回没有计数，`init_fallbacks=0` 不能证明从未遇到这个拒绝。原始 273 条 1Hz state 中 84 条 live=32，多数其余为31；只能证明经常到边界，不能推导拒绝数量或音轨占比。

现在 native 上限为64、shadow仍32，用已有 map 锁保护两类 live 计数；启动后端互斥，所以实际进程最多64 native，而不是两类上限相加。只在构造期满额条件成立时累计 `capacity_rejects`（尝试次数，不是唯一个体数），仍在首个 Init 前保留原 SDK/vptr。被拒对象不会在空出容量后自动换后端；已 native 的对象也不因容量变化切回 SDK。无新增每包锁、原子、线程或计时。

现有 NDK 固定点来源和实际 DWARF/机器码可静态还原 `opus_decoder_get_size`：mono17,860B、stereo26,580B；旁表仍264B，无 native PCM shadow 区。64 对象时 native state+旁表请求约1.11–1.64MiB，比32上限多579,968–859,008B；不含 allocator rounding、原 SDK guest work 和声音缓冲。这些是64个 live attachment 的分配请求账目，不是实测RSS或绝对内存硬上限。析构先从 map 移除，旧节点可能因已借用引用暂存；native state 在原析构前释放，而旁表到最后一个借用退出才释放，短暂重叠仍可超过 live 账目。只增加12B按模式计数和8B累计counter；map仍为链表，最坏线性扫描界限从32增为64，构造、借用和销毁成本并非免费。额外可接管的工作量和CPU净收益必须再由真实capacity_rejects/官方profile核实。

owned 压力用真实 codec 初始化同时持有64 native+32 shadow验证独立预算，各超额一次保留原vptr；连续解码、D0释放/新对象复用、原被拒对象不自动切换、最终live归零均验证。host与ASAN通过，Q原生ARM同一测试通过；这仍是owned SDK mock。真正Q SDK双构造432case在SDKdefault/native两模式复测通过（native204success全部204packets，max1LSB；SDKdefault逐位一致）。证据 `.local/logs/opus-native-cap64-v1-{host,asan}.log`、`opus-native-cap64-v1-q-{owned,sdk,native}.log`。本轮不改变默认后端、包解析、渲染与热策略，未宣称解决过热或已经涨帧。
