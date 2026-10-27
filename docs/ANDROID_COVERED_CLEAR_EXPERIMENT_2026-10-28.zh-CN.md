# 首 draw 完整覆盖时省略初始颜色清屏

本候选复用现有 `cover_first_draw` 的几何与反馈读取证明：仅当同一物理
Render Pass 的第一个 draw 能覆盖颜色附件所有像素、不会读取原内容且
不会因深度／模板、丢弃、混合、颜色掩码等条件保留原像素时，将初始
`loadOp=CLEAR` 改为 `DONT_CARE`。原 `LOAD` 消除规则保持不变。

冷启动 `--discard-covered-clears` 开启，默认关闭；不支持运行中切换。
`covered_clear_omissions` 与原 LOAD 计数分开，提交结束发布。无新增计时器。

## 成本与预算

复用原首 draw 的顶点／索引读取，不再读取一次；但此前 CLEAR 会早退，
开启后，合格的首 draw CLEAR 颜色附件会新增现有反馈扫描和纯几何证明。
每 pass 最多检查八个颜色附件；失败不在后续 draw 重试。不能将新增成本
描述为零，亦不能把不同 attachment 的命中简单累加为整帧收益。

当前 BC3 Mesa pass 诊断中，非零 draw 组的 clear 区间约 0.51ms，
仅是粗略筛选上界，本规则可覆盖的子集更小。零 draw pass 的约 1.326ms
不属于本候选；未新增跨 pass 丢弃或重排。

## 已验证

- 真实 public NVN 编码器、生产 planner/replay 和 Lavapipe：CLEAR 开关
  OFF/ON × 异步完成 OFF/ON 四组各 37 场景、300,846 项检查，Core 与
  Synchronization Validation 零错误。开启组各命中六次，关闭组零次。
- 包含 CLEAR+LOAD MRT、首 draw 部分覆盖后第二 draw 全覆盖、中间真实
  texture-copy 消费者、深度／模板拒绝及 GPU 逐 aspect 回读、零 draw、
  同 pass 显式局部清屏保留；原三十项覆盖证明用例保留。
- Launcher 33 项测试通过；显式开启／关闭可核验实际启动日志，attach
  在调用 ADB 前拒绝此冷选项。

这是生产后端正确性及工作量证据，未执行游戏模块，不能代替 Android
真机性能结论。最初全套 host runner 在异步模式完成本夹具后耗尽后续
测试的 NVN command-memory；原失败保留，未声称全套四组通过。独立夹具
筛选后上述四组均完整通过。

证据及编译输入：`.local/logs/native-bc3-three-arm-game-v1/clear-first-draw-audit/`。
Android 同 APK 冷启动对照和视觉核验尚未完成，候选保持默认关闭。
