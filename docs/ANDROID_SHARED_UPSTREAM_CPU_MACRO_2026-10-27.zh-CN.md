# 同 B3 的上下游 owner 宏观指令预算复核

本次只整理已有官方 Simpleperf/Perfetto 原文件，没有 ADB、采集、计时器、生产改动或性能开关实验。

## 结论

“70.9M 对 48M”这个 whole-owner 差距成立，但不是“主线翻译器多用了23M”的证明。可核查的采样差额约23.108M，其中 host 17.588M、原游戏/SDK/未知模块3.763M、B3编码1.368M、动态TLS resolver0.628M，libc反而少0.239M。当前主线 memcpy/memmove 已少2.711M；共享内存确实省了搬运，但额外的前瞻规划、资源消费/依赖记录以及SDK路径仍存在。

最大的独立已定位差异是**主线额外的整提交规划6.974M**。另一大项是**每draw仍分阶段处理的资源职责**，单纯把其中一个末端cache改快无法取消整个流程。还发现此前被叫作“游戏/unknown”的差额中，有**SDK原子owner-word锁实现**，不应归入NVN翻译，更不能直接称为锁等待。

没有依据把这23M全部归给copy、driver或互斥锁，也没有证据证明可直接安全删去某个23M大块。

## 1. 窗口和符号资格

| 项目 | 新主线cacheON | 上游B3 |
|---|---|---|
| 原窗口 | `record-state-cache-v1/pmu/PMU-ON1-BASE` | `remaining-upstream-gap-v1/DRIVER-T1-PMU` |
| PID/TID | 30592/30642 | 29065/29149 |
| Surface完整间隔 | 860 | 794 |
| 原窗口FPS | 43.1804 | 39.9833 |
| MONOTONIC边界 | 126530226948188—126550143387868 | 112649516201712—112669374476548 |
| exact instructions:u / Surface | 70.9012—70.9523M | 47.9690—48.0033M |
| exact cpu-cycles:u / Surface | 28.6911—28.7143M | 23.3932—23.4103M |
| 指令采样period合计 / Surface | 70.9297M | 47.8218M |
| 实际调用链 | DWARF32768、post-unwind、no-callchain-joiner | 未启用 `-g`，2932个窗内样本链深均0 |
| host BuildID | `0ea8fc4326161fec14530b9ad27c6cd9f40c0022` | `834ddc22ce07a9f3b283324042b09df2dbb973bc` |
| host SHA256 | `d327a5a261ce58cc47405b6c098a92bd2d24b3c4418900027d2cc0651fd9634c` | `13565f6df8c9e3054268cda5b03b2c19d47a3bdc4798d936111b20fa8104d364` |

两边均为同机Android16官方Simpleperf `instructions:u -f199 --add-counter cpu-cycles:u`、clockid=monotonic、无lost/truncated记录；计数差分使用各自官方Perfetto完整Surface边界与相邻累计counter样本夹界。每条原record的SHA与receipt复核通过，host与B3的真实record BuildID均与符号ELF一致。B3两端SHA同为 `b3fce5ab29c6617bd2cf334b3a56c67951de271bd870878c9a3121ff606b1b9f`。

资格包含：1968x900游戏输出、DM_3D_SCALE=0.85（1673x765主3D目标）、营地区域1、原对照角色和同B3 prefer_sysmem/UBWC。原main/sdk ExeFS SHA与原剪口语义已有独立核对，见 `resource-preparation-audit-next/original-module-and-rule-comparison.json`。两宿主的共享native路径、纹理/Shader加工表示、游戏装载和TLS桥不同，因此这是整条宿主实现对比，绝不是只切换某一算法的A/B。

主线同窗Perfetto owner Running18.809ms、whole app35.148ms；上游同窗分别19.474ms、41.529ms。主线虽然多指令，但owner Running并没有多相同比例，全app反而更少。每指令周期、CPU频率、访存等待和游戏worker不同，**不能按23M/70M推算可省几毫秒**。

上游无-g、无owner NVN phase slice。本报告不为它拼接其他System驱动/旧DW窗口的祖先；其下表只给静态PC/源符号的SELF集合。源行使用官方NDK llvm-symbolizer内联信息，未把它当运行调用链。

## 2. 可直接比较的模块预算

单位：M sampled instructions / Surface。各行互斥、可加和；是采样归属估计，不是每模块精确counter。

| 叶指令模块 | 主线 | 上游 | 主线－上游 |
|---|---:|---:|---:|
| host | 45.353 | 27.765 | +17.588 |
| 原游戏/SDK及其他未知 | 13.636 | 9.874 | +3.763 |
| B3 driver | 5.665 | 4.297 | +1.368 |
| libc | 5.487 | 5.725 | -0.239 |
| linker动态TLS resolver | 0.789 | 0.162 | +0.628 |

主线 `__memmove_aarch64_simd` SELF1.224M，上游3.936M，已少2.711M；主线 `__memcmp_aarch64`1.253M，上游1.021M，仅多0.232M。不能再用“memcpy很贵”解释主要差距，更不能在共享后继续把所有host工作都叫脏页/上传。B3的总编码差额1.368M也解释不了host的17.588M。

## 3. 两边各自的宏观工作

主线：真实DW祖先与同record实际最内层Perfetto phase相交，planning嵌在replay里时只计planning，不重复相加。

| 主线互斥职责 | M / Surface |
|---|---:|
| phase外游戏/其他宿主/未恢复父链 | 13.822 |
| NVN录制（实际祖先证明） | 0.617 |
| HLE线程服务/短等控制 | 0.597 |
| phase外其他公开NVN API | 0.985 |
| 整提交planning | 6.974 |
| replay内 CPU source preflight | 3.093 |
| replay内 bindings准备父项 | 12.502 |
| replay内 state应用 | 3.429 |
| replay内其他draw职责 | 21.009 |
| replay内其他已知职责 | 1.868 |
| replay切片中缺失replay祖先 | 5.886 |
| present | 0.149 |

不能把“其他draw21M”当一个叶热点。实际直接子项再拆：draw SELF/内联4.885M、pool_address1.960M、shader_prepare_images1.773M、texture_shader_record1.627M、pipeline_prepare1.286M、uniform specialization0.551M、image_dependencies_batch0.529M、ubo_plan_admit0.490M、buffer_shader_read0.482M、program_shaders0.429M。其余原样保留在JSON。

上游：以下是**可识别源码SELF集合**，含编译器已内联的代码；独立标准容器函数只有类型/符号足以说明职责才分入，不能认领libc/driver祖先。这个表不能逐行与上表相减。

| 上游静态SELF集合 | M / Surface |
|---|---:|
| resource+descriptor+snapshot明确集合 | 15.148 |
| command decode / state execute | 4.503 |
| draw+PSO+attachment明确集合 | 4.844 |
| NVN录制/API thunk | 1.338 |
| HLE/gate明确集合 | 0.163 |
| 未分类host | 1.769 |
| 游戏/SDK/其他未知 | 9.874 |
| libc（父项未知） | 5.725 |
| B3（父项未知） | 4.297 |
| linker | 0.162 |

上游 `submit()`直接snapshot_reset后沿handles `execute()`，没有主线 `submission_index_build→walk→逆扫DS/query→Store finalization` 这条额外全量流程。它仍有4.503M decode/state，不能把主线planning与自身state应用简单合并后全当“上游没有的浪费”。上游的resource集合包含snapshot、UBO缓存和内容哈希；它不是没有每draw准备资源，而是用了更简单且不同的消费/镜像契约。

## 4. 不是draw多了几倍；是同批draw的管理阶段仍多

新主线本窗前后原state快照横跨895次guest submit（略宽于860个Surface窗），PSO准备尝试2419.143/guest submit，internal submit1.0/guest submit。上游B3同次运行稳定日志为2421 draw/frame、10 dispatch、54 pass、1 submit，不能证明逐条动态流相同，但足以排除“主线多画数倍”。主线PSO是尝试代理，不把它偷换成精确issued draw。

主线同宽快照每guest submit：

- 实际View消费 = views4163.253 + misses644.706 = 4807.959。
- Pool命中9278.149次，范围处理6770.285次，slow仅2.001次。
- 最终descriptor内容命中4498.207、miss350.079，命中率92.78%。分配仅0.034次、0.046set。
- dirty proof命中2191.740次、真正check7.098次。
- ordinary image-buffer子组build1782.101、hit610.042；被跳过items6559.042。

这些次数说明当前大头是**重复进入资源准备与消费契约**，不是descriptor重新分配、资源首次加载或全池重扫。View/Pool缓存已命中，命中后仍有CPU源资格、shared buffer/image别名、stage/range可见性、image layout、当前recording生命周期保活以及descriptor实际内容等职责。

单draw实际流程仍是：CPU source（publication）→ Pool operation/admission → VBO/IBO/UBO require → attachment准备 → shader image准备 → descriptor/真实View消费 → image/Buffer读写登记与dependency提交 → driver encode。绑定不变并不自动使全部步骤重复无用；“末端set命中”发生在许多步骤之后。

仅用互斥父项和真实draw直接子项圈出的资源工作包：source3.093 + binding12.502 + draw下pool_address1.960 + shader_prepare_images1.773 + texture_shader_record1.627 + dependency_batch0.529 + ubo_admit0.490 = **21.974M**。这是调查覆盖面，包含真实必需工作与已有快路径；不是可删21.974M的预算。当前有source-value handoff、ordinary子组、附件/raster复用，不能再把它们提作未实现的新发现。native_front与source_partition在本窗均实际关闭；旧实验有自己的适用性/管理成本结论，不据此盲开。

## 5. 重要资格纠正：游戏/unknown里有SDK原子锁代码

利用各自启动日志的真实main/sdk base，把官方sample原IP归一化：

| 原模块代码 | 主线 | 上游 | 差额 M / Surface |
|---|---:|---:|---:|
| main | 7.936 | 6.789 | +1.146 |
| sdk | 4.958 | 2.548 | +2.410 |

两端SDK最重的同两个offset `+0x3517b0 / +0x351854` 合计主线3.861M、上游2.081M，多1.779M。未改写原NSO只读解压反汇编证明：前者是owner-word LDAXR/STLXR acquire函数成功返回段，后者是release成功后检查waiter bit30。这不是采样到VS/资源处理，也不是GPU waiting。

特别限制：热点PC本身位于成功路径/尾声，不能据此断言自旋次数多或发生锁争用。PMU skid/周期自适应会影响固定短函数归属。当前对应SDK区间173个DW样本均未恢复出父链，因此不知道这些锁由哪类游戏工作触发，也不知道实际调用次数、锁地址或等待者；不能造出一个“SDK锁占4ms”的结论。它解释的是为什么不能把whole-owner差额都归翻译，不是一个允许删除原游戏同步的依据。若继续查这块，应先利用原SDK符号/正式调试工具解决父调用与对象身份，不能直接改锁协议。

## 6. 优先级和可去掉整段流程的边界

1. **规划的结构生产/消费融合**：当前独立6.974M，旧8M不能继续沿用。录制cache已把new record44,844降至31,600（-29.53%），但planning仅7.727→6.974M，说明收益受draw级Store/依赖事件和逆向证明约束，不能期望按OP条数同比下降。可调查在录制时直接生成已知OP结构/局部状态摘要，submit只组合动态部分，避免再走同一批头部/固定结构。必须用权威录制代数和CALL/JUMP子段覆盖；若只是EndRecording再扫描或每submit再hash，没有少做完整工作。本窗planning6.974M是父项上界，不能承诺全删；Store、query及跨handle初始状态仍要真实解析。这个方向已有生命周期审计，尚缺命令不变/复用率资格，不应换名重做同一个末端cache。
2. **一次生成同draw资源消费包，多个阶段消费**：调查覆盖21.974M，应先证明哪些实际阶段对同一逻辑slot重复解析/准备、哪些是新的writer/layout事件；目标是取消一个完整阶段或使纯native/immutable类别直接不进入它，而非给21M再加一层逐项key检查。现有source handoff只移交逻辑source，不是可跨writer复用的physical完成证明；已有ordinary/native子组实验必须纳入，不能当空白重写。没有当前命中覆盖/失效原因数据前，预计收益只能圈父项，不能给净省10M的数字。
3. **分离SDK原子协议和真实游戏预算**：额外SDK2.410M值得独立查资格，尤其两个同offset1.779M；它不是资源准备的优化成绩或失败。先恢复父调用，不改原游戏锁。
4. **不把小项升格**：driver总额外1.368M、TLS额外0.628M、memcpy已省2.711M、最终set分配近零，都不能单独解释整条23M差距。若不命中前两类完整职责，继续对容器/原子/比较做零碎修改不会自然消掉这条路径。

本报告没有提出可立刻安全删去全部planning或全部resource包的补丁，也没有把指令差额包装成已获得帧率收益。下一实验应以“哪一整段不再执行、仍保留哪些动态语义”为验收条件，并沿现成官方工具复核，而不是只报告末端cache hit更多。

## 可复核产物

本目录：`flat-budget.json`（两端）、`host-PC-source.json`（NDK官方inline/source行）、`official-report.csv`（官方CLI静态SELF）、`macro-comparison.json`（全部分类成员/真实主线子项）、`unknown-game-address-budget.json`、`main-SDK-atomic-callers.json`、`original-SDK-owner-word-atomics.txt`。`export-existing.py`和`summarize-existing.py`仅编排官方离线ReportLib/报告/符号化，不采集、不计时、不生成合成祖先。
