# Android 原生外壳

APK 将原宿主作为共享库加载，在手机硬件上直接执行离线改写后的 ARM64 游戏模块，
原 NVN 接 Vulkan，通过 SurfaceView 出图。禁止 CPU 模拟；约束见根目录 AGENTS.md。
真实里程碑与剩余验收见 [移植计划](../../docs/ANDROID_NATIVE_PORT_PLAN.zh-CN.md)。

## 构建与私有数据

```sh
source scripts/dev/env.sh
scripts/dev/build-android-native.sh --touch --tlsdesc
python scripts/dev/build-android-apk.py --tlsdesc
adb install --no-incremental -r .local/build/android-apk/d2r-native-tlsdesc-debug.apk
```

无预加载版本使用系统原生 TLSDESC，产物目录 `.local/build/native-touch-tlsdesc-rewrite`，
HOST_ABI=2；APK 不含 wrap.sh。原 initial-exec 调试路径仍保留：不带 --tlsdesc 构建，
使用 `.local/build/native-touch-rewrite` 的 HOST_ABI=1 产物及 d2r-native-debug.apk。
**两种模块与宿主不能混用，装载器会明确拒绝。** 当前发布流程、普通用户导入器尚未完成。

APK 不含完整游戏、密钥或游戏派生着色器。应用私有 `files/runtime/` 需要：

- `modules/`：上述对应规则/ABI 的三模块改写产物与元数据。
- `RomFS/`：用户已有的合并解包游戏。
- `shaders/`：完整校验过的离线 Vulkan 缓存，目前 6931 个模块。
- `romfs_overlay/`：build-original-assets.sh --overlay 生成的原移动端覆盖层。
- `save/`、`temp/`：原宿主存档与临时目录。

先 adb push 到 `/data/local/tmp/d2r-native/`，再以 run-as 在手机本地复制，
不得经 adb exec-out 的 stdin 写模块文件（该通道不可靠）。替换模块前保存退出并停止应用；
复制后核对 SHA-256。游戏数据、签名密钥、APK、派生资源及存档不提交。
固定 zlib-ng 只加载到原资源生成子进程，保持原 PNG/表/元数据冻结检查不变。

## 输入与界面

Canvas/JNI 接原 dm_touch_controls.c；Android 平台桥按原 front_ios.m 转接光标、
物品查询/拖放/双击、技能绑定、地图、腰带和人数档请求，游戏调用仍由原宿主泵执行。
原 v4 HUD 的图片、布局 JSON 与预渲染字形直接打包，血蓝/经验/腰带/人数来自原 dm_hud_get。
面板默认直接触屏，调试肩键/方向键用 --ez touch_debug true 临时显示；实体手柄与 ADB 按键通道始终保留。

慢帧时，已松开的界面手指等待原命中/落点查询答案，不沿用原前端 250 ms 后按未知结果执行的兜底。
点击/长按仍按真实松手时刻判断；五秒无回答或被新触点取代时取消，不向下发出未判类的点击、丢弃或出售。
Android 的自动菜单方向脉冲只跨一个出图帧，避免慢帧下原三帧脉冲触发游戏的按住连发、跳过目标行；
其余自动键保持原时长。

语言沿用上游自定义页（`files/runtime/sw_custom.txt` 的 `lang_en`）：
1 在下次启动时选择英文文字和语音，0 选择简体中文。Android 壳与原 iOS/Mac 壳一致，
在挂载存档前初始化原自定义设置目录，再由原设置加载流程应用语言。
物品栏、商店面板及其中的文字由游戏经 Vulkan 绘制；Android Canvas 绘制原触控层和 v4 HUD 素材。

原自定义页的省电设置现已接入 Vulkan 目标与原 NVN 取图节拍器，按实际屏幕刷新率选 30/60 档。
同一进程应用开/关已实测。只读 frame_pacing 发布目标及有效取图周期，不代表 GPU 已达到该帧率；
屏幕刷新率提示仍待对应。


原可选运行时外推已按 nvn_rtex.inc 与 rt_* 状态机对应：完整世界绘制与 GPU 历史重投影交替，
保留原蒙皮单位/阴影/界面通道规则。缺少片元模板导出扩展的设备通过 R8_UINT 洞标记纹理加
第二次仅写模板的绘制完成。默认关闭。只读 runtime_extrap 分别记录候选帧与实际编码的重投影。

同步校验下，lavapipe 的两种模板路径与 Mali 的两趟路径均通过 279259 项 GPU 夹具检查。
测试用独立标量参考核对颜色/深度/模板，并覆盖相机跳变、原通道角色及强制完整帧。
通道角色夹具检查模拟后处理前的不透明结果；检查数量不能代表真实游戏最终合成质量或性能。
已覆盖原生 D24S8 与 D32S8。Metal 将 NVN D24S8 升成 D32S8；Vulkan 保留原精度，
两者均按原深度模板角色识别。
实机已识别 D24S8 主通道，营地与区域 2 均实际编码重投影；移动、1→2→1 换区、
物品栏开关及同进程 60→30→60 交接通过。更多最终合成场景与受控性能对照仍待验收。
同 PID 切 HOME、原后台存档与 Surface 重建恢复后继续外推，生命值不变。

真实手机已验：原移动端选角进入营地、摇杆、世界点按/寻路、储藏箱、物品选中/说明/拖放后重载、
卷轴出售/买回、空技能位绑定后重载、回城 2→1→2、血蓝/经验/腰带数量、人数档 1→8→1、
小地图→全屏图→小地图。野外有真实瞄准击杀和经验 21→54，起始药水丢出拾回；自然掉落等覆盖仍待完成。
原 35 个增强模块已直接链接，链接不等于所有可选功能验收。

实体输入走 dm_pad_set，触屏走独立 dm_input_set。Enter/Space 确认、Escape 返回、方向键导航。
Android 编辑框接原 hle_swkbd.c，保留 UTF-16 长度和 UTF-8 转换，标题/提示来自游戏。
控制器重连、多设备、所有物品与技能手势仍需补覆盖。

## 调试与验证

启动：`adb shell am start -n com.d2rmobile.nativeport/.MainActivity`。

- --ez run_game false：只验证外壳/TLS。
- --ez validate true：要求包内 Khronos 同步校验层。
- --ez run_vulkan_tests true：运行原 NVN/Vulkan 夹具后退出；需私有 vulkan-fixture/。
- --ei trace_frame N：私有 GPU 逐绘制抓帧。
- --ei watch_type TYPE --ei watch_class CLASS：原单位监视器；CLASS=-1 取消，singleTop 可更新。
- --ez touch_debug true：显示开发用附加按键。
- --ez overlay_continuous true：同进程对照原持续 HUD 重画；默认只在绘制内容变化时重画，输入仍按显示节拍轮询。
- --ez pipeline_byte_hash true：同进程对照原逐字节管线缓存哈希；默认按 64 位计算，仍完整比较键。
- --ez pipeline_prepare_reference true：每笔重建管线键/查表用于对照；默认直接复用相同静态状态。validate 启动还会重建完整键核对复用证明。三个开关均支持 singleTop 更新，见 scripts/dev/measure-android-render-toggle.py。
- --ez gpu_profile true：可选实际 GPU 分段计时，默认关闭。
- --ez external_profile true：厂商工具采集模式，关闭自定义 CPU 计时/统计和整提交/分段 GPU 时间查询；保留 NVN 游戏查询、完成栅栏及输入/状态日志。与 validate、gpu_profile、trace_frame 互斥。启动脚本对应 --external-profile。
- --es game_res WIDTHxHEIGHT：可选冷启动输出尺寸，16..4096；默认按 Surface 原生像素输出。
- --ef scale_3d 0.5..1、--ez fixed_3d true：转接原 3D 比例及动态比例开关；启动脚本对应 --scale-3d 与 --fixed-3d。
- --ez async_submit true：调试用有界在途提交，启动脚本对应 --async-submit。每份提交独立保活，最多 8 份；池镜像上传按页位图等待相交的在途 GPU 访问。默认仍为串行对照；出图也用 acquire/copy/present GPU 信号量衔接，仅在复用实际未完成的资源时保留等待；打包纹理等 CPU 消费路径仍待完整审查，不能视为整套异步兼容已完成。
- --ef bright_k 1..4：仅调试时转接原 DM_BRIGHT_K；默认使用上游动态曝光。
- --ez panel_freeze true：启用原 DM_PANEL_FREEZE；默认与上游一样关闭。
- --ez runtime_extrap true：启用原 DM_RT_AB=1；默认关闭。

私有 native.log / tls.json / hid.jsonl / game-state.jsonl 记录真实宿主事件、TLS 模型、
游戏 GetNpadState 返回与原 HUD/玩家/绑定读数，不拿前端发布意图冒充游戏响应。
android-game-key.py 支持按键或 --tap X Y，等待实际 UI 消息和松开采样；
android-game-tap-unit.py 用原单位投影点按；walk-to.py 用原摇杆方向公式闭环移动，不传送或改角色状态。

无预加载 APK 已验普通 System.loadLibrary、三真实模块、营地与触屏移动；进程无 LD_PRELOAD，
不同线程 TLS 偏移实际不同、隔离正常。原夹具覆盖 dlopen 前既有线程、64 并发线程、200000 原子增量、
信号、x18、x0/x1/LR、NZCV 与 SIMD 保持。原 IE 夹具和不兼容 ABI 拒绝也通过。
同一无预加载游戏进程已通过 3 轮 HOME/Surface 重建并恢复新鲜世界/绑定快照。
发行签名、发行配置、16 KB 页真机和更长稳定性仍需完成。

Android 宽屏手机默认沿原 iOS 使用原生像素 UI、3D 比例 0.75 和动态分辨率控制（默认下限 0.60），保留掌机/桌面档选择与显式尺寸覆盖。
`--scale-3d 1 --fixed-3d` 是全原生 3D 诊断选项，不是日常默认值。窄屏平板的完整 UI 投影对齐仍需另行验证。
PHP110 已验游戏/Surface 同为 2772×1240、固定 3D 1.0，英文营地、物品栏、移动和后台原存档通过；
真实游戏完整管线键核对 769423 次，同步校验无报错。最初无注入运行四窗约 9.07 fps 的记录仅为历史基线，不代表当前版本。
旧 1280×720 / 3D 0.60 记录是历史低分辨率基线，不能与此原生输出直接当提速对照。
呈现时间来自驱动扩展；厂商采集模式关闭自定义计时，缺失指标明确不可用。

```sh
python scripts/dev/android-game-launch.py --external-profile --scale-3d 1 --fixed-3d --report .local/logs/native-launch.json
```

`game-state.jsonl` 的 game_output/surface_output 取自实际成功提交的出图尺寸；不是启动意图。
当前按提交全局等待及资源寿命改造见 [提交审查](../../docs/VULKAN_SUBMISSION_LIFETIME.zh-CN.md)。

系统返回键沿原客体帧脉冲执行：面板中 B 返回，世界中 PLUS 打开原菜单；全屏地图先回小地图。Android 33+ 使用系统返回回调，旧设备保留旧回调。真机物品栏打开/关闭及地图返回均有证据。

正常游戏与暂停菜单均沿原后台存档流程提交。移动物品后只切 HOME、结束进程并重启，未点保存退出，位置仍保留。丢弃画面仍保留原呈现编号；present_sample_ns 可区分恢复后来自驱动的新呈现读数。

调试浮层显示约一秒平均的实际上屏 FPS，复用已有驱动呈现反馈；反馈有约八次出图的延迟，没有额外 GPU 查询或独立计时器。它不作为瓶颈归因工具。
温度浮层仅显示 `ACTION_BATTERY_CHANGED` 的电池温度，使用事件广播，没有温度轮询或后台查询线程；不使用 ADB 温度报告、文件桥接、shell 或 sysfs 回退。
`PowerManager` 热等级监听只用于平台保护：LIGHT/MODERATE 采用原 30 FPS 出图档，SEVERE 降为 20 FPS；进入过 SEVERE 后，MODERATE 仍保持 20 FPS，降至 LIGHT 或以下并稳定 30 秒才恢复 30 FPS，回到 NONE 并稳定一分钟后释放限制。SEVERE 持续 45 秒或到达 CRITICAL，沿原存档与后台暂停流程让设备冷却。此举降低持续负载，不算性能优化或已证明解决过热。暂停/分离视图会清理监听。
Android 公开热等级不能保证覆盖厂商所有保护策略，仍需真实连续游玩验证保护是否足够及时；电池温度也不能当作 CPU/GPU 温度或热源归因。

python3 scripts/dev/android-game-launch.py --report .local/logs/launch.json 冷启动已有角色，核对新 PID 的启动标记、实际角色选择/HUD 事件及新鲜玩家/绑定/呈现读数，避免把上个进程日志当新启动证据。android-game-key.py --host-event 也可接受原宿主观测，如地图切换；默认仍要求 UIMSG。

## 可追溯构建与部署

上游基准固定在 scripts/dev/upstream-source.json，不随本地 HEAD 改变。原始 ExeFS 与生成目录分开，禁止以 patched.bin 作为新输入。完整构建入口会调用 rewrite-native.py；缓存命中也核对所有镜像、侧表、源码指纹和共享清单。改坏缓存后可从原始 NSO 自动重建，不在旧镜像上修补。

```sh
source scripts/dev/env.sh
scripts/dev/build-android-native.sh --touch --tlsdesc
python scripts/dev/rewrite-native.py --source "$DM_ROOT/data/switch/3.3.94012/Update/ExeFS" --output .local/build/native-touch-tlsdesc-rewrite --abi elfdesc --rules touch-run-v4 --verify-only
python scripts/dev/audit-native-rules.py --bundle .local/build/native-touch-tlsdesc-rewrite --report .local/logs/upstream-audit.json
# 先完成原后台存档并停止游戏进程，再切换已校验的整套模块。
python scripts/dev/deploy-android-modules.py --bundle .local/build/native-touch-tlsdesc-rewrite --report .local/logs/deployment.json
```

工具分别生成 rewrite-manifest.json、每模块 provenance.json、上游规则审计与手机哈希回读报告。模块部署保留上一套派生产物；不参与存档、RomFS 或密钥处理。默认 Apple 三模块产物已与固定上游逐字节一致；Android 规则审计区分平台分支重定位。干净目录重复生成、损坏镜像/共享元数据自动重建、缺失门表/篡改观测表/加工后输入拒绝已有原创夹具覆盖。

NVN 遮挡查询默认使用对应 Metal 的通道范围。调试 Intent
`--ez query_per_draw_reference true` 可切回旧逐 draw 查询，支持 singleTop 同进程对照。
它只改变查询分组，报告、重置、绘制与分辨率仍保留原语义。
