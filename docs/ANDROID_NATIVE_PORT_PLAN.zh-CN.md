# Android 原生移植：现状与执行计划

更新：2026-10-17。目标是在原项目上补全 Android ARM64，约束见根目录 `AGENTS.md`。
真实游戏 ARM64 指令由手机 CPU 执行，沿用原离线改写、装载、宿主门和 HLE。
禁止 CPU 解释器、JIT、动态翻译及复制模拟器系统服务；继续复用原 GPU 着色器翻译链。

## NVN 范围与优先级

| 层次 | 原实现与对应 | 工作范围 |
| --- | --- | --- |
| NVN 对象/录制 | 原 hle_nvn.c、nvn_obj.h 与 ARM64 跳板 | 复用原入口和命令记录格式。 |
| 通用执行层 | 原 nvn_mtl.m → nvn_vk*.c | 优先资源、着色器绑定、状态、绘制、拷贝、同步与出图；不引入相机或场景语义。 |
| 原有游戏专用增强 | 原 bright_lut、ghost_*、frz_op、rt_* → 独立 Vulkan 文件 | 保留上游规则和默认开关，不新增 Android 专用游戏渲染逻辑。 |

相机矩阵仅属于原可选 rt_* 外推，不属于通用 NVN API；读取原常量不等于修改游戏相机。
基础 NVN 兼容性必须能在关闭该可选分支时运行。两后端均有 39 个已录制操作码的分派，
但源码分派齐全不代表所有格式、标志或绘制子类型已对应。非索引 QUADS 就曾是其中的缺口，
现已沿原共享索引模式补齐；继续按语义和 GPU 实际输出验收。

在保证显示正确的前提下，性能主线按当前 Android/Mali 真机 profile 制定。
[Metal→Vulkan 技术评估](NVN_SPECIAL_OPTIMIZATIONS.zh-CN.md)仅作参考，不先追求补完原优化清单；
每项改动由已观测瓶颈驱动，通过真机受控对照验收。原 Metal 未写附件 DontCare
和跨提交历史预测不能无条件成为 Vulkan 默认行为。性能定位优先厂商硬件计数器/时间线；
抓帧、同步校验、自定义逐通道计时和正常性能测量分开，记录采集工具的扰动。

当前真机以原生2772×1240输出；固定3D1.0的英文营地、原物品栏、触控移动、原后台存档和Surface恢复已有证据。
游戏仍沿原ARM64原生执行与原改写规则。没有加入模拟器CPU，也没有增加相机/玩法/binary补丁。

通用提交架构已完成按提交隔离的描述符、暂存、查询、临时视图、绘制任务与资源退役；GPU访问页/CPU写入范围保护
共享镜像，回读快照、事件与栅栏按真实GPU完成发布。出图的acquire/render-finished信号量按实际使用者回收，
内部提交不推进游戏栅栏。`--async-submit`已包含异步出图；`--stream-submits`可在真实目标切换处提前提交GPU工作。
两者仍为显式选项，默认保留串行参考。分批提交在同进程原生画面受控对照中复现约36.6%吞吐改善；
不能把它与其他APK/温度下的改善百分比相加。提交合同与检查见[资源寿命审查](VULKAN_SUBMISSION_LIFETIME.md)。

已验收默认项包括遮挡查询跨度、限定图形消费者阶段、非阻塞可选CPU证明、CPU缓存回读、按实际使用页上传、
附件依赖索引的描述符快照与空stage扫描省略。图像布局、storage偏移、Performance Hint、描述符集复用和shader整理
没有稳定净收益，未当作默认加速。不可变CPU已知来源拷贝可减少约42.8%回读，但帧率两轮约+1.5%/−2.2%，
目前只保留默认关闭的实验路径。所有GPU拷贝和完成栅栏仍执行；未知GPU生产者继续原回读。

性能仍未达用户要求的30FPS以上。厂商有效GPU ceiling为383MHz时，当前原生固定3D1.0稳定窗口约7.2FPS；
同进程自然降温恢复848MHz后，四窗合计13.33FPS、首窗15.91FPS。这是工作条件变化，不能计为代码收益。
Arm官方SDK计数器已按同一时间窗实际呈现对齐，采集10个shader core与4个L2切片；CPU/调度跟踪、抓帧、同步校验
和无跟踪性能对照分别记录。新的优先项是由这些工具核对GPU访存/编译成本与剩余同步范围，继续保持真实内容与画质。
详细对照及拒绝的候选见[当前Mali性能记录](MALI_PROFILING.zh-CN.md)。最新候选检查为宿主508933、Mali APK508938、Adreno APK505995项，
包含逐字节/像素比较；这些数量不代表玩法场景数，也不替代完整游戏验收。

最近修复了 CPU 纹理跨行读取覆盖填充/拒绝奇数步长的问题，并按原 Metal 的 NotMipmapped
修正 Vulkan 非 mip 采样模式与 LOD clamp。两台真机的完整 GPU 回归无校验错误；Mali
原营地/HUD和原存档恢复已验证。6 GB 骁龙平板真实游戏仍在菜单后被系统以 LOW_MEMORY
终止（exit-info PSS 4.5 GB），不能认为平板已完整可玩。Mali 营地约 7.36 GiB PSS 的实际
内存开销也须在后续工具分析中审查，不以改变玩法或丢弃画面缓解。

## 真机现状

PHP110 / MT6983 / Mali-G710 MC10 / 16 GB / Android API 36，实际内核页大小 4 KB。
调试 APK 已执行三份真实游戏模块，完成原 SDK 初始化与构造，进入角色选择及第一幕；当前沿原设置持久化为英文。
**尚未达到完整可玩验收；当前没有可信的整体完成百分比。**

已完成的真实游戏路径：

- 显示全部职业模型，通过 Android 系统键盘创建野蛮人 AndroidTest；触屏进入营地。
- 原 C 摇杆实际移动，营地 1 → 鲜血荒地 2；原寻路打开恰西对话、交易和储藏箱。
- 实际近战击杀，经验 0→21；原瞄准剪口与触屏拖动锁定后再次击杀，经验 21→54，松手后轴/键归零。
- 原免费回城模块实际开门，点门完成区域 2→1→2。
- 丢出起始药水后，真实触点拾回，腰带 4→3→4。此项不等于自然怪物掉落验证。
- 原物品桥选择卷轴、显示说明、拖到背包另一格；保存退出、重启后位置保持。
- 原交易路径出售鉴定卷轴，金币 0→1，再从货架买回，金币 1→0；保存成功。
- 完整 touch-run-v4 改写已部署（36 门、48 剪口、21 观测门），原移动端 RomFS 覆盖层与启动流程已接通。
- 原 v4 HUD 字形/数值已验：生命 55/55、法力 10/10、经验条、腰带四列数量；真实人数档 1→8→1、小地图→全屏图→小地图均通过。
- 真 HUD 绑定 getter 的 FP/对象身份验证通过，12 槽快照 ready、identity_scope=1、group_seen=63；点空 B 键，经原技能树与绑定框完成普通攻击绑定，原存档事务已提交。
- 保存退出、APK 重启读档和同进程 Surface 销毁重建已有实机证据。

原宿主的动作、光标、HUD、自动腰带、掉落过滤、秘境、编年史等 35 个功能模块已直接链接，原 diag/crash/目标门及原绑定桥也在 APK 内运行。
**链接与进入原泵不等于每个可选功能验收通过**。下面按真实路径补覆盖。

## 尚缺什么、按什么顺序继续

| 阶段 | 工作与完成条件 | 当前缺口 |
|---|---|---|
| 1 手机交互闭环 | 原触屏技能绑定、换组、物品拖放/双击、交易、腰带、地图、面板和存档往返 | 绑定写入后重载已过；其余手势/职业、更多物品类别、充能技能和手柄切换等覆盖 |
| 2 图形语义对齐 | 逐项对照原 nvn_mtl.m 已实现行为，补 Vulkan 对应，真实场景和 GPU 夹具验收 | 格式/视图、部分拷贝及其他实际原实现分支；实例步长 2/3 与非零 baseInstance 已验。当前游戏反射无描述符数组；不能用菜单出图代替 |
| 3 真实游戏覆盖 | 自然掉落与拾取、地下城/换图、技能特效、多个职业、死亡/复活、任务、仓库和增强功能 | 已有野外战斗，尚未覆盖完整游戏循环；禁止通过写玩家状态伪造结果 |
| 4 性能 | 保持显示正确，以当前Android/Mali profile选择工作并独立对照 | 已验收默认项与分批提交见当前性能记录；原生输出热态约7.2FPS、降温窗口13.33FPS，尚未达到30FPS以上。各次改善不能相加 |
| 5 稳定与交付 | 多轮后台/Surface/冷热启动、存档故障恢复、资源导入、诊断导出、发行 APK 与机型覆盖 | 原生 TLSDESC 无预加载 APK 已运行营地/移动并通过 3 轮后台恢复；发行配置/签名、16 KB 页真机与长期稳定性仍未验 |

每项完成后直接推进下一项，不等待常规许可；性能、稳定性和打包不会因为功能首通而被省略。
Android ARM64 是首个验收平台。ARM64 Linux 可共享 ELF/POSIX/Vulkan 核心，但窗口、输入和发行仍需另行完成。
当前 x86 Linux 只承担构建、资源处理、离线翻译和原创 GPU 夹具，不用 CPU 模拟执行游戏。
Switch 模拟器仅为可选独立对照，不是运行依赖，也不能代替原生手机验证。

## 构建与验证

```sh
scripts/dev/build-android-native.sh --touch --tlsdesc
python scripts/dev/build-android-apk.py --tlsdesc
adb install --no-incremental -r .local/build/android-apk/d2r-native-tlsdesc-debug.apk
```

`--touch --tlsdesc` 将 HOST_ABI=2 完整原规则产物放到 `.local/build/native-touch-tlsdesc-rewrite`，并生成原资源覆盖层
`.local/build/android-romfs-overlay`。不带它的 `--rewrite` 保留 CLI 边界所用的独立 native 缓存。
initial-exec 旧调试路径仍可用 --touch 不带 --tlsdesc 构建，HOST_ABI=1；不能混用 ABI 产物。部署路径和参数见 `dm/android/README.zh-CN.md`。

- 原资源冻结校验已解决：编码器为 zlib-ng 兼容模式，构建脚本只给生成子进程加载固定库。
  原 12 张 PNG、2 份官方表、3 处 C 元数据全部通过，未放宽哈希或改测试期望。
- GPU 夹具 488629 项已在 Linux lavapipe 与 Mali 通过；APK 证据使用包内 Khronos 同步校验层，CLI Mali 运行不具备该层。
  新增原截图队列/延迟/裁剪/PNG 输出，另用 Pillow 对实际像素解码核对。
- 原首绘覆盖证明已接到 Vulkan loadOp，真实游戏至少 1800 次省略附件 LOAD，换区/英文物品栏/后台保存回归通过。
  V2 缓存的 6931 份 SPIR-V 与旧库全部相同，20792 文件经手机全量验哈希后发布。见 android-vulkan-load-cover-verification.json。
  Arm 原厂计数器已可采集；Android 覆盖层修复前，完整 SurfaceFlinger 上屏窗口约 7.4–7.6 fps。一次采集开销对照约 0.54%、后续一轮约 1.99%，
  都是短顺序窗口，不能当零开销证明，也不能用旧/新进程差异宣称 LOAD 优化的受控提速。
- Android 覆盖层与管线查找优化由实际 CPU 热点驱动，不按 Metal 清单逐项推进。已解析状态复用完成
  最终版本 130056 次真实游戏完整键核对、换区/物品栏/存档和同步校验；性能独立对照及复用证明发现的 Mali 填充字节问题
  见 [真机性能分析](MALI_PROFILING.zh-CN.md)。
- 原生硬件夹具完成 200000 次并发原子增量、信号、观测门组合与 SHA-1 向量；原绑定身份夹具 63 项通过。
  新 TLSDESC 夹具覆盖普通 dlopen、加载前既有线程、64 并发线程、NZCV/SIMD 保持和 ABI 不匹配拒绝；IE 回归亦通过。
  依赖私有映射/绑定文件的 t_native_hud_bridge 曾因未提供参数失败，不能把该次调用算通过。
- 原 Metal 调色表加亮已对应到 Vulkan，真实游戏 32³ RGBA16F 表已执行，直接沿用未改动的 dm_bright_k。
  GPU 参考值对照、固定曝光英文营地与默认动态曝光通过，游戏模块不变；原可选冻结世界绘制分支也已有 GPU/实机证据。运行时外推的 GPU 内核/通道角色夹具已通过，
  包含原生 D24S8/D32S8 及 Mali 两趟模板路径。真实游戏已识别 D24S8，营地/野外实际编码外推，
  移动、1→2→1 换区、物品栏、同 PID 60→30→60 及后台保存/恢复通过；更多最终合成场景、
  受控性能验收及其他图形增强/诊断仍待完成。
- 6931 个离线 Vulkan 1.1 SPIR-V 模块已校验。所有游戏素材、密钥、派生数据和存档保持忽略。

私有证据在 `.local/logs/` 与应用 `files/runtime/`；关键记录为
`android-original-binding-success*`、`android-original-ui-drag-reloaded.png`、
`android-original-shop-{sale,buy}*`、`android-vulkan-full-touch.log`。
HUD/地图证据为 android-original-hud-* / android-original-map-full.png；无预加载证据为 android-tlsdesc-*，
GPU 校验为 android-vulkan-tlsdesc.log。完整可玩验收仍为未通过。技术细节见 [TLS ABI](ANDROID_TLS_ABI.zh-CN.md)。

## 最近完成：后台存盘和呈现恢复

沿用原 diag.c 的 dm_save_wait：营地后台约 361 ms 提交两次；暂停菜单后台约 323 ms 提交两次，原冻结路径前后世界快照及生命值不变。实际移动卷轴后只切后台、不点保存退出，结束进程并重启，卷轴仍在新格。
后台丢弃画面现在仍递增原呈现次数，记录为 -1；恢复不会错位到旧编号。game-state.jsonl 的 present_sample_ns 只在收到正的真实上屏间隔时更新，同进程恢复后已验证新样本持续前进。
证据：android-background-item-reloaded.png、android-original-background-save-paused.log、android-present-background-roundtrip.json、android-present-background-game.log。

可选 GPU 分段计时使用实际查询，按首个片元着色器与目标大小统计渲染通道、按计算着色器统计 dispatch。开启参数为 --ez gpu_profile true，默认关闭；其区域耗时可能包含依赖及重叠，不能当独占着色器时间相加。此工具用于定位后续性能工作，不能代表已优化帧率。

## 移植范围与产物来源约束

优先级是原底层在 Android 稳定运行及可复现工具链。游戏逻辑只复用原项目已有修改或必要补强，不另写 Android 专属玩法、数值、AI 或任务；后续战斗/换图等阶段是兼容性验收，不是新增游戏改造。所有新改动应有明确上游对应或平台根因。

rewrite-native.py 从原始 NSO0 一次调用原 patch_nso.py 生成，记录原输入哈希/构建号、固定上游提交、脚本和工具版本、有效选项、所有镜像及侧表的哈希。命中缓存仍检查完整产物；损坏镜像、缺失门表、修改观测表及加工后镜像输入的负例已拒绝。deploy-android-modules.py 只接受通过该校验的完整包，手机回读全部哈希后切换模块目录，保留上一套模块，不修改存档/RomFS。

不再在 native 原规则域额外启用 Android 瞄准别名；完整触屏 APK 仍使用上游 touch-run-v4 原瞄准规则。重新生成的 main/sdk/subsdk0 哈希与当前手机原产物相同。规则审计脚本会对照固定上游的剪口、观测门、参数、扫描点和门语义，同时验证当前默认 Apple 产物与上游逐字节一致；平台追加跳板的分支位移单独核对。

上游审计已全部通过：固定基准 afad65c895386a96dfb863f2317e41d61222a5a9；main 保持 48 个剪口、21 个观测门、36 个宿主门，9 个差异仅为指向平台追加跳板的分支重定位；sdk/subsdk0 无新增剪口。默认 Apple 三模块产物逐字节一致。证据为 android-upstream-rule-audit.json；最新构建清单与该次审计的所有模块产物完全相同，关联证明为 android-current-bundle-upstream-link.json。手机完整部署及冷启动证据为 android-provenance-deployment.json、android-provenance-cold-launch.json。
