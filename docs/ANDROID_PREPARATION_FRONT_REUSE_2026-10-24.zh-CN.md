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

| 原统计，按同进程宽端点的原始 guest submission 归一 | OFF | ON |
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

计数分母是宽端点 `buffer_sync.guest_submits` 的差值，既不是游戏 tick 差，也不是 Perfetto 内窗完整 Surface；以上调用量不是每显示帧，也没有合格的完整 DRAW 分母。views 单项是 cache-hit，必须加 misses；prepares 也不是总消费次数。已有 ordinary/native 混合统计不能冒充新增 native 覆盖率。

完整六窗审查：`.local/logs/native-read-group-front-callchains-v1/six-window-review/`。独立 PMU 四臂亦已完成，不能与旧 A 的窗口拼成对照：

| 臂 | 指令 M / 完整 Surface | 周期 M / 完整 Surface | Running ms / Surface | Surface 间隔 ms |
| --- | --- | --- | --- | --- |
| A1 OFF | 58.681–59.106 | 33.656–33.920 | 26.518 | 32.975 |
| B1 ON | 60.843–61.446 | 36.317–36.729 | 30.608 | 46.812 |
| B2 ON | 60.838–61.437 | 33.376–33.750 | 31.776 | 46.800 |
| A2 OFF | 58.049–58.598 | 37.034–37.529 | 31.899 | 47.043 |

四臂 full=observable、100% PMU 覆盖、无丢样。两个配对的 ON 指令区间分别高约 2.94–4.71%、3.82–5.84%，再次确认额外工作。周期方向不同；采样驻核/频率不同，B2 variants 为 30，其余 32，不宣称完全同工作形状。原 OFF 末臂亦出现约 47 ms 呈现间隔，不能把整个 FPS 下降归因于 B。宽端点组复用损失在两对重复，不能用频率差把明确的消费次数增加解释掉。

这四臂统一通过 Mesa 官方 file logger 核验本次 PID 的实际 `TU_DEBUG=0x1`，只在启动前设置并在结束后 CAS 恢复日志属性；没有改驱动或新增采样器。原 logcat 失败保留，未改成有效窗口。报告 `.local/logs/mesa-startup-file-qualified-v1/four-arm-review/report.md`。

B 的问题对应的最小修复是分开 ordinary / native 两个同 shader、同 pass/serial 子组，让 native 成员变化仅打断 native 子组。保留原所有全局同步边界，不做多 shader 大缓存，不在热路径重新扫描整组资源。旧 B 源码与结果冻结，修复与重新对照见下节 C。

## C：独立子组修复与实测

修复已实现并冻结为 `1123f17` / `codex/native-read-group-partition-v2`，基于 B。普通/native 子组借用位、失效、retry 分开，共享现有 payload 的不相交 descriptor ordinal；native writer 不再打断普通组。两组交错回调时保留仍有效组，失败组只补原先跳过的消费。空组使用现有 FS c2 source version 作资格备忘，变化时重试；空命中不持有请求、不借用 Pool、不计实际节省。

最终同 host binary 的 13 项资格及 16 项 launcher 测试通过。新增真实绘制覆盖：稳定普通纹理 + 连续变化 native 纹理、pending primary 的无关 Buffer fill 引发真正 writer hook、consumer/dependency 两阶段回调、借用升级、空组变为有 native 成员、原 UBO payload 复用。原生产断言未放宽；一轮夹具对 dependency 阶段的额外 View 消费期待错误，失败保留，按实际原阶段分开断言后才最终验收。仍不能将软件 Vulkan RGBA 当 Android ASTC 证明。

新 APK 使用同一 NDK/ARM64 配置、同源码入口构建，1034 项实际输入与六库均核验。初次缺 Android toolchain 预配置时，原禁止 x86 native-host 的 guard 正确拒绝；仅补标准 NDK 配置并保留失败，没有修改 guard 或引入 CPU 模拟。C 真机实际 native 命中、营地画面、原存档 commits=2 与停止通过。

独立四臂结果（完整首对）：

| 指标 | C OFF | C ON |
| --- | --- | --- |
| 指令 M / 完整 Surface | 58.170–58.596 | 60.255–60.681 |
| 周期 M / 完整 Surface | 33.929–34.213 | 34.558–34.819 |
| Running ms / Surface | 26.776 | 26.650 |
| Surface 间隔 ms | 33.123 | 33.166 |
| 按原 guest submission 的 View 消费 | 4861 | 4640 |
| 按原 guest submission 的 skipped_items | 6476 | 6678 |

首对 PMU 完整且 PSO325/variants32 稳定。View 少约 4.55%，但指令高约 2.83–4.32%、周期高约 1.01–2.62%；Running 小降同时伴频率升约 1.18%，不能认作净节省。第二对仍重复了消费条目减少，但 B2 有短驻留 CPU1 的计数缺口、A2 full 缺 CPU7 左边界；保留 full/observable 机械裁剪，不伪造完整第二对效果。没有为了结果改窗口或重采凑数。

C 的事件统计按两个 subgroup 计数，不能把 ON 的总 builds/hits 与旧 B 整组计数当同一总体。实际条目消费/跳过量的单位保持，故能确认修复恢复复用，但尚不能说明新管理值得。当前 C 仍默认关闭并保留源码，原因是已有净工作量退化证据，而非仅因没有即时涨帧。

当前三窗 ON 的官方职责细分又排除一个小方向：cold owner helper inclusive 10/5459 个样本，约 0.183%，同窗估计约 0.061 ms/Surface；唯一被 LLVM 标成 pins helper 的点实际是扫描前初始化，pins 循环没有直接样本。不能把整个 capture 父桶当成这个锁/扫描的成本，更不能拿它解释约 23 ms。

冷建（排除 owner/retain）约 0.286 ms，handle watcher 约 0.204 ms，ready 混合约 0.020 ms，原必须保留的实际 retain 约 0.204 ms，均含原 ordinary 工作且细项样本少。不可把这些 ON 单项全称新增或与异窗 PMU相乘。原 retain 返回 owner 证书的方案已只读设计，但没有因看见一个锁就立刻扩五个文件；先用关闭侧同口径比较确定优先级。

证据：`.local/logs/native-read-group-partition-v2/ready.json`、`native-read-group-partition-v2-independent-review/final-review.md`、`native-read-group-partition-pmu-v2/four-arm-review/report.md`、`native-read-group-partition-pmu-v2/pmu-boundary-evidence/report.md`、`native-read-group-partition-callchains-v2/ON/group-duty/`。

### C 的最终六窗比较

开关各三窗均已完成官方导出、HTML 与原生命周期收尾；精确新 ELF、PSO325/variants32 稳定，无采样丢失或 TraceProcessor 健康错误。343 项最终审查引用匹配。OFF/ON 各有 494 个完整 Surface，用户态估计 28.463→28.994 ms，Running 30.918→31.338 ms，呈现间隔 47.016→46.973 ms。这批窗口频率/驻核与 PMU 首对不同，不能把约 47 ms 和前述约 33 ms 拼成候选效果。

明确的工作收益仍在：按原 guest submission，View 总消费约 4895→4626–4630、跳过条目约 6442→6692。管理包络排除必须保留的 retain 后，全窗份额 2.163%→3.297%；同窗 0.654→0.981 ms（约增 0.327 ms，计数噪声尺度约 0.183 ms，非统计置信区间）。其中冷 capture/build（排除 owner/retain）22→53 样本、handle watcher30→38、record/lifecycle23→38、expand/break7→15、ready7→10，包含原普通组，不能把整个 ON 包络都当新增。

View 子项同窗 2.086→1.943 ms，但只有 102→95 个样本，差值噪声尺度约 0.287 ms；全窗份额反而 6.92%→7.25%。history/readproof 0.716→0.736 ms。消费次数减少是确证，净省时仍未确证；原 PMU 完整首对指令上升亦须保留。没有把未改路径的时间波动包装成收益。

去除 group 管理父路径后的宏观互斥估计包含 Draw 装配/录制约 6.1–6.3 ms、driver 约 3.6–3.7 ms、资源职责约 9.1–10.0 ms，以及 replay、planning、提交等。这些是继续调查的范围，不是可删除预算；目前没有证明另一个可整段删掉的大循环。尤其不能从约 0.061 ms 的 owner 叶子扩出能解释整线程的收益。

最终报告与官方 HTML 索引：`.local/logs/native-read-group-partition-callchains-v2/six-window-review/report.md`。A、B、C 源码均保留并推送 fork 独立分支；本轮没有向生产主线默认启用这些候选，也没有删除已减少实际工作的机制来掩盖问题。B/C 的依据是已观察到净指令/管理退化，不是只因 FPS 没涨。约二十多毫秒的整线程问题尚未解决。

所有本轮设备采集已结束，最后一次 C OFF 的原进程保存 commits=2 并停止。根代理另核 `pidof` 无进程、两项 Mesa 调试属性均恢复空值；没有让游戏空跑，也未触碰已暂停借出的 Mali 或 6GB 平板。

## 证据位置与构建边界

- A：`.local/logs/early-native-consumer-v1/ready.json`；`.local/logs/early-native-consumer-build-v1/ready.json`；`.local/build/early-native-consumer-v1-final/`。
- B：`.local/logs/native-read-group-front-contract-v1/ready.json`；`.local/logs/native-read-group-front-build-v1/ready.json`；`.local/build/native-read-group-front-v1-final/`。
- 隔离计划：`.local/logs/early-native-consumer-isolation-v1/plan.json`。
- B 真机画面及原存档：`.local/logs/native-read-group-front-visual-v1/capture/`。
- B 官方调用栈：`.local/logs/native-read-group-front-callchains-v1/`。

两次 canonical ready 中的重定向控制台输出在 ready 写入后才结束写入，原引用因此仍记录空文件。没有修改原 ready 或产物；仅对该唯一的精确 ready 引用 JSON 行补充封存 `.local/logs/early-native-consumer-isolation-v1/canonical-evidence-finalized.json`。实际 APK、ELF、全部源码及其他证据引用不变。不得把这一控制台收尾例外推广为允许产物哈希漂移。
