# 大 Pool 子窗独立 appUID 功能包装

此树保留原包装 `9baacd2`，只叠加独立核心修订 `7186b6d` 对应的未跟踪多镜像回退修复，不改变其导入/排除/退休策略；只增加显式测试冷选择、原夹具顺序执行、功能 JSON 和官方 aapt2 独立包/事务编排。不是主游戏版本，也不启用生产全进程 pin 策略。

- 安装包 `com.d2rmobile.borrowedsubwindowsfixture`，完整 Activity/JNI 类仍 `com.d2rmobile.nativeport.MainActivity`。原六个 ELF 与既有 APK builder 字节一致，明确省去 direct ICD 无法组成的可选 Android VVL 层。
- Intent 必须同时 `run_vulkan_tests=true`、`borrowed_nvn_windows_test=true`、`async_submit=true`、`vulkan_driver=panvk`、`run_game=false`、`vulkan_test_validation=false`。新 JNI 无地址参数，共享旧选择器的单进程互斥凭证，不能同时选旧 whole fixture。
- 同一独立进程先运行原 general（8,273项，含选择检查），再运行原 full-reference（增量1,560项），总计9,833项。保留原像素、dirty、writer/image/CPUeffect、closing/ABA、tail/跨节点断言；不降低计数或放宽字节比较。
- 非 coherent 的原真实 Flush 成功后追加 node begin/end、逻辑读取范围、memory-local offset/size、atom/flags 功能行；不加采样、计时或新同步。这部分在宿主 coherent0xf 无实际调用，不能替代后续 Mali0xb 验证。
- 控制器默认 plan 不访问设备。执行必须绑定租约当时完整原 APK SHA、原 ELF SHA、28模块部署报告和 shaderchain；不内置旧 e4248/ff2363 任一值。原 ELF 从现场 APK 在宿主只读解压核验，生产包和数据没有写路径。
- 一次45秒功能运行、安装70秒上限；只新独立包/唯一 tmp。预检原 game PID 空、原包身份全匹配、独立包不存在；不换系统驱动、不改全局层/系统服务、不 root/thermal/sysfs。只加载固定源构建 PanVK `bdd9df32…` 和原17个 GLSL 夹具文件。
- 结束仅停止/卸载本次精确 APK 的独立进程/包，删本次 tmp 并核实不再存在，终止本次 host installer/am-start 子进程，重读原包/ELF/28模块/chain。ADB或采集错误原样保留，不能把清理失败说成已恢复；不重启设备或重装原游戏。

正式接受要求普通 appUID、实际 provider PanVK/driverID20、flags0xb、atom64、两个 phase 全部结束，Flush本地 offset/extent 不越导入节点且包含非零 offset 与4KiB尾窗。现有宿主9833/VVL/sync通过只作为离线功能验收；直到 root 单独授权 lease 并完成事务，状态始终是未安装、未实测。

本次小修使普通未跟踪池的上传 visitor 可跨相邻镜像，原 GPU 复制和逐字节断言保持。独立 host 负例另有16,418项；正常 Android 合并夹具仍为9,833项，不把没有导入的普通回退计作 PanVK 非一致性导入成功。`9baacd2` 及原失败产物保留，不覆盖旧候选。
