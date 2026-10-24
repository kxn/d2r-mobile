# 资源准备前移复用：两个独立候选与隔离对照

目标是减少普通 NVN 翻译路径的资源消费工作，不依赖旧命令或 retained command buffer 存活。约 8.2 ms 的资源准备父项含消费、依赖、引用和历史记录，不能整体认领为可缓存时间；约 26 ms 的翻译线程 Running 也不能直接作为收益预算。

## 实现

两个候选均基于 `4997063`，默认关闭，分别发布到 fork 的独立分支。它们不叠加，覆盖存在重合，收益不能相加。

| 候选 | 源码 | 实际省去的工作 | 保留与新增成本 |
| --- | --- | --- | --- |
| A：resident native recipe 提前交付 | `f305e6f`，`codex/early-native-consumer-v1` | 复用已有 recipe，限定 FS sampled / resident / GENERAL，提前返回已准备的 View，省通用尾部选择和比较 | 保留原身份、mip、回调重验、operation retain、请求、依赖和历史；新增提前资格分支。这是很窄的候选，旧采样不能支持多毫秒预测 |
| B：native 纹理进入已有同 pass 读取组 | `e099713`，`codex/native-read-group-front-v1` | 首次完整消费后，组命中跳过逐纹理准备、View/sampler 解析、请求组装和重复只读历史 | 仅同 pass / recording serial，保留 source walker、CPU publication、提交同步；每操作重新 retain 去重后的 Pool。冷建增加 Pool 身份提取与去重，变化会使整组失效 |

B 不把 CPU 可写纹理当不可变数据缓存。原 Buffer producer、barrier、图片写入、layout/mip、资源退休、descriptor/sampler、pass/serial 边界仍会物化或失效旧组。回调后重新取得请求数组，避免物化扩容后使用旧指针。支持的只是合法同 pass 只读子集，不能推广为跨提交复用。

## 隔离条件

- 普通主线源码不包含 retained、descriptor-buffer、source-forward 实验实现；source partition 和 Pool copy accounting 均关闭。
- 同一候选使用同一个 APK，只切自己的冷启动开关；A、B 分别构建，不把多个候选混成对照。
- 固定 Q735、f830 驱动、950 shader chain、native 6217 表及 33 项已装资源。保持 0.60、1650×756 的 3D、2750×1260 输出、目标 60、shared lazy 1 MiB、原 span reference。
- 不启用抓帧层、ADPF、固定频率、sysmem/profiled 模式或额外诊断。测量期间不做截图、模拟点击或额外 ADB 查询。
- 沿用官方 Simpleperf / Perfetto 及原同窗分析。计数、CPU Running 和呈现间隔分别报告，不将异窗比例相乘推导毫秒。
- 原游戏模块和资产未部署新版本；本次修改只有宿主 Vulkan 后端及冷启动选项桥。每次测完通过原生命周期确认本进程新存档 commits=2，再停止并核实退出。

## 已完成的正确性资格

A 完成 12 个有效 host GPU 用例、16 个 launcher 用例；B 完成 7 个定向 native GPU 用例、普通完整回归开关各 23,781,033 checks 和 16 个 launcher 用例。保留原像素、mip、publication、回调、异步提交与借用升级断言。先前失败原样保留，不混入通过数。

Host 使用 coherent imported RGBA / 软件 Vulkan，不能代替手机 ASTC 游戏验证。B 尚无定向多 Pool GPU 专项，实际 writer-hook 有效失效计数为零（原 render_end 先失效），不能宣称该分支已动态覆盖所有情形。

两候选在 Q735 均已实际进入原游戏营地，截图未见明显黑块或几何错误，存档退出通过。B 实际首次命中为 1 个 native descriptor、1 个 unique Pool，仅证明路径已使用，不代表整帧覆盖率。

## A 的实际结果

| 窗口 | 指令 M / Surface | 周期 M / Surface | 翻译线程 Running ms | Surface 平均间隔 ms |
| --- | --- | --- | --- | --- |
| A1 OFF | 58.517–58.941 | 34.382–34.668 | 26.777 | 33.076 |
| B1 ON | 58.644–59.068 | 34.647–34.914 | 26.444 | 33.125 |
| B2 ON | 58.255–58.681 | 34.456–34.725 | 26.745 | 33.124 |

三个窗口 PMU 覆盖完整，区间来自原工具窗口边界，不能当统计置信区间。ON 与 OFF 范围重叠，没有证明净省工、涨帧或净回归。B1 的 variants 端点从 30 到 32，频率与驻核亦变化，不能把约 0.333 ms 的 Running 差归因于提前返回。

第四臂两次因未获得本 PID 的官方 TU/MESA startup 日志而在测量前拒绝，均成功存档停止，没有性能窗口，不能称完整 ABBA。日志握手成功不能替代实际 driver mode 证明。没有为凑齐对照降低资格门槛。

完整离线审查保存在 `.local/logs/early-native-consumer-device-v1/independent-review/three-window/`（404 inputs、544 个原引用匹配）。A 保留为可复核的默认关闭实验；不能仅以未涨帧判断机制无效，也没有证据把它默认启用。

## B 测量记录

官方 `cpu-clock:u` / FP 开关各三窗已经完成，使用本候选精确 ELF 重新解析所有实际 PC。六窗无采样丢失；现成统计显示明确的反向工作量变化：

| 原统计，按同进程宽端点的游戏 tick 归一 | OFF | ON |
| --- | --- | --- |
| group builds | 1767 | 1922 |
| group hits | 597 | 442 |
| skipped_items | 6550 | 4572 |
| invalidations（约） | 48 | 347 |
| View 总消费：texture_bindings.views + misses | 4787 | 6765 |

少跳过 1978 项，恰好对应多消费 1978 次。group 准入扩大后，新增成员的变化牵连原 ordinary 成员；这有工作计数证据，不是因为 FPS 没涨而猜测无效。首次 native 命中不能抵消原缓存命中的损失。invalidations 不包括换 shader、空组等所有重建原因；当前证据尚不能指出每次失效具体来自哪个 writer 或哪个句柄。

| 同窗指标，OFF / ON 各合并三窗 | OFF | ON |
| --- | --- | --- |
| 用户态 CPU 采样估计 ms / Surface | 23.993 | 25.002 |
| 资源职责互斥估计 ms / Surface | 8.885 | 9.686 |
| NvN Running ms / Surface | 26.260 | 26.732 |
| Surface 平均间隔 ms | 33.038 | 33.077 |

View residual 1.903→2.178 ms，group 冷构建 0.086→0.186 ms，热准入 0.114→0.115 ms，展开失效 0.014→0.057 ms。细项只有个位到百余样本，存在频率和采样噪声，不能把它们当精确因果金额；OFF 第三窗亦保留工作形状小变。主判断依据是总消费次数反增，而非约 0.039 ms 的呈现差。

计数宽端点的游戏 tick 与 Perfetto 内窗完整 Surface 不同，以上调用量不是每显示帧，也没有合格的完整 DRAW 分母。views 单项是 cache-hit，必须加 misses；prepares 也不是总消费次数。已有 ordinary/native 混合统计不能冒充新增 native 覆盖率。

完整六窗审查：`.local/logs/native-read-group-front-callchains-v1/six-window-review/`。独立 PMU 四臂正在采用新 namespace 测量，不能与旧 A 的窗口拼成对照。

后继最小修复是分开 ordinary / native 两个同 shader、同 pass/serial 子组，让 native 成员变化仅打断 native 子组。保留原所有全局同步边界，不做多 shader 大缓存，不在热路径重新扫描整组资源。旧 B 源码与结果冻结；修复在新分支实现并补稳定 ordinary + 变化 native 的真实 draw 负例，再重新对照。

## 证据位置与构建边界

- A：`.local/logs/early-native-consumer-v1/ready.json`；`.local/logs/early-native-consumer-build-v1/ready.json`；`.local/build/early-native-consumer-v1-final/`。
- B：`.local/logs/native-read-group-front-contract-v1/ready.json`；`.local/logs/native-read-group-front-build-v1/ready.json`；`.local/build/native-read-group-front-v1-final/`。
- 隔离计划：`.local/logs/early-native-consumer-isolation-v1/plan.json`。
- B 真机画面及原存档：`.local/logs/native-read-group-front-visual-v1/capture/`。
- B 官方调用栈：`.local/logs/native-read-group-front-callchains-v1/`。

两次 canonical ready 中的重定向控制台输出在 ready 写入后才结束写入，原引用因此仍记录空文件。没有修改原 ready 或产物；仅对该唯一的精确 ready 引用 JSON 行补充封存 `.local/logs/early-native-consumer-isolation-v1/canonical-evidence-finalized.json`。实际 APK、ELF、全部源码及其他证据引用不变。不得把这一控制台收尾例外推广为允许产物哈希漂移。
