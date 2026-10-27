# 首 draw 完整覆盖时省略初始颜色清屏

本候选复用现有 `cover_first_draw` 的几何与反馈读取证明：仅当同一物理
Render Pass 的第一个 draw 能覆盖颜色附件所有像素、不会读取原内容且
不会因深度／模板、丢弃、混合、颜色掩码等条件保留原像素时，将初始
`loadOp=CLEAR` 改为 `DONT_CARE`。原 `LOAD` 消除规则保持不变。

冷启动 `--discard-covered-clears` 开启；初始实验默认关闭，完成下述资格
和成本对照后，源码默认开启，可用 `--no-discard-covered-clears` 保留参考。
不支持运行中切换。
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
## 同 APK Android 结果

Q735 / Adreno 735，BC3 native 共享、12b8 Turnip、1968×900 实际交换链、
1673×765 主 3D 目标，inline CPU7、单提交、异步完成、prefer_sysmem、
UBWC、原 shader 链 29e56b51bce5。各臂都执行原 main/sdk/subsdk0 游戏模块，
未改游戏 binary、driver 或资源。APK `498c0ff8…f42d3ef`、ELF
`4e97b332…619592`，源码 `23808fb`，1054 项构建输入及六库哈希均验证。

| 整轮指标 | CLEAR 保留 | 首 draw 证明后省略 |
|---|---:|---:|
| 普通运行，20 秒官方 Perfetto FPS | 43.1791 | 43.2499 |
| 主线程 Running，ms / Surface | 18.7323 | 18.7473 |
| 全应用 Running，ms / Surface | 35.2975 | 35.3816 |
| 主线程 Running 时加权频率，MHz | 1603.11 | 1584.66 |
| 独立原厂计数器窗口 FPS | 43.2388 | 43.2998 |
| GPU 全局读，MB / Surface | 105.4410 | 106.0163 |
| GPU 全局写，MB / Surface | 44.2473 | 44.2795 |

这里没有可声明的显著涨帧或带宽收益，也没有明显 CPU 成本退化；独立
窗口频率不同，不能把约 0.015ms 的 CPU 差当作精确的证明成本。已有
工作量计数在开启的 20 秒窗口前后状态括号内增加 3584、关闭为零。
状态括号不是严格裁到 Surface 的区间，不能用游戏逻辑 tick 当渲染帧
计算准确每帧次数。截图目视未见新黑块或几何错误，不代表全游戏验收。

这项证明成立的工作量削减予以保留，源码默认开启；目前不能把它放在
主要提速预算里。不能从无 DRAM 下降反推它没有命中，也不能将逻辑清屏
字节数冒充硬件流量。本轮没有进一步测清这些清屏的内部压缩／metadata
成本，因此不声称已经证明具体驱动消除机制。

最初普通 N1 对照因沿用的工具编排助手只替换固定 `duration_ms:20000`
文本，而源配置为 8000，实际只捕获八秒。原始 trace、配置和收条保留，
仅作普通运行初筛；已修正未来编排并用实际 `duration_ms:20000` 的 N2
补齐上述正式窗口。没有把旧八秒重新标为二十秒。原厂 GPU 窗口本来就是
独立八秒，不受这个问题影响。

完整结果、来源哈希及整轮 CPU／等待／GPU 变化：
`.local/logs/native-bc3-three-arm-game-v1/covered-clear-v2/RESULT.json`，
同目录 `whole-normal-v2/`、`whole-gpu-v1/`。测试后原 39197b APK、原
22 个用户存档、六个 overlay 文件、B3 driver 与全部 28 个游戏构建产物
均已恢复，游戏停止；本轮源码默认改变尚未部署为用户日常包。原厂 GUI、
Xvfb 与本次 13 个 collector 已核实退出；关闭过程一项 `/proc` 退出竞态
原样保留，最终全部不存在，没有重复杀进程或停止系统服务。
