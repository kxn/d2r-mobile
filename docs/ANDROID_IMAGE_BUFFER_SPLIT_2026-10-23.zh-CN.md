# Image 与 Buffer 消费拆分：覆盖、预算与实验边界

状态：完成覆盖/成本审查、当前稳定 APK 的 LLDB 只读元数据核查；已在独立工作树实现候选 e3bc14a，完成 host 正确性检查与规范构建，已完成 Q735 真机两对同 APK 开关交错测量及前后旧包对照。机制省工成立，净 CPU 毫秒收益未稳定。默认启用实现已合入主线；默认包实际游戏验收通过，但桌面发布未取得所需 Java profile 标签，已完整回滚旧日常包并结束重试。以下先列实验前预算，再记录实际结果，不能混用。

目的不是再增加 getter 缓存，而是在 Buffer 地址/偏移变化时，不重新准备未变的图像子集。目标仍是降低 NVN 翻译线程 CPU 时间；本项不能单独承诺达到 60fps。

## 证据分开使用

| 证据 | 版本/范围 | 能回答什么 |
| --- | --- | --- |
| 官方 GFXReconstruct 单帧解码、SPIRV-Cross/spirv-dis | 历史 `533820e`，frame1049，2312 draw、8次Vulkan提交 | 图像槽身份、实际生产者、同pass内重复；不是当前CPU耗时 |
| 官方 Simpleperf 六窗与匹配 Surface 分母 | 当前 `10d/28a`，1387个Surface周期 | 互斥调用职责的CPU成本；不含每个纹理身份/缓存分支结果 |
| 官方 LLDB 一次停止进程只读快照 | 当前稳定10d APK，PID25281，原游戏进入营地 | 现有物理image的来源/布局字段；不是时间、频率或动态命中率 |

这些证据不能拼成“当前已省了多少毫秒”。所有资源/采样分析脚本只整理现成工具输出；本轮没有新计时器或采样器。

## 覆盖：稳定的是子集，不是完整描述符集

比较边界为相同实际shader/layout、physical pass、command buffer及中间无硬命令的区间：

| 片元图像槽访问 | 全帧 | 主要1650×756 pass |
| --- | ---: | ---: |
| 总访问 | 10,701 | 9,109 |
| 每个连续组首次访问 | 2,039 | 1,782 |
| 后续身份变化 | 1,210 | 998 |
| 后续身份不变 | 7,452 | 6,329 |

69.64%是全帧的**身份重复率**，不是已满足所有有效性条件的命中率，更不是CPU节省比例。仅接受整组完全相同，只覆盖3,593个重复槽；允许部分槽变化，才覆盖上表的7,452个。全GENERAL整组策略只有32个后续槽，不能继续把native-only作为大覆盖前提。

主pass几个主要shader分别有15/16/13个图像槽，其中12/12/10个在该pass的该shader使用序列中身份保持不变；其余材质图像或Buffer偏移在变。跨非连续shader使用的统计只是身份，首版不据此省跨边界同步。

主pass的6,329次不变访问，按本帧前置生产者分解：

- 1,841次：前置RT写入/清屏的结果。
- 1,968次：前置compute写入的四张3D图像。
- 2,520次：捕获内未找到图像生产者，来源未知，不能直接称为静态资产。

四张3D图像均为32×8×32、单mip、RGBA16F，由实际dispatch写入；主pass607个draw每次都采样这四张。扣除每个连续shader组的首次消费，才是上述1,968次潜在重复。首次写后读依赖必须执行，后续真正无写才可能省掉重复准备与读历史维护。

### 按实际首版形态重算：只有一个最近FS图像组

不是理想的逐槽独立缓存。冷路登记当前符合条件的图像子集；任一登记槽身份变化、shader/layout或硬命令边界变化就整组重建，无关材质槽/Buffer变化不失效。历史主pass607draw的序列模型为：

| 已知来源子集 | 总槽访问 | 冷建次数/消费槽 | 热组次数/可省槽 |
| --- | ---: | ---: | ---: |
| 四张compute图像 | 2,428 | 115 / 460 | 492 / 1,968 |
| 前置RT+compute图像 | 4,695 | 115 / 886 | 492 / 3,809 |

115次冷建包括首次1次、shader/layout变化105次、硬命令边界9次；这些已知子集没有组内图像身份变化。492次热组中，310次有其他材质槽变化，447次动态offset变化，133次Buffer描述符内容变化。这说明一个最近组也有覆盖，不需要先扩为跨帧或多shader缓存。

这仍然是历史API身份序列模型：无法观察NVN重复setter、当前has_layout/external标记、CPU发布及底层别名失效，不能当作当前候选的实际命中或耗时。证据：`.local/logs/nvn-image-set-stability-v2/single-last-plan-model.json`。

## 当前真机元数据：已确认存在合适的GPU图像类别

不替换APK，在当前稳定包中使用NDK LLDB读取完整image registry，共2,878个对象；其中80个 `has_layout=0`，1,308个有native entry。四张32×8×32 RGBA16F图像（当前serial54–57）均为：

```
tex.has_layout = 0
native_entry = 0
external_access = 0
shared_writer = 1
shared_alias_linked = 1
```

这证实当前实现中有“不从CPU packaged纹理上传、也不是native导入”的GPU生成图像。尺寸/格式与历史四张吻合，但没有跨版本对象ID映射，不能把历史1,968次直接称为当前资格命中数。

`shared_alias_linked=1`是逻辑内存别名登记，不单独证明VkImage物理存储共享；同样不能忽略它。涉及原NVN地址的Buffer读取/转换、写入和layout变更仍需原语义及有效性失效。

本次明确detach成功，TracerPid=0；原Android生命周期存档commits=2后停止进程，移除本次lldb-server和forward。未操作Mali或平板。执行的是原稳定ARM64游戏模块，未修改游戏产物、驱动或资源。

## 时间预算：先限定可能删除的职责

当前六窗按同一Surface分母互斥归类：

| 研究职责 | 宽松CPU包络 ms/Surface |
| --- | ---: |
| 来源枚举/逻辑元数据解析 | 0.881 |
| image prepare整个入口 | 0.568 |
| view元数据、查找与request构造 | 1.020 |
| sampler | 0.138 |
| 合计 | 2.607 |

这是含必要工作的包络，不是全部可删。六窗合计范围2.372–2.892ms。

为了决定是否值得实验，作明确假设的敏感性计算：假设历史身份分布适用于当前、各槽成本均匀、重复槽都满足有效性条件，则上述包络中可删工作比例为25%/50%/100%时，毛节省分别约0.454/0.908/1.816ms。真实结果还必须扣除setter比较、失效传播、冷重建与生命周期维护。这是实验预算模型，不是预测，更不是实测收益。

另外，读后历史整桶1.063ms中，明确native调用路径约0.204ms须排除；余下0.859ms仍含附件、写者和身份不明工作，不能全部算成普通GPU图像可省时间。不得与上述包络或其他父函数重复计算。

只做普通GPU图像子集时，CPU来源与Pool admission先保留，sampler也可保留，因此不能声称这项窄实验具有完整1.816ms预算。若仅省元数据工作，即使达到该理想模型，约26.6ms的全翻译线程仍约24.8ms，距离16.67ms尚远；后续还需其他结构改造。

## 实验实现必须满足的形态

1. shader构建时把现有consumer schema分为Buffer与image，并预编译NVN槽到实际消费项的映射；不在draw时重建映射。
2. 变化在TEX/SAMP/IMG setter和物理资源生产/替换入口标记。材质槽变化不能把未涉及的GPU图像槽一同失效；现有整stage c2版本不能直接充当这种证明。
3. 冷路直接从原成功消费产物建立计划，首次真实draw和读历史完成后才发布可复用状态；不是先新增一遍解析。
4. 热路只处理变化或不合格子集，省掉合格稳定子集的prepare、view解析、request追加、重复依赖和读历史循环。若实现仍逐图验证、再建立另一张列表，视为不满足实验目的。
5. 首版优先核验普通私有GPU生成图像：非native、无packaged CPU来源、无external access；不按具体纹理尺寸、shader哈希或游戏场景定制。其他图像和Buffer保持原路径。
6. 保留CPU preflight与Pool admission。`has_layout=0`不等于所有外层检查都可删除。不得为增加命中清空NVN barrier或吞掉待处理工作。
7. 资源写、layout、descriptor/物理替换、目标改变、mip事务、pass/recording边界和重入必须使对应计划失效。无关RT每draw写入不能全局破坏稳定图像计划。
8. 当前绑定后仍可能有attachment/query/VBO/IBO回调。必须在不可重复的query副作用之前保证回退完整，不能混用旧descriptor/Buffer句柄，也不能以逐图最终验证掩盖失效合同缺口。

实施后先用已有契约测试验证正向省循环和反向失效，再进行相同APK开关交错、官方Simpleperf/Perfetto测量。报告必须同时列旧循环消失量、新setter/失效成本、CPU与整帧结果。后面的实施与真机结果另列，不将预算当作实测。

本地证据：`.local/logs/nvn-image-set-stability-v{1,2}/`、`.local/logs/nvn-image-plan-cpu-budget-v1/`、`.local/logs/nvn-image-group-invalidation-v1/`、`.local/logs/nvn-image-group-current-metadata-v1/`。


## 已实施的整段省略，而非逐图再检查

候选 `e3bc14a05dd26594cbac28dfdceb0c4a860b2f15` 位于独立分支 `codex/image-buffer-split-v1`。普通 FS 消费完符合条件的图像子集后，保留同 physical pass / recording 的整组消费结果。后续 Buffer 偏移或无关材质变化时，复用组内 descriptor 值，并从 prepare、view 解析、request 构造、依赖与读历史循环中跳过这些成员。当前 batch 只保存一个组引用，不在每 draw 创建另一份图像检查列表。

资格限制为普通私有 GPU 图像：`!native_entry && !tex.has_layout && !external_access`，并排除当前附件别名、可写 shader、特殊 local/quad 路径等。首次读前的真实生产者依赖保留；第一次实际 draw 的访问登记完成后才发布有效组。TEX/SAMP 中相关槽真正变化、layout/物理对象更换、写入、pass/recording 或重入等边界使组失效。Buffer、CPU 来源预检、Pool admission 和最终 descriptor set 处理仍走原路。

新增成本包括每 image 8 字节 watcher、一个上限 128 项的静态组、setter 失效判断、冷建和事件物化。语义计数器只统计实际成功 draw 的命中与跳过项，提交时公布六个原子快照；没有新增计时器、采样器或逐 draw 原子统计。

独立审查发现并修复了一项实质缺陷：回调中物化组可能令 request 数组 realloc 或增长，依赖处理不能继续使用旧 pointer/count。现已在回调后从稳定 batch 身份重新取数组，写入在原追加入口失效，不新增整组写者扫描。

## 正确性与可复现部署

- 10 项正常 host 回归及 4 项独立 ASan/UBSan 组合全部通过。包含实际组命中、嵌套 ghost、promotion、retired handle、强制扩容、不同 mip、跨 pass、CPU 同值写与实际 GPU 像素检查。证据：`.local/logs/nvn-image-plan-independent-validation-v2/`。
- Host 使用软件 Vulkan，只证明这些 NVN/Vulkan 图形契约，不冒充 Android 游戏原生运行或硬件 ASTC 验收。
- Android 保留 285 项既有编译命令与 535 个 ABI root / 524 个 hidden fast root；六个库与 UI 资产完成核验。没有改游戏逻辑、driver 或纹理资源。
- 从原始 NSO 经规范入口重新生成并独立 verify；23 个执行/规则产物及 build_src 与手机既有已验证模块字节相同，部署保留这些模块。原始输入、源码指纹及产物 SHA 见 `.local/logs/image-buffer-split-v1/raw-ready.json`。
- 候选 APK SHA256：`ed93095d7c33b0f29cd65f583e5d5c10468770d85a1f857563d97fa14cf537d9`；ELF Build ID：`b01709dc52d5b3b18869a731baf7480a6b723bb5`。APK 规范构建记录：`.local/logs/image-buffer-split-v1/ready.json`。
- Q735 实际执行原 ARM64 游戏模块进入营地。比较固定 2750×1260、3D 比例 0.60、目标 60、external-profile/noADPF、相同 Turnip 和 native 资源；没有绑核或强制频率。每轮由原 Android 生命周期存档 commits=2 后停止；Mali 和 A650 未操作。

## 真机结果：省工复现，净毫秒未稳定

使用现成 Simpleperf 的 CPU-clock 调用栈及 PMU、Perfetto 的 sched/Surface 窗口。CPU 时间以相同窗口的 Surface 间隔作分母。PMU 是独立窗口，没有精确 Present 分母，不能换算 instructions/frame 或用另一窗口 FPS 归一。

实际顺序为 C1 → B1 → A1 → B2 → A2 → C2。C 是原稳定 APK，A/B 是完全相同的候选 APK 的 OFF/ON；主线默认 ON 配套没有混入测量。

| 轮次 | 状态 | replay 主体 ms/Surface | 整翻译线程 Running ms/Surface | Surface Hz | 线程运行期间加权频率 MHz |
| --- | --- | ---: | ---: | ---: | ---: |
| C1 | 原稳定包 | 23.301 | 27.148 | 28.46 | 1482.98 |
| B1 | 候选 ON | 21.983 | 25.631 | 30.30 | 1545.87 |
| A1 | 候选 OFF | 23.414 | 26.879 | 30.14 | 1587.99 |
| B2 | 候选 ON | 23.080 | 26.751 | 30.18 | 1539.05 |
| A2 | 候选 OFF | 22.508 | 25.808 | 30.10 | 1651.09 |
| C2 | 原稳定包 | 23.234 | 26.836 | 30.27 | 1566.83 |

第一对 ON 减少 replay 1.431 ms、整线程 1.248 ms；第二对 ON 却增加 replay 0.572 ms、整线程 0.943 ms。第二对 OFF 频率更高约7%，但不能据此进行线性校正、声称原频率下必有收益。结果不能证明稳定净省1.43ms，也没有可靠FPS提升。旧包前后replay较接近，不代表候选组管理的成本为零。

B2 相比 B1 整线程慢1.120ms，其中1.0965ms来自 replay 主体；replay内其他子marker及外部工作净差仅约0.0235ms。变化主要不在queue submit、present或提交规划。记录调用栈与scheduler是不同窗口，样本也不足，不能将这个差值硬归给某个具体函数或锁。

工作量证据（分母审计后修正）：B1/B2 的 scheduler 外包络中，成功组命中数与 `stream_submits.draws` 增量之比分别约24.59%/24.70%，跳过项数与同一分母之比为2.696/2.708，冷建调用数与该分母之比约74%。**这个分母在分块提交时累计 `V.draws`，包含部分非图形操作并可能遗漏流尾，不能称为全部 graphics draw 数，也不能将这些比率称为精确 draw 命中率。** 成功命中和跳过项本身有语义计数证据；同一 ADB 外包络的次数不能与裁剪后的 Perfetto 帧数混算成每帧节省量。实现只跳过合格子集，没有删除命中 draw 的全部准备。

互斥 CPU 样本中：

| 职责 | A1 OFF | B1 ON | A2 OFF | B2 ON |
| --- | ---: | ---: | ---: | ---: |
| 图像 view 路径 | 64 | 30 | 48 | 33 |
| 新增组管理自身 | 1 | 10 | 1 | 12 |
| 剩余混合绑定（含Buffer） | 73 | 81 | 71 | 83 |
| 图像读写历史 | 24 | 23 | 24 | 21 |

图像 view 减少复现；组管理新增开销也可见。剩余混合绑定不能全算组维护，历史路径没有足够证据称为大幅下降。B1/A1 record 的 shader variants 都为30，B2为32；各独立工具窗口需保留身份和稳定性审查，不能直接把几个样本换算精确毫秒。

结论：保留并在既有严格资格范围默认启用，因为实际删除重复工作且未发现画面/生命周期回归；不扩大到native导入、不放弃CPU发布语义，也不因此声称23ms主体问题已解决。新的设置支持明确OFF以继续对照。下一步若扩 native，必须先建立整组对distinct Pool的真实所有权与失效合同；不能再套逐图valid列表。现有native单独可识别调用预算只有约0.284ms，通用父热点身份未分清，尚无依据宣称扩native就能省几毫秒。

完整原始记录和逐窗审查在 `.local/logs/image-buffer-split-device-v1/`。不稳定的 PMU 窗口已单列，不能参与跨轮硬比较；初始未知 thread_state 的小段不影响已核验的 Running/exclusive，但不能声称其 Runnable/Sleep 完全覆盖。此为短时营地对照，不是长时热稳定、跨关卡或所有GPU验收。


## 主线保留状态

实现合入 `0bd31cd`，默认启用与明确关闭控制为 `0f9eb3b`。启动参数 `--no-image-buffer-split` 可做原路径对照；运行中不切换。默认资格与冻结实验相同，没有借默认启用扩大到native导入或跨pass复用。

默认策略改动后另跑6项启动参数自检、默认ON（无环境变量）与显式OFF两次完整host fixture，均通过；日志 `.local/logs/image-buffer-split-default-on-v1/`。默认包规范构建和原始NSO记录放在 `.local/logs/image-buffer-split-default-v1/`。它的启动/存档验收与冻结A/B性能数据分开，不把新包未经测量的CPU时间填进上表。


默认包已实际进入原游戏营地，默认开关为ON，观测到82,252次成功组命中、593,057个跳过项；原生命周期存档2次并停止，已恢复旧包供正式桌面发布前对照。该计数为启动至验收的累计量，不是新性能窗口。记录：`.local/logs/image-buffer-split-default-game-acceptance-v1/`。

最终默认包 SHA256 `c98cf422c800bdfcffd8b2e11d2a21fa38b0355a280873979fe15533bc9b8266`，ELF Build ID `38accb4b5a6b72aa4d6d204069d0b362c5247b5b`。实际7个APP单元预处理检查、六库签名/哈希及285编译命令与稳定版等价核验通过；原始NSO重新生成的28项产物已核对。主目录此前遗留可选validation layer会额外打入APK，正式包改在同commit干净源码目录生成，未部署该七库中间包；两个未完成审计产物和日志保留，未伪装为通过结果。


首次桌面发布在第二次原启动后未读到早期 Java profile tag，触发验收失败并完整恢复旧 APK/八目录/profile/receipt；两次游戏均到原 HUD、均 save2，未观察到对应游戏崩溃。v3 停止态实读与 v2 的全部库存一致，确认回滚完成。独立工具提交 `bdac84b` 在原等待HUD之前额外保存官方logcat早期快照，原等待后快照也保留，合并后继续要求成功tag且任一拒绝tag即失败；12项工具回归通过。没有降低验收条件、没有为这个日志问题改游戏运行时。失败记录保留在 `.local/logs/image-buffer-split-default-desktop-Q735-v1/`，后续发布另用v2目录。


第二次发布（v2）也在第二轮出现早/晚tag快照均空，尽管原HUD与save2成功；已再次完整回滚、释放工具锁。早/晚快照未能确定原因，不能把日志缺失直接认定为缓冲覆盖。下一次将从启动前用现成官方logcat连续记录并按本轮PID校验，保留原拒绝检查；游戏运行时不为此修改。

最后一次发布（v3）已在启动前打开连续官方logcat，仍未取得本轮Java profile标签，原验收拒绝，完整回滚。独立v5停止态库存与尝试前v4逐项一致；设备恢复旧APK `ec15c8…`、原模块及profile，存档commits=2，无游戏PID、专属logcat或遗留事务锁。候选曾到达原HUD并产生默认ON组命中，不因此越过交付验收。当前源码含默认ON实现，但**设备日常包仍是旧稳定版**。不再重试这项交付；主线回到翻译结构调查。最终记录：`.local/logs/image-buffer-split-default-desktop-Q735-v3/final-handoff.json`。
