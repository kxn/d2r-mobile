# ASTC page-array 硬件 oracle WIP（暂停）

按共享内存优先级暂停。独立 c079fcb 后继，未改 renderer/driver，未构建 Android
canonical/薄 APK，未访问设备，未实际执行 ASTC GPU。当前最终源码未重新编译或审核；
早一版 host 编译日志及 unsupported 退出仅作开发记录，不作为当前源通过证据。

已写原 native_vulkan 夹具 body：ASTC4/6×UNORM/sRGB，16²×3layers×3mips；原NVN
Builder/packaged swizzle/上传/FS draw/输出RGBA ReadTexels，源实际 staging LDR证书，
全 mip/layer copy，原 clear 初始化的同格式 baseline reference、旧 FS 读后 late BC
B2T恢复RGBA、保留全部层/mip、GPU写源撤销证书后新fresh目标保持RGBA。case 使用
不同4MiB地址，避免已Finalize但仍在翻译宽限期的真实别名阻断新fresh证明。

Java 仅新的有限测试冷分支复用现 nativeTests，run_game=false/system/private/async/
validation，拒其它fixture；JNI只设置独立测试环境开关和选择器。普通游戏路径不变。
未来必须先审源码与补齐必要离线入口检查，再授权 canonical 和真实 hardware 验证。
ON/OFF 应分别冷启动；unsupported/无实际admit不能算通过，不能把 BC3 stand-in 当ASTC。

未来 argv（仅模板，不可部署/执行）：
`am start -n <independent-package>/<MainActivity> --ez run_game false --ez run_vulkan_tests true --es vulkan_provider system --ez async_submit true --ez vulkan_test_validation true --ez compressed_page_array_test true --ez compressed_page_arrays <false|true>`
不含游戏模块/资源、不复用原游戏PID；具体APK/6SO/fixture cache/系统VVL身份及严格cleanup
仍需独立冻结。当前没有执行控制器，不应拿模板直接操作设备。
