# 单次 native import 元数据快照

复用 `native-image-bb-lldb-v2`（5264ec2）的 NDK 官方 lldb-server / LLDB、首次 PID、普通 UID、APK/ELF、私有 socket、forward 和清理协议。工具不启动或结束游戏；调用方负责实际游戏测试、原生命周期存档和恢复。

缺口：现有 game-state 没有完整输出 native VkDeviceMemory 的存活 import 地址区间和 native 物理区间索引规模。仅补这一处：一次 attach，在初始全线程暂停点用 LLDB 的 SBModule/SBType/SBProcess.ReadMemory 读取现有 4 个 8 字节 global 后立即 detach。没有断点、继续执行循环、目标函数调用、表达式求值、链表或贴图数据遍历、逐帧日志、计时器或采样器。

读取 `native_allocation_count`、`native_allocation_bytes`、`native_range_count`、`native_range_capacity`。DWARF 的 `sizeof(native_image_allocation)` 和 `sizeof(native_address_range)` 用于报告结构元数据规模。现有 game-state 的 pool/allocator 数据继续直接使用，不重复实现；`dumpsys meminfo` 由调用方另行采集。**import bytes 是注册的地址包络累计量，可能重叠；不等于唯一物理页面、resident RAM、PSS 或显存节省。** 暂停点可能正处在某个计数更新的中间，不是事务快照，不据此断言计数彼此一致。

必须在官方 CPU/GPU profile 采集结束后单独执行。attach 会暂停进程，不能用这一段推导帧率或卡顿。调用方不得把工具与 profile 窗口重叠。

先运行 `freeze-source.py --output <新目录>`，输出带输入哈希的工具归档。使用归档中的脚本。配置 JSON 必须有且只有：

- `schema`: 1
- `serial`: `10CG570J59001U1`
- `APK_sha256`、`ELF`（完整 DWARF ELF 的绝对路径）、`ELF_sha256`、`ELF_BuildID`
- `canonical_source_ready`、`canonical_source_ready_sha256`，指向与 APK/ELF 相符的 canonical ready 记录
- `tool_inputs_sha256`：归档 `tool-inputs.json` 的 SHA256

调用方式：`run-debugger.py FIRST_PID OUTPUT --config CONFIG --config-sha256 SHA`。`--plan` 只验证本地输入，不调用 ADB。作为现有两参数 hook 时，可用冷环境 `DM_NATIVE_COUNTER_CONFIG` 和 `DM_NATIVE_COUNTER_CONFIG_SHA256` 提供配置。外层 transaction 必须已经将同一 NDK lldb-server 放入自己的 `files/runtime/temp/d2r-lldb-server`。

本工具只使用 socket `d2r-native-counter-owned-v1` 和本机端口 39486，冲突即失败，不接管任何其他调试器。客户进程等待最多 20 秒；失败保持 inconclusive，清理只针对核验过的自有 tracer PID，移除自己的 forward，并将原首次 PID 恢复给调用方存档。现有业务进程或其替代 PID 不由此工具结束。
