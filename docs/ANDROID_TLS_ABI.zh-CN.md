# Android 原生 TLS ABI

ELF TLS 的分配由 Android 动态链接器管理。initial-exec 要求库在进程启动时已加载；
动态 TLSDESC 可用于普通 dlopen / System.loadLibrary。依据为
[Bionic ELF TLS 设计](https://android.googlesource.com/platform/bionic/+/HEAD/docs/elf-tls.md)
与 [Arm ELF64 TLS 描述符重定位规范](https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst)。

原离线改写器/装载器现有三种显式 ELF 模型，Darwin 产物不变。

| 改写参数 | HOST_ABI | 键页内容 | 宿主构建 |
|---|---|---|---|
| DM_HOST_ABI=elf | 1 | 固定的有符号 TP 相对槽偏移 | 原 initial-exec 调试预加载 |
| DM_HOST_ABI=elfdesc | 2 | 系统链接器创建的 TLS 描述符指针 | DM_TLS_DESC，普通动态加载 |
| DM_HOST_ABI=elfdesc-gate | 3 | 同 ABI 2 的八个 TLS 槽，另加只读进程门地址 | DM_TLS_DESC，普通动态加载 |

ABI 2 在 platform_elf.c 声明编译器管理的动态 TLS，以 AArch64 TLSDESC 重定位取得其 GOT
描述符。离线槽地址跳板调用系统解析器，加当前硬件线程指针与逻辑槽偏移，然后继续执行原指令。
跳板保留 x0/x1/LR 和 NZCV，其余寄存器按 TLSDESC 调用约定保持。
没有 CPU 解释器、运行时指令翻译，也不查私有 Bionic TSD/DTV 布局。
客体线程初始化先触及 TLS 块，再进入游戏；键页写完后仍设为只读。

ABI 3 的单指令 TLS 访问模板与 ABI 2 逐字节相同。进程内恒定的 `dm_gate`
函数地址直接存放在独立键页槽 8（`HOST_GATE_IDX=8`），SVC/计数器/观测门不再
为查找这个函数地址调用动态 TLS 解析器，普通门模板从 25 条指令缩短为 6 条。
八个线程槽、线程初始化、原跳表与门内保存/恢复规则不变。必须用新的
`--abi elfdesc-gate` 从原始 NSO 重新生成完整包；旧宿主会拒绝 ABI 3，不能手工
改 ABI 标记或修改已加工的 binary。ABI 2 仍可作为对照产物使用。

2026-10-19：新 ABI 已通过实际 ARM64 普通 dlopen、既有线程与 64 并发线程、
浮点/整数状态、256 跳表分支及原并发夹具。实际原游戏 build 94012 从原始
main/sdk/subsdk0 重新生成并验证完整来源清单后，进入选角、营地与野外，完成
原生命周期存档两次提交后停机。实际整帧收益尚未完成受控比较；不能将门模板
的指令比例写成游戏提速比例。私有证据为 `.local/logs/process-gate-v1-*`，
模块包为 `.local/build/native-touch-elfdesc-process-gate-v1`。

ABI 3 还支持离线合并连续的普通 x18 指令：`rewrite-native.py --abi elfdesc-gate
--x18-group-size 3`（ABI 3 默认值）。2～3 条指令共用一次真实 TLS 地址解析，
每条指令执行后仍立即写回逻辑 x18；每个原指令地址均保留独立入口，从组中间
跳入也会先完成自己的 TLS 解析。涉及 SP、控制流、系统或原子指令时不合并。
后续规则覆盖任一成员时会重新校验并回退，不能绕过原有规则。
`--x18-group-size 1` 保留逐条解析作对照；选项计入来源清单及缓存指纹，必须
从原始 NSO 重建。ABI 2 不启用合并。

该合并已通过实际 ARM64 的中间入口、寄存器/NZCV、信号、并发独占递增和
64 线程夹具，并在同一 APK 上完成真实游戏营地两组对照。两组均受温控限制为
30 帧，尚无受控整帧收益结论；局部解析与指令工作量的减少不能当作整机提速。
证据在 `.local/logs/x18-groups-v2-*`，对照和候选模块分别位于
`.local/build/native-touch-x18-groups-v2-reference` 与
`.local/build/native-touch-x18-groups-v2-candidate`。

装载器拒绝不兼容模型混用，改写目录及缓存指纹分开。三种模型均通过原生夹具，覆盖 x18、
原 SP 地址/写回、x0/x1/LR 目的寄存器、NZCV/SIMD 保持、强制 HLE、信号与 200000 次并发独占递增。
动态库还覆盖了经同步确认在 dlopen 前已运行的线程，以及 64 个并发线程。

PHP110 / API 36 / 实际 4 KB 页上，无 wrap APK 已通过普通 System.loadLibrary。
实测不同线程的 TP 偏移不同，线程隔离正确，真实进程环境没有 LD_PRELOAD。
三份真实游戏模块进入选角与营地，触屏移动通过；3 轮 HOME/Surface 重建保持 PID，恢复新鲜世界/绑定快照。
同一 APK 内 35429 项原 NVN/Vulkan 检查通过，启用包内 Khronos 同步校验。
这些证据确立原生加载，不等于完整游戏、发行签名/交付或 16 KB 页机型验收。

构建和匹配部署路径见 [Android 外壳说明](../dm/android/README.zh-CN.md)。
私有证据在 .local/logs/：android-native-tlsdesc-test.log、android-native-ie-regression.log、
android-native-wrong-tls-abi.log、android-tlsdesc-game-probe.json、
android-tlsdesc-background-roundtrip.json、android-vulkan-tlsdesc.log。
