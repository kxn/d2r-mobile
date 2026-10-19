# Android Opus 影子验证候选

此候选默认关闭：仅 `DM_ANDROID_OPUS_SHADOW=1` 在第一次构造前启用，启动后缓存判断。本页仅描述 shadow 模式：实际 PCM、返回码、consumed、输出数量仍由原 SDK 产生。另一个独立、默认关闭的 native core 候选见 [ANDROID_OPUS_NATIVE_CORE.zh-CN.md](ANDROID_OPUS_NATIVE_CORE.zh-CN.md)。不得将宿主原创用例通过当成真实游戏声音验收，更不能预报 FPS 或耗电收益。影子同步重复解码会增加 CPU 工作，限每对象 256 次、进程 1024 次 native 尝试，最多 64 条功能日志。

## 原项目路径与 ABI

沿用 `hle_codec.c` 原有 HardwareOpusDecoder 构造接管，先在原来 guest 分配的对象上调用 SDK 软件 OpusDecoder 原构造。启用候选时，必须解析同模块原 vtable，要求其符号大小恰为 96B、offset-to-top 为 0，并逐一匹配全部 10 个虚函数符号。ABI 不符就保留原 vptr，不猜测 SDK 内部 state ABI。仅该 HLE 边界增加宿主旁表；无游戏地址补丁、无新游戏数据和线程。

真实 SDK 符号与重定位证据：`.local/logs/opus-shadow-v1-original-vtable.json`；原方法反汇编：`.local/logs/x18-groups-v2-opus-interface-disasm.txt`。原 SDK 版本串是 `libopus unknown-fixed`，不能据此断言它与 1.5.2 位精确相同。

10 槽依次为 D1/D2、D0、GetWorkBufferSize、IsInitialized、Initialize(rate, channels, work, size)、Finalize、GetSampleRate、GetChannelCount、Reset、DecodeInterleaved(consumed, count, PCM, PCM bytes, input, input bytes)。所有槽转发原方法，Init 后独立创建 host state；原 guest work 仍由 SDK 使用。D0/D1 在执行原析构前恢复原 vptr，旁表仅释放独立 host state；原 D0 独自释放 guest self，返回后不再读取 self。

Decode 先执行原 SDK。只有成功且跨度合法、输入与输出/count/consumed 不重叠，才对 `input+8, consumed-8` 解码；8B 来自已有 SDK wrapper 行为，不重新猜包头长度或字节序。原输出始终不改。1 LSB 差异仅作为非逐位一致的影子验证观察，允许继续累计；不宣称完整兼容。第 256 次有效比较立即停止并报告 successful/bit-exact/non-bit-exact packet 数、不同 scalar 数及最大误差。SDK 错误、native 错误、输出数量/guard 不符或任一样本 PCM 差异超过 1 LSB 都会停止该影子，直至明确 Reset/成功 Init。Reset 按原 SDK 延迟语义在下一次 SDK 成功解码时作用于 native state。包跨度仍按原调用线程约定稳定；这不是对无同步并发 guest 写入的事务快照。

## 线程、旁表与栈

全局 map 锁只保护查找/引用、移除、少量累计计数和日志额度；绝不覆盖 SDK/native 解码，不持 map 锁等待对象锁。每 decoder 有独立 recursive serial mutex 和 borrow refs。析构从 map 移除后，已借用但排队的调用只检查 detached，不再解引用 guest self，最后一个 ref 才释放 host node。两个 decoder 不因候选新增全局串行；原 SDK 内部已有锁仍照旧。

当前合并 native core 元数据后，host 节点实际 NDK DWARF 大小为 264B（原 shadow-only 为 224B），另有一次 Init 分配的 native state 和 23,072B PCM/guard 区，无每包 malloc。启用影子后每次方法有 map/ref 和对象锁成本；对照累计数据增加短 map 锁和样本逐值比较。进程 budget 停用后保留 wrapper 生命周期管理。默认关闭时不改 vptr，无 per-packet 新锁、查找或环境查询；构造多 cached pthread_once。线程元数据新增 24B 与一次启动期公开 pthread_getattr_np 查询，未改 guest 栈大小、调度与等待。

原 guest 音频工作线程实际栈容量尚无已有日志证明。官方 NDK `-fstack-usage` 显示 codec 动态数组，不能拿静态 frame 字节当总栈上界。为此仅纯 host libopus Init/Reset/Decode math 通过 `dm_thread_host_call`：

- 启动期缓存公开 pthread stack bounds，验证至少 256KiB 剩余空间。
- 已在原 host 栈时直接调用；否则仅切到此前 entry 保存且经 bounds/alignment 验证的 parked SP 下方，保留 thread_main frame。
- 嵌套已在 host 栈就直调；depth 非零但不在该栈则拒绝，避免重切 parked 顶覆盖外层。
- 无注册线程、无可靠锚点或空间不足则不执行 callback，停止/跳过影子，正式 SDK 照旧。
- 不分配新 decoder 栈；桥不改 x18/TLS，保存 x19/x20、FP/LR，SP 16B 对齐。DWARF CFA 以 x19 固定在原 guest frame。回调不得执行 guest code、ExitThread、longjmp、C++ unwind；非信号安全 API。现有 altstack 信号处理可照常打断/恢复，需 ARM 测试。

`opus_decoder_destroy` 在固定源码中仅为 free，清理不执行大 codec math。普通 guest 分配/释放语义仍与现有 SDK 构造/析构一致。256KiB 是保守 native headroom，不把 `.su` 动态栈报告冒称为完整数学上界。

## 来源与可复现构建

官方来源：[libopus 1.5.2 发布](https://opus-codec.org/release/stable/2024/04/12/libopus-1_5_2.html)，[原 tarball](https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz)。SHA256：`65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1`。

`prepare-opus-source.py` 验原 archive SHA 后生成原样来源子集：434 文件，省去未启用 DNN 的非 header 大模型文件，保留 BSD COPYING；逐文件 SHA256SUMS、UPSTREAM.json。CMake configure 再逐一验证来源。固定 fixed-point、float API off、DRED/OSCE/deepPLC off、thread-unsafe pseudostack off、PIC/hidden/static、-O3/fno-omit-frame-pointer；APK 即使 Debug CMake 配置也不让 codec 落入 O0。ARM64 保留 NEON intrinsics。上游 1.5.2 CMake 误声明 ARM32 汇编 pitch alias，wrapper 仅移除非 _INTR 的两项 define，未改任何 vendor 字节；x86 fixed-point AVX2 配置自身来源列表有 FLP include 缺口，host 用其原 SSE4.1/C 路径。

## 本轮已执行与尚缺验收

已执行：

- 官方上游 CTest 5 项 decode/padding/API/encode/extensions 全通过（host fixed-point）；这不证明 SDK 版本兼容。
- owned shadow fixture 普通及 ASAN/UBSAN 通过，覆盖 8/12/16/24/48k、mono/stereo、输出/count/consumed、错误后停用/Reset、unsupported Init、两对象、D0/D1 恢复和并发借用、256 次 budget。
- 确定性 cond 场景中 A SDK mock 阻塞而 B 完成，A 的 Finalize/D0 借用均已进入真实旁表（读实际 ref 谓词，无生产测试 hook）后才释放 A；100 次独立进程复跑通过。
- 独立 NDK native_rule_test、libnative_tlsdesc_test.so、native_opus_shadow_test 编译/链接通过；NDK codec 两个 TU O3 编译通过；官方 llvm-dwarfdump 验证 bridge CFI。

已补充真机验收：Q 原生 bridge/owned wrapper、原 TLS descriptor 64 线程和 ARM 透明尾转发通过；Mali 真实 SDK 4 对象共 1024 包 shadow 对照通过。两 48k stereo 对象存在 3891/970 个不同 scalar，全部最大误差 1 LSB；两个 16k stereo 对象共 512 包逐位相同，实际 Finalize/D1 已观察。证据位于 `.local/logs/opus-shadow-v2-mali-sdk/`。这不证明完整兼容、听感和持续稳定性；LLDB 跨栈 backtrace 仍未执行。native_rule_test 新增 case 保留所有旧 case，使用原 HLE Create/Start/Exit/Wait/Close 跑 2 个 16KiB guard guest 栈，各 32 次 96KiB native callback+嵌套；检查 x18、x19–x29、d8–d15、SP、两边 canary 与 altstack 信号。普通主线程直接调用与无 HLE 锚点拒绝也包含。这些 ARM case 已由 root 在 Q 执行通过，见 `.local/logs/opus-shadow-v1-q-arm/`。

root 执行步骤：先运行独立 `native_opus_shadow_test` 无参数；再 `native_rule_test <既有原创 rewrite fixture目录>`（原 ABI2/3 均可），需要时原 `--thread-main-exit` 另测；有模块 manifest 的 fixture 原样复用，不修改任何真实游戏产物。ARM 原生测试通过及独立审阅后，root 才将默认 OFF candidate APK 安装，启动前设 shadow 环境，并确认日志 active/compared、native/SKD 错误和 PCM 差异，SDK 音频仍为唯一输出。最终是否能启正式 native 输出另行实现/验收，不在此候选内。
