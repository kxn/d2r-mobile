# Legacy ASTC 系统 Vulkan 验证包装

基线为 `8205364fca3c1196d5be506a3b6ea5aeabe59d18`，渲染器、NVN、dirty 和 GPU 字节 fixture 均不变。本补充仅修改 Java/JNI 测试包装、原启动脚本，并新增独立控制器/离线合同检查。

`nativeTests` 末尾显式接收 validation 布尔，Intent `vulkan_test_validation` 缺省仍为 true。false 同时取消 validation 与 synchronization-validation 环境请求，日志明确 `disabled_explicitly`，不能称验证层通过。日志复用既有 `dm_fixture_output_open`：只 truncate 一次，两个 FILE 均 append，避免独立 offset 覆盖 JSON。配置和返回 terminal 各记录同一进程 UID/PID；终态必须等原完整字节 fixture 返回，不因中途 final 提前杀进程。

原 `android-game-launch.py` 新增 startup-only BooleanOptional `--legacy-astc-copy` / `--no-legacy-astc-copy`，只把请求转为同名 Intent 并在报告记录请求；attach 禁止设置。真实 adoption 依赖 native `[ASTC resource copy]` 日志，不根据请求猜测命中。

离线打包复用原 `build-android-shared-image-gate-fixture.py`，新包 `com.d2rmobile.fixedsharedfixture`，Activity/JNI Java 名仍为 `com.d2rmobile.nativeport.MainActivity`。manifest 包名/资源和签名为独立身份；原 canonical 六个 native 库与其余有效载荷逐字节一致，不含游戏文件。只安装独立包，运行 system Vulkan，无私有 ICD 上传/系统驱动替换。

`run-system-legacy-astc-fixture.py` 默认 plan-only；执行前必须明确当前原游戏 APK/ELF、28 模块、6 资源和 shader chain，前后只读核验原包不变及进程不存在。只允许一次安装、一次冷启动，无重试。新包 fresh `vulkan-test.log`；首个单 PID 用 `/proc/<pid>/status` 与 run-as UID 交叉确认。多 PID、替换 PID、未观察 owner、无 terminal、旧结果、缺项或 unsupported 均不能通过；不接管未知进程。退出/中断只清理精确持有的独立包进程、包与唯一 tmp，不停止系统服务或原游戏。

实际命令固定 `run_game=false run_vulkan_tests=true legacy_astc_copy=true legacy_astc_copy_test=true shared_guest_heap=false async_submit=true vulkan_provider=system validate=false vulkan_test_validation=false`。必须同时看到：

- final `legacy-ASTC-copy-encoding` passed / actual_ASTC_GPU=true / CPU_phase_result=passed / game_modules_executed=false。
- 唯一 `ASTC4-copy-physical-encoding` 的七项真实 GPU 语义布尔均 true。
- 唯一 `legacy-ASTC4-copy-storage` 初始 decoded RGBA、原 whole tracker、无 import、完整 host 页目的均 true。
- CPU-escape、配对 pread CPU-phase 与原 29 项 admission 行均通过；配置/terminal UID/PID 等于本次 owner。
- system loader、无 FATAL/VUID/native fault；关闭 VVL 是显式验收限制，不把没有 VUID 当作 VVL 成功。

本阶段没有访问设备、执行原游戏或宣称性能/带宽/发热收益。root 审核冻结产物并分配短租约后才可执行；普通 UID 压缩 GPU oracle 也不能代替真实游戏数组 adoption/稳定性验收。
