# 既有生命周期的 builtin VMA 归档

此工具后继以 `1197ac7d2c4790341726081ba00685e9829a159d` 为父，
仅给原 `native-bulk-game-v1/run-game.py` 增加可选输入：

```json
{"builtin_vma_on_suspend": true}
```

字段缺席时保持原行为；提供时必须是 JSON `true`，且必须使用原
`official_cpu_aa` 和 `performance_tools`。不允许并存 startup allocation、
LLDB counter/read-only snapshot、sustained、GPU diagnostic 或额外 L1I 窗口。
原 APK、资源、路线、3×30s FP/5×8s Perfetto、存档和恢复规则均不改变。
不新增 runner CLI、运行时开关、JNI、HUD 或配置界面。

原离线入口如下；输入的其余身份和 helper pins 仍须由原 proof 验证。
此文不提供待执行输入，也不声称已在 Q735 运行：

```sh
python3 scripts/dev/native-bulk-game-v1/run-game.py \
  --inputs /absolute/path/to/reviewed-inputs.json \
  --inputs-sha256 ACTUAL_INPUT_SHA256 \
  --report /absolute/declared/output/result.json
```

归档只在所有 performance windows、前台 dumpsys/showmap 和 HUD 收集结束后，
原最终 fresh-save 调用增加既有 `--vma-dump-on-suspend`。
执行的仍是仓库根目录既有 `scripts/dev/android-game-background-save.py`；
此工具不复制或修改它，其实际路径和 SHA 已纳入 `controller_sources`。
helper 必须确认同 PID 的新 onNewIntent accepted=1 后才 HOME，且返回新 save2。

随后最多 20 秒、每 200ms 读取原 native.log，等待同一个请求的完成。
这是已有工具的有界控制等待，不产生 CPU/GPU 性能时间；不重复请求或暂停。
每次都确认 owned PID、native-start 和日志连续前缀。只接受本次日志范围内
唯一 accepted=1、唯一 fresh complete=1/errno=0 和原 builtin 路径/标记，无 failed。
确认完成后才读取固定 `files/runtime/temp/vulkan-vma.json`，以官方 ADB exec-out
按原字节归档，并比对读取前后的远端 SHA 与本地 SHA；不解析生成替代统计。
原 JSON 仅检查 builtin detailed-map 的基本结构。

每个 phase 增加 `before-vma-request-native.log`、`after-vma-suspend-native.log`、
原样 `vulkan-vma.json`；phase.json 的 `builtin_vma` 含 path/SHA/bytes/PID/完成日志。
请求、日志、JSON 或归档失败时记录 `builtin_vma_error`。
只要原 helper 已确认新 save2，诊断失败仍执行原 owned PID stop，再令事务失败并
进入原 restore/finally；helper 未确认 save2 或 PID 被替换仍沿原 fail-closed，不擅自停止。

这是原 pause/drain 后、该次 idle trim 前的 VMA 元数据，可能已发生之前的 tail FRAME trim。
不是与前台 PSS 同时的快照，VMA Allocation/Block/Usage 不等于物理驻留或 mtrack。

主机资格复用既有历史 Mali builtin JSON/log 只验证解析与新旧请求边界；
不构成当前 Q735 内容、存档或性能验收，不会启动该设备。
测试覆盖 stale complete、错 PID、拒绝/重复请求、坏 JSON、失败完成、延迟完成、
超时、日志重置、原字节归档，以及诊断失败后 stop/未确认 save2 不 stop。
