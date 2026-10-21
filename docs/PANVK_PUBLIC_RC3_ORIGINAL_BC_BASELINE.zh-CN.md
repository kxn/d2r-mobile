# 公开 RC3 原版驱动对照（仅工具，待真机）

目标是在 PHP110 / Mali-G710 MC10 上确认发布者原样 RC3 能否完成真实原游戏绘制，先建立没有本地驱动修改的基线。不是共享注册、性能或持续稳定性验收。

发布版本为 `zenithblue-oss/panvk-kbase-android` 的 `panvk-kbase-v0.1.0-rc3`：关联源码 `fd6ed91eff1c8d3a83307a2348b959bb93797011`，公开 Android SO SHA256 `b46b9f0416f207a05482349dffb37b6f3d84c20762151c623774831ce3939445`，BuildID `4ec75e25deca57c27fb1e0872fdbf58473d41527`。使用下载的原字节，不编译、不 patch、不 strip、不改 kernel 或设备权限。关联源码归档不是本地可重现构建证明。

ELF 无 libhardware 依赖；七个依赖与此前 PHP110 实际可加载的 df9 完全同集。HUD55应用的 app-private dlopen + 标准 ICD 导出入口可直接使用。实际 namespace/AHB/kernel 兼容仍以本轮日志为准。ELF Android note 为 API35，工具拒绝低于35的系统。发布者 README 记录 Pixel 7/7a G710 MC7、MTK G610 实测，不能替代 PHP110 MTK G710 MC10/uAPI1.18 结果。

保持当前HUD55 APK/ELF及root选定35资源模块及 cbd8 shaders。只暂时发布已证明来源的原 BC 四叶 overlay，UI 与生产 ASTC4 相同。公开 PanVK 支持 BC 时，旧 cooked ASTC4 可能触发157→137未实现转换，不能拿该资源混用失败来判断驱动。sharedOFF、legacyASTCcopyOFF、固定.60/2772×1240，驱动按当前 requirements 正常分配。不开 TRACE/DUMP/kbase_diag/SYNC/序列化，不设 debug properties；现有外部 debug flags/layers 直接拒绝。公开默认 BC shadow/AFRC 行为保留，若出现画质差异如实记录。

唯一 actual launch；原启动器75秒、外层100秒、自然退出最多30秒、原 lifecycle save helper45秒。超时/未进HUD是未完成，不能自动等同GPU DeviceLost。单 ordinary UID/首 PID，PID歧义保留现场不强停。真实进世界必须原 fresh save2 后 stop；若HUD55仍菜单，则只接受既有严格同PID新增drain完成且没有任何save日志、Activity STOPPED的原cold-pause证明。已退出绝不为存档重启。此分支复用已审v6的精确 helpers，不新改游戏状态。

复用原v5四目录 driver/temp/cache/overlay owner+hash CAS；当前 cache/PSO/shaders完整保留，失败同样恢复。成功仍需原HUD状态、实际RC3 driverInfo/maps与截图；截图由root目视确认，不以阶段字符串当视觉通过。不会运行旧capture、诊断hang工具、官方probe以外的新测试程序或自制计时器。

默认仅输出离线计划。sourceReady中的执行命令是 DRAFT，待root审核并串行分配Mali后才可执行。不得访问设备；由root验收HUD55并提供当前基线后串行执行。

## HUD55 后继（尚缺实际基线）

旧081d66e及其DRAFT原样保留。当前后继测试和恢复都使用55a1112宿主，不切换旧022 APK或NSO；只driver/temp/cache/overlay四目录按原CAS临时发布。源码、启动器、APK/ELF已pin55；原BC composition41ce与公开RC3 b46原字节保持。原35只是待root实际快照确认的清单长度，不能用旧022 map补齐。

`--baseline-ready`及`--baseline-ready-sha256`绑定root当前快照，字段为`source_revision/APK/ELF/selected_module_resource_hashes/shader_chain/root_actual_receipt/execution_ready/root_actual_complete`。最终root实际receipt SHA尚未可用，`BASELINE_ROOT_SHA=None`使execute无条件拒绝，不能仅自填布尔值开执行。缺参数仅输出明确DRAFT。serial不再默认旧端口。

55源码保留同一if(dm_ingame_now())条件save分支及无条件结果LOG先于图形suspend，diag的unbuffered stderr源码与022相同；实际菜单退出仍必须本次同PID新增GPU-drain完成、没有save结果LOG且Activity STOPPED。旧022实际冷暂停记录只是历史依据，不是55真机结果。
