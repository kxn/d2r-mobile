# 独立 Android shared/private 匿名页 oracle

在530995f host fixture上只增加测试冷启动开关：
`host_import_shared_anonymous` → JNI → 原环境选项。只允许
`run_game=false, run_vulkan_tests=true, host_import_test=true`；一次进程只执行
一次标准测试，不允许 Activity 重建/新 Intent 改映射类型。所有原字节断言
保留。guest heap、Vulkan driver 和原生产包不改。host import的vkMapMemory
可能返回与原host pointer相同的VA；保护对象始终是fixture自己mmap创建的VMA，
不操作另一块由driver分配/映射的内存。

使用 `build-android-apk.py --tlsdesc --opt-level 3` 原入口生成带完整来源清单
的宿主，再由 `build-android-shared-anon-fixture.py` 复用已有独立包打包方式
改 manifest package 为 `com.d2rmobile.sharedanondirtyfixture`，保留原完整
Activity 类名和6个 byte-exact native libraries。仅明确去掉未用的可选VVL库；
直接PanVK测试必须显式validation=false，错误计数0不冒充真机VVL覆盖。

`run-shared-anon-import-fixture.py` 默认仅plan、无ADB；显式执行要求绑定
root当前生产APK/ELF/28模块/chain/full4六文件。原生产包只读核对，独立新包
必须原先不存在；测试provider仅固定bdd9原文件。四轮均冷启动：private普通、
private dirty、shared普通、shared dirty，都是原标准8KiB双向oracle。

原private dirty如果再次字节断言失败，照实记 `native-assertion-failed`，
保留原始日志、清理本次进程后再跑下一格；不把预期失败转换为通过。四格
都通过才exit0；完成但有失败仍exit1。若provider/实际type0xb/映射配置未证明，
停止整个矩阵并保留证据。每轮仅认领所启动的首PID，不认领replacement。

finally只停精确本次PID/卸载精确新包/删自己临时目录，再核原包所有绑定不变。
不启动原游戏，不读温度，不增加计时/采样器；monotonic只编排原45秒功能限时。
此包仍是独立原创功能测试，不证明整个heap、任意VMA、PFN/COW根因或游戏共享。
