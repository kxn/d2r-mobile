# 进程内调试面板

点击现有左上 FPS／电池徽标展开，收起按钮、面板外点击或系统返回先收起。面板是原 Activity 的子 View，不新建 Dialog/Window，不发送 HOME、不改变正常存档／暂停生命周期。收起不重置设置；设置仅在本次应用进程内保留，恢复应用默认会打开两项应用热策略并选择“跟随原游戏”。不控制系统温控或系统强制退出。

显示原已有的真实出图 FPS、电池广播读数和公开 Thermal 状态、原渲染策略请求／有效目标、手动及热上限、已成功提交的游戏输出与 Surface 尺寸、当前实际 3D 比例。原策略请求沿用原游戏省电 30／普通 60 和启动时已锁存的 DM_FPS 诊断覆盖，绝不将屏幕刷新率冒充游戏帧率。详情只在面板可见时通过新 JNI 读取现有 CPU 侧原子状态；保持原 nativeFrame 164 项布局，未占用空槽。没有 GPU 查询、计时器、Profiler、系统文件或 ADB 温度轮询。复用原 Choreographer；详情先比较现有值，变化时才格式化和更新 TextView。

手动跟随／20／30／40／60 仅新增独立应用 cap atomic，与现有热上限取更低值，不修改游戏设置、不跳帧、不提高原游戏 30/60 目标。显式应用 cap 是出图节拍的上限：在 60/90/120 Hz 等不可整除刷新率上也不使用原向上取刷新整分档来突破 cap；没有应用 cap 时保留原策略。40 FPS 在不可整除显示刷新率上仍可能有显示节奏不均，不承诺变成恒定实测帧率。

“应用热限帧／热降分辨率”关闭时沿既有 JNI 同时清除渲染热 cap 与 `dm_android_3d_thermal_limit`，不混入手动 cap。两项热策略独立：关闭自动热暂停会撤销原 severe deadline，已出队旧 callback 也不能暂停；重新开启按当前公开热状态重新计时，CRITICAL 则执行原立即存档暂停。固定 3D 比例仍保留原固定语义。

展开先给原触点发送 CANCEL 并清除原桥接状态，而不是 UP，不释放技能或世界点击；摇杆、多个触点均取消。面板覆盖层消费所有面板/空白事件；收起后残留 MOVE/UP/CANCEL 被吞掉，直到下一次新 DOWN。Back 先收起，键盘/手柄输入在面板开启时不进入 GameInput，系统音量键和子控件键盘导航仍由原 Activity 分发。原 UI、素材、手势和游戏逻辑未改。

源码验收包括全 Java API 编译、原 ThermalPolicy 12 项加 3 项开关/deadline 回归、触点序列门的多点/Back/CANCEL 回归、原 C 手势 47 案 1599 检查、原生产 pace 在无/30/60 启动覆盖下的 cap 与 60/90/120 Hz 对照，以及编辑 JNI 的实际 NDK 源码编译检查。未构建 canonical APK、未执行真实游戏或 Android View 输入自动化；真机布局、触摸与系统暂停体验待 root 审核后验证。

紧凑布局：界面仅保留四行常用状态（Surface 与游戏输出不同才追加一行）、两项热开关和同行帧率下拉。默认／收起并排，顶部 8 DIP，宽度按实际可用区域限制在 360 DIP 内，详情与控件均固定 12 DIP 字号以免系统 fontScale 放大挤出屏幕；详细语义保留在本文。ScrollView 仅作极小窗口的回退，正常 326 DIP 高横屏目标无需滚动。

3D 比例下拉：自动（默认，恢复启动配置／原自适应策略）或固定 50/60/65/70/75/85/100%，75% 标注 iOS 默认。手动档由 JNI 仅发布合法整数请求，非法值忽略；原游戏通知 owner 在既有 renderer.scale／0x8582f0 路径生效，不增加地址补丁、不在 UI 线程写游戏。固定档绕过动态与热缩放，但热帧率上限、自动过热存档暂停仍独立生效；原 rulertruth 诊断强制1.0优先级保留。原 present owner 在固定档期间不积累自适应窗口，进出固定档按既有 hold 清掉旧窗口／升降档惩罚，短暂切换由原 resumed 信号补清窗；UI 不修改静态 controller。详情继续显示实际 g_scale3d_cur，不冒充请求已生效。恢复默认同时回自动，设置仅本次应用进程。新增一行 36 DIP 并移除小 footer，保持紧凑，无文本输入。

## 2026-10-28：GPU 对照的显式 60 FPS 上限

未来 GPU 实验可显式使用 `--experimental-present-cap-fps 60 --display-refresh-hz 60`。前者在 Activity 原 HUD 初始化位置调用既有 `nativeManualLimit(60)`，只更新本进程的 `DebugPanel.Settings` 和原手动 cap；无参数时保持原路径，不写用户 preferences、启动 profile 或系统显示设置，不新增 JNI、NVN pacer 或计时器。该参数仅限冷启动；`onNewIntent` 不调用 cap API，并从当前 Intent 移除该 extra，避免忽略的请求在 Activity 再创建时残留生效。已经生效的进程 cap 不因此被重置，仍由原 HUD 管理。原游戏或热策略降到 30 时不会被抬到 60，实验准入须拒绝实际低于 60 的窗口。

取图周期与面板 Hz 分开核准：原 NVN `present_period` 进入纹理 `avail` 和 `WindowAcquireTexture` 的绝对时刻等待；`target_fps=60` 单独不够，须核实际 `pace_hz=60`、`period_ns≈16666666` 及真实输出尺寸。`--display-refresh-hz` 仅复用同像素显示模式和 `Surface.setFrameRate` 的应用内请求，不能把请求值、native 初始化时缓存的 refresh 或 `VSYNC-app` 回调周期当作即时物理面板模式。旧三臂出现 native 缓存 90/120/120、NVN 周期 90/60/60，而官方 trace 的 `VSYNC-app` 更新均约 11.074 ms，不能靠其中任一字段补全另一个事实。周期准入只以实际 `pace_hz/period_ns` 为准，不额外要求启动时缓存的 refresh 必须为60；该缓存可以为90或120。跨实验比较实际交换链尺寸、格式及图像数，缓存 Hz 单独留档。

独立 `.local/logs/gpu-period60-admission-v1/run-with-period60.py` 复用既有 runner 的启动、官方采集和存档停止；追加原 cap/显示请求，采集前核实际周期和 native Surface，结束后仅拉取一次已有追加状态日志，用官方时钟转换覆盖整段 Surface 窗口，拒绝已记录的中途周期变化、Surface 重建和热上限变化。原状态约 1 Hz，报告必须保留最大发布间隔，不能冒充逐帧连续观测或排除未记录的短瞬变；请求未达到共同准入时失败留档，不以反复重启试运气替代验证。

该补丁已通过 Android 构建，产物为 `.local/logs/gpu-period60-admission-v1/built-apk/canonical.apk`，SHA-256 `195281b1c48ec6ff8d934c3ce4377ba1be35e85872d80e1199e575a8d6d715ef`；其构建记录 `canonical.apk.build.json` SHA-256 为 `dfae01a892083a0fd0a04b46357d8e3984d29504aa946df4245e709bb06fcfe2`，包含 1056 个输入，MainActivity 指纹与本补丁一致。已有 41 项启动器测试和 9 项独立周期准入测试通过，共 50 项，覆盖默认不改、显式冷入口、缓存90/120但实际周期60允许、实际90/30/错误周期拒绝、首尾相同但中途变化、缺窗口末端及 Surface 重建。这里记录构建与主机资格；本入口尚未作为真机 GPU 性能或实际面板 60 Hz 的验证结果。
