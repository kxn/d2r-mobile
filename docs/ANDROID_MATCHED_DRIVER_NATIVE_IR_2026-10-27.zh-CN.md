# Q735：同PSO、同SPIR-V的系统驱动与Turnip原生表示核查

本轮取得了可核查的编译输入对照：两端各12个PSO，**12/12完整canonical key逐字相同，实际各stage的SPIR-V也逐字节相同**。系统驱动成功返回统计，但不返回内部表示；Turnip返回了真实NIR和IR3。不能把这写成已经拿到了系统驱动汇编，或已经定位某一条指令造成性能差异。

## 比较对象与身份

使用原上游 `b811b90172f4eb40ec1bc89d903308e0c5ab88d0` 的原执行路线，沿已保存的driver-control loader做独立冷诊断。真实ARM64游戏模块、资源和shader加工规则未修改。当前两臂运行同一诊断APK，切换系统Adreno驱动与B3 Turnip；这不是我们自己后端和上游后端的混合比较。

- APK：`1f3e9fc425ab75f5ecac9c9d92bf3cb4a49113b93f8b4897f3948963ac0c7d46`。
- 宿主库：`850a3c116a5be038f32e0ce6b87e3cdf3c436f1474ea7d7ec445dc8afbd041ce`。
- B3：`b3fce5ab29c6617bd2cf334b3a56c67951de271bd870878c9a3121ff606b1b9f`。
- 设备实际报告Adreno735；各臂保留session中的API/driver版本、pipelineCacheUUID及实际进程maps。

匹配不依赖进程内pipeline指针或导出序号。两端pipeline创建顺序本来就不同，按序号相减会配错。使用上游 `xbg_pipeline_key.h` 的无指针、无padding、覆盖layout/renderpass/static PSO状态的完整key；12项均 `canonical_pso_key_valid=true`。每对另外检查实际stage数组的stage、flags、entry、原key、专门化及SPIR-V SHA，而非仅凭原NVN shader key。

两条目标FS的实际post-rewrite输入：

|原key|SHA256|
|---|---|
|`5c1ca4e7c3e4657c`|`4fe6bf202cd8102b80b23d83ae9981dfe436b69ed37aed2c984fb708c9333952`|
|`38f344adf801b153`|`6011038ce80c4bd1f6c8f02aaa3fc81ae3b6924822f7a59888a94104511ed5f9`|

涉及的三条VS也全部匹配：`af64b417689666bf`、`54f730a6c0efc118`、`06ece8336c8c242c`。此前我们自己后端 `e102...` / source-BLAKE3 `e8d73...` 的较小模块不能套给这批上游输入。上述资格证明编译输入和静态状态相同，不代替动态场景、uniform内容、执行频度或缓存热度的证据。

## 标准工具的实际限制与最小补充

上游原 `gfx-pipe-stats` 已启用 `VK_KHR_pipeline_executable_properties`，但只请求CAPTURE_STATISTICS、只调用Properties/Statistics，没有请求内部表示。已有官方Snapdragon Snapshot数据库的486条和243条ShaderData，`shaderDisasm`均为空；SCOPE计数器是否可用与标准内部表示是否可读是两件事。

现成RenderDoc确实支持 [KHR pipeline executable反汇编目标](https://github.com/baldurk/renderdoc/blob/v1.x/renderdoc/driver/vulkan/vk_replay.cpp)，因此先实证其准入，而没有先自行实现dump工具。实际安装同版官方APK、使用标准StartRemoteServer：最初存储权限处理后握手仍报Network I/O；进一步CLI/GUI连接挂起，host超时，USB shell也随之无响应。过程**没有进入游戏注入或capture**，不把故障归因到游戏渲染或某GPU指令。退出工具并定向恢复无效，重启手机端USB调试后连接恢复。工具进程、存储appop、三个tool属性及五项全局settings均已恢复。

原始失败与恢复收条保留在 `.local/logs/q735-system-native-ir-v1/`，包括 `restored.json`。这是规约12要求的实际工具缺口。后续只沿上游既有诊断补充 [标准内部表示API](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPipelineExecutableInternalRepresentationsKHR.html)，不重试该服务器、不增加计时器、采样器或重放器，也不改变上游已有timer调用。

## 实际返回结果

两端PSO创建均核实flags为192，即CAPTURE_STATISTICS与CAPTURE_INTERNAL_REPRESENTATIONS；feature实际启用，标准stats非空。

- **系统驱动：24个executable全部返回VK_SUCCESS，但内部表示数都是0。** 这是实测后的驱动可见性限制，不是遗漏capture flag或未调用接口。没有System assembly。
- **B3：返回初始/最终NIR与IR3 Assembly。** 包括12个FS、12个VS和12个Binning VS的编译记录；编译出了Binning VS不等于SYSMEM运行时执行了分箱。
- 相同FS在不同VS/descriptor布局下有4种native文本版本，故仍按完整PSO匹配，不拿同原key当作同编译变体。

主要静态统计如下；不同驱动的统计口径不同，原name/description/value均保留，不能直接按指令差折算执行时间。

|字段|5c：System / B3|38：System / B3|
|---|---|---|
|Subgroup|64 / 64|64 / 64|
|静态指令数|3366 / 2562|3504–3506 / 2704|
|Full register footprint|19 / 19|22 / 19|
|Half register footprint|4 / 0|24 / 0|
|System scratch / B3 STP,LDP|0 / 0,0|0 / 0,0|
|B3 max waves|10|10|
|B3 preamble指令数|169|226|

这不支持“Turnip有更多寄存器溢出”的解释。也不能从System用了少量half就直接强制降精度：实际SPIR-V没有RelaxedPrecision、没有Float16类型，只有9条NoContraction。原生代码仍有真实的动态buffer/texture访问、循环和光照数学，没有证据能保持通用语义而整段删掉。NOP、SS/SY及编译器估计stall是合法化/静态信息，不是实测的独占等待时长，不能凭这些数字删除保护。

## 只保留一条可证伪候选：descriptor前言预取

两条HDR实际IR3给出了具体消减对象：5c的169条前言中有20条写dummy r63的SAM/RESINFO/LDC请求，38的226条前言中有21条。5c没有前言材质条件分支；38的21条也全部位于第一个材质条件分支之前。

Mesa `ir3_nir_opt_preamble.c` 说明这些是A7xx的descriptor预取，用于early preamble隐藏访问延迟，且已经对descriptor去重。它们不是额外游戏采样调用，不能把每条都算成一次DRAM纹理数据读取，也不能仅因写dummy寄存器就判为错误。

唯一候选为现成 `IR3_SHADER_DEBUG=nodescprefetch`：只关这项预取，保留uniform preamble、精度和应用同步。源码 `ir3_compiler.h::ir3_shader_debug_hash_key()` 未将该选项排除，`tu_pipeline.cc::tu_hash_shaders()`将其混入原shader cache key；无需删除缓存、改driver UUID或资源包。

候选的**工作量预算**是每次对应前言执行少20/21条显式descriptor请求及辅助设置。尚无前言独占耗时、实际执行次数和延迟数据，不能据此承诺整帧收益，尤其不能将全局stall百分比乘帧长当作独立原因或候选收益预算。关闭预取也可能把等待移到首次使用，因而变慢。

已准备普通参考/候选各一个8秒的现成Perfetto初筛，以及独立一次标准KHR候选IR窗。必须先验证实际20/21请求消失、完整PSO与源字节仍匹配，再报告body读取、寄存器、preamble及整帧变化；工具窗FPS不进入普通性能结果。本记录不包含尚在排队的新性能结果。

## 源码备份与复现入口

[完整v2补丁](../scripts/dev/experiments/upstream-driver-native-ir-v2.patch) SHA256：

`df49440274c2cc09a81913449a0e07820b6ce4df8d967fc37644e20763951d73`

**它已经包含v1 loader补丁，不要在已经应用v1后直接再应用整份v2。** 推荐从干净的独立b811b90 worktree开始，只应用这一份v2：

```sh
D2R_ROOT=/home/dl/d2r-mobile
git -C "$D2R_ROOT" worktree add --detach "$D2R_ROOT/.local/worktrees/upstream-native-ir-repro" b811b90172f4eb40ec1bc89d903308e0c5ab88d0
git -C "$D2R_ROOT/.local/worktrees/upstream-native-ir-repro" apply --check "$D2R_ROOT/scripts/dev/experiments/upstream-driver-native-ir-v2.patch"
git -C "$D2R_ROOT/.local/worktrees/upstream-native-ir-repro" apply "$D2R_ROOT/scripts/dev/experiments/upstream-driver-native-ir-v2.patch"
```

已用独立临时git index验证：b811b90直接应用v2成功；b811b90+v1再直接应用整份v2被拒绝；精确撤去v1后应用v2，与直接应用v2得到的tree完全相同，7个源文件均与已构建冻结worktree逐字匹配。主工作区index未改，没有提交。

构建依赖：NDK `28.2.13676358`、CMake/Ninja、原v1冻结的生成头/触控资源，及本仓库Volk。`DM_GEN_DIR`应指向已验证的 `upstream-driver-control-v1-gen`，`DM_CONTROL_VOLK`指向宿主仓库 `dm/third_party/volk`；这些不是补丁内的新游戏产物。原四项生成输入SHA已逐项核对。

```sh
D2R_ROOT=/home/dl/d2r-mobile
cmake -S "$D2R_ROOT/.local/worktrees/upstream-native-ir-repro/dm/android" \
  -B "$D2R_ROOT/.local/build/upstream-native-ir-repro" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$D2R_ROOT/.local/android-sdk/ndk/28.2.13676358/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-31 \
  -DDM_GEN_DIR="$D2R_ROOT/.local/build/upstream-driver-control-v1-gen" \
  -DDM_CONTROL_VOLK="$D2R_ROOT/dm/third_party/volk"
cmake --build "$D2R_ROOT/.local/build/upstream-native-ir-repro" --target dm -j 4
```

诊断默认关闭。冷启动前的 `gfx-pipe-internals` marker可为空（有界全量）或填调用者选择的十六进制原key；实现中没有硬编码HDR/game key。输出为独立 `pipeline-internals-<PID>`，不覆盖旧文件，限制为8MiB/文件、128MiB总量、128PSO、32executable/32representation，VK_INCOMPLETE最多三轮。源码保存真实post-rewrite SPIR-V、标准统计/IR与完整PSO schema，不把空返回或截断文件伪装成汇编成功。冷诊断自动禁用warm/async pipeline创建，不可用它的运行性能替代普通基线。

## 本机原始证据

- 两臂原文件：`.local/logs/q735-system-native-ir-v1/{SYSTEM-IR2,TURNIP-IR1}/`。
- 严格配对：同目录 `analysis-agent/matched-pso-native-audit.json`、`concise-HDR-statistics.json`。
- 可复用离线整理：`analysis-agent/match-and-audit.py`；现成spirv-dis输出在同目录。
- 工具准入/源码来源：`.local/logs/remaining-upstream-gap-v1/gpu-native-representation-audit/`。
- 冻结构建及六项host接口守卫：上述目录 `v2/ready.json`、`host-interface-tests.json`、`README.zh-CN.md`。守卫测试不代替真实驱动结果。
- 候选队列入口：`.local/logs/q735-system-native-ir-v1/analysis-agent/RUNNER.zh-CN.md`、`runner-ready.json`。
- 补丁应用核验：`analysis-agent/patch-backup-application-check.json`。

复现真机采集仍需原合法游戏模块/资源、相同原角色clone、Android壳和adrenotools配套库；补丁只覆盖宿主诊断，不包含或改写这些输入。本轮源码备份和文档整理没有操作ADB、重编产物或修改已冻结runner。
