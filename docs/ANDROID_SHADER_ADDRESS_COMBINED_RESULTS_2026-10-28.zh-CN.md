# Shader 地址规范化：VS2 + FS2 本轮结果

**正式同尺寸对照通过；同日已完成高通手机普通桌面版发布与两次冷启动验收。** 性能窗口使用同一498测试APK；日常c09普通APK另行通过功能验收，不将两者冒称同一个已测binary。

本轮在已化简的两条 VS 上，再对两条 HDR FS 的原始 base 和 `.spec` SPIR-V payload 应用同一精确整数地址规则。普通20秒窗约49.88→50.45 FPS；独立官方GPU8秒窗的主GPU批区间19.771→19.611ms。省工已同时有原生编译和真机硬件区间证据，但增量约1%，仍有CPU调速、动态工作量及单对样本的限制。

## 资格和唯一变化

- 同一 `498c0ff8…` APK / `4e97b332…` native ELF、精确 `12b8b54a…` Turnip、原 BC3-linear 共享资源；CLEAR均OFF。
- **两臂均以实际日志证明** game/swapchain1968×900、main1673×765、scale0.85，营地位置5225/6289、同角色状态。PSO180、specialization variants32、pending0、image allocation313450496B签名相同。
- 参考为 VS-only `1065929e0840`，候选为 combined `cf83dfece8e7`。两条 VS 的 SPIR-V 逐字节相同；区别限于两条 FS 的精确整数表达式和据此重新认证的闭包。无新增运行时预测、精度、描述符范围、robust、资源编码或游戏逻辑变化。
- 两组正式工具/普通 receipt 均 `completed=true`、`window_qualified=true`、`game_stopped=true`。普通20秒与带SDP的8秒窗口分别报告，不能拼成一个窗口。

## 普通20秒窗

VS-REFERENCE-N2 → COMBINED-N1：

| 项目 | VS-only | Combined | 变化 |
|---|---:|---:|---:|
| Surface FPS | 49.883500 | 50.447268 | +1.130% |
| 平均间隔 | 20.046709ms | 19.822679ms | −0.224030ms |
| p95间隔 | 22.159896ms | 22.155833ms | −0.004063ms |
| owner Running/Surface | 16.469162ms | 16.438549ms | −0.030613ms |
| 全app Running/Surface | 31.623909ms | 31.507294ms | −0.116615ms |
| owner运行期加权MHz | 2048.67 | 2066.53 | +0.872% |
| owner S/Surface | 3.295328ms | 3.137802ms | −0.157526ms |
| owner R/R+/Surface | 0.226915ms | 0.216232ms | −0.010683ms |

普通窗内睡眠减少较CPU Running明显，kgsl-events后继唤醒归属约3.013505→2.866957ms/Surface。该睡眠归属不能直接叫Shader执行时间；缺少本轮PMU instructions/callchain，也不能用Running×平均频率制造指令预算。

资源外侧snapshot中，texture prepares/guest约−0.85%、pipeline lookups/guest约−2.29%，说明相同场景/稳态签名也不保证每次动态draw序列完全相同。descriptor allocation snapshot调用13→62、sets13→115；数量不大，但不能用倍数推毫秒，或无依据地把它因果归给此次离线变换。internal submit/guest两轮均1，旧pool上传/回读等增量均0，没有新增搬运证据。snapshot分母使用各自guest提交数，不能替换成Surface数；官方同trace时间转换已确认它们确实括住Surface窗口。

VS-REFERENCE-N1被原健康gate拒绝：官方Perfetto `systrace_parse_failure=11`，原始内容是lmkd内存压力/thrashing普通print。失败receipt及原始数据保留，**没有纳入正式结果，没有放宽gate，也不把它解释成Shader变换崩溃或性能回归。** 正式参考使用重采且合格的N2。

## 独立官方GPU8秒窗

VS-REFERENCE-G1 → COMBINED-G1，396/399个完整Surface间隔，工具FPS50.071520→50.446433（+0.749%）。

| 项目 | VS-only | Combined |
|---|---:|---:|
| 主批SOP/EOP平均区间 | 19.770980ms | 19.611331ms |
| 主批P95 | 19.904333ms | 19.722422ms |
| 出图copy平均 | 0.189331ms | 0.189047ms |
| GPU clock-count率 | 499.716358M/s | 499.537344M/s |
| Fragment instructions/Surface | 1.877183G | 1.843704G |
| Fragment instructions/second | 93.993409G | 93.008273G |
| Read Total/Surface | 105.290939MB | 105.171841MB |
| Write Total/Surface | 44.184526MB | 44.177727MB |
| Read Total/second | 5.272077GB | 5.305544GB |
| Write Total/second | 2.212386GB | 2.228609GB |
| Shaders Busy | 72.167% | 71.755% |
| Shaders Stalled | 16.361% | 16.736% |
| Stalled on System Memory | 0.375% | 0.400% |
| Texture Fetch Stall | 11.916% | 12.180% |
| Wave Context Occupancy | 58.028% | 58.021% |

主批缩短 **0.159650ms /0.8075%**，fragment instructions/帧减少约1.784%，GPU clock-count率没有增加。读写/帧基本没降；每秒读写略升伴随FPS增加，不能只看每秒数字判定工作变多，也不能将本次说成明显的带宽优化。SDP计数为全局，包含compositor；不代表独占NVN成本、功耗或DRAM饱和。

各臂独立用真实PID/owner/队列事件识别context，再以positive-stamp `queued`关联准确API根，随后连接submitted/retired；没有按延迟的submitted事件当时所在API猜角色。主批SOP/EOP包含GPU内部停顿/抢占，不是纯Shader耗时。796/803对本窗时钟锚点拟合约19.2MHz XO，最大BOOTTIME映射残差4.169/4.793µs；XO校准不能冒充Shader核心频率。

可观察内部窗口GPU区间并集99.8661%→99.7604%，前缘53.915/47.295ms未知、尾缘未知0；未将未知边缘填0。所有可配对下一replay均早于上一main硬件EOP（392/392和395/395），没有GPU因为CPU睡眠断粮的证据；这些占用区间也不等于ALU利用率。

同工具窗CPU owner Running为16.197771→16.351670ms/Surface，运行期加权2047.266→2076.498MHz（+1.428%），频率覆盖99.9758%/99.9609%；全app Running30.588788→30.909331ms/Surface。因此不能说整帧提升来自纯GPU单因素，或把单对+0.749%当稳定承诺。

## 原生代码与正确性合同

官方现成`gfxrecon-extract`从已有捕获中恢复的两条HDR FS specialized payload及实际配对VS，与当前draw-linked源SHA完全相同；真实PSO的layout、dynamic UBO、128B push constants、压缩顶点格式、B10G11R11+D24S8、depth/stencil等沿用原API值。独立普通UID夹具只创建PSO，不draw/submit/计时。其原FS的native BLAKE3也精确复现实际游戏 `1fce1866…` / `fa9cb4b7…`。

| 当前热点FS | 原版native | 规范化native | 寄存器/waves | 动态LDC |
|---|---:|---:|---:|---:|
| 5c1ca… | 2096 instr | 1993 instr | 均19full /10waves | 23→7 |
| 38f344… | 2124 instr | 2038 instr | 均19full /10waves | 23→7 |

23个标量读取合成5×vec4 +1×vec2 +1×scalar，仍为同23个分量；cat2各减76，texture45/46与loops4不变，没有FP16或描述符范围变化。额外safe-constlen native变体保留，但未混成与实际draw匹配的主变体。

Combined从完整原29e cache及精确`59320caa…`原receipt派生，不手改VS-only缓存。两VS各48个结果、两FS base/spec各23个结果，共 **188个SSA结果**逐个通过全32bit Z3等价证明；构建本身不依赖每个Shader运行Z3。有限规则保留32bit溢出/低位进位/所有输入bit pattern，浮点与内存访问语义不变。

FS `.spec`沿原声明的DSP1/DSP2格式结构化重建，原magic/version flags、binding、slot、offset、SpecId保持；当前两条仍为原uint32 Spec0选择，slot4/offset0、binding27或26，没有重新启发式选word。新base/payload分别证明等价，原base/spec关系通过两者等价传递；仅字数与新baseSHA随新模块更新。

6931 Shader /52240原始产物完整验证；候选52252产物另加build record。所有未选产物和`.meta`原字节保持，driver-key/span/pixel-local闭包由原发射器再生成并独立核验。7项合同/负控测试覆盖改selector、base、原metadata/manifest/archive以及即使重写外层ledger仍拒绝。统一derived registry和真实`deploy-android-shaders.py --verified-cache-derived`已host验证；增量发布必须先核准精确原device输入，复制而不硬链接，上传差异，再完整核准候选。不能冒用旧链或跳过真实deployer。

## 阶段性收益与当前结论

- 前一VS阶段的普通窗约43.2485→49.8556FPS，是主要收益；相配硬件主批22.914733→19.818330ms。该阶段CPU调频也明显变化，已分别报告。
- 本轮FS增量以当次新参考49.8835→50.4473FPS、主批19.770980→19.611331ms为准，收益约1%。
- “原始约43.25到目前约50.45”只能描述分阶段进展，**不是同一单pair结果，不能直接拼接、相加或排除热/工作量差异。**
- 省整数寻址和动态LDC的机制成立，GPU指令/主批也小幅下降，可保留；没有理由因增量小而撤回已经证明的省工。但仍不能保证全场景60、长期热稳态、或把当前实验写成production已发布。

主要本地证据：

- `.local/logs/native-bc3-three-arm-game-v1/ubo-address-game-v1/combined-game-v1/whole-analysis-agent/`：普通窗完整CPU/资源/健康资格与时间转换。
- 同目录 `gpu-tools/comparison.json`、`hardware-analysis-agent/formal-comparison.json`：本轮9项SDP、官方SQL、KGSL队列关联、时钟拟合和边界。
- `.local/logs/hdr-representation-compile-v1/`：真实9PSO、原厂IR3、完整source ledger、188证明所需分模块产物、combined构建/验证/差异清单。
- `.local/build/address-combined-derived-v2/vk-cf83dfece8e7/`：完整canonical candidate与独立验证record。生产发布状态需由后续正式验收收条更新。

## 普通桌面版交付

实际发布并独立验收的普通 APK 为 `c09ff45b3a39146c7b6f5ef612da1d867406cbababdc7ab178dc9d9fd8fce24b`，host ELF `b828245443280f607c24075e7bd930513b68a567f782d4122ce3645354c97a22`；采用本轮 `cf83dfece8e7` 完整认证 Shader 链、12b8驱动和原BC3共享资源。原游戏28个模块未改。新schema6桌面配置绑定完整APK/驱动/Shader链/表/pack/producer身份，未增加隐藏分辨率参数或修改系统属性。

该APK无诊断wrapper，谓词观察默认关闭，保留已另行验收的covered-clear默认开启。它不是性能对照所用498APK，故没有把50.45FPS直接标成日常版的已测帧率。

两次均只用标准 `MAIN`/`LAUNCHER` 启动，无Intent extras；沿原角色选择UI进入游戏，确认真实HUD、原生模块、实际新host/私有驱动映射、共享heap/native资源、inline CPU7与缓存策略，然后由原Android生命周期得到fresh `commits=2`并停机。功能测试使用受控角色；最后从保留的原备份复制恢复全部22个原用户存档文件，逐项验证且检查没有硬链接，恢复后没有再次启动用户存档。原6文件资源目录、B3驱动、旧profile/receipt均留有可核查回退备份。

实际普通桌面输出两次均为 **2358×1080、3D比例0.75**，保留原用户偏好；与实验1968×900/.85不同。不得借交付行为改变用户设置，或直接承诺不同像素负载下同为50.45FPS。SharedPreferences前后完整相同。交付结束游戏与本轮SDP GUI/全部collector均已停止；Mali和其它平板未参与。

本地正式凭证：

- `ubo-address-game-v1/production-publication-agent/actual-publication-v1/acceptance.json` 与固定 `journal.json`：两次普通冷启动、实际身份、存档恢复、文件CAS备份。
- 同目录 `launch-profile.properties` / `launch-profile-receipt.json`：当前桌面绑定。验收状态另存，不为了改变状态回写已绑定receipt。
- `ubo-address-game-v1/device-transaction-handoff.json`：当前已采用普通c09/cf83；旧实验的reset/restore已退休并加拒绝执行保护，不能再用它覆盖新桌面配置。
- `combined-game-v1/gpu-tools/close-receipt.json`：本轮所有拥有的官方工具进程均已确认退出。
