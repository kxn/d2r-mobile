# Turnip：真实 SSA 读取链的调度实验源码

这里归档当前 `12b8b54a… → 82cbd84e…` 候选的完整源码差异及实际构建来源。它是编译器调度实验，不是游戏补丁；没有驱动 ELF、APK、游戏文件、资源表或设备采集数据。正式同条件游戏对照已完成：约50.37→50.14 FPS、主GPU19.654→19.734ms，本轮未见整帧收益；保留实验而不默认发布。不能从构建成功或静态指令顺序推断性能收益。

`memory-critical-schedule.patch` 是从已冻结 **12b** 源树直接到最终 **82c** 源树的 combined patch，仅改变：

- `src/freedreno/ir3/ir3_sched.c`：仅 FS 首个非 preamble block，沿真实 SSA 数据边识别有依赖关系的读取链。原 DAG、硬延迟、特殊寄存器、kill、SS 和 outstanding 上限不变；只调整原软调度偏好。
- `src/freedreno/ir3/ir3.h`：保存已判定的读取链优先级，使其通过 RA/完整指令复制进入 post-RA；新增 copy/spill 默认未标记。原 NDK record-layout 证据表明结构仍是 184B、对齐 8B，见 `stages/v2/instruction-metadata-layout.json`。
- `src/freedreno/ir3/ir3_postsched.c`：仅在原合法 DAG heads 中优先选择已标记且 hard-ready 的指令；若关键读取已是 head 但尚有硬延迟，则优先选择无 SS/SY 的可用普通向量 ALU 填槽。无合格项时保留原 fallback；不插 NOP、不跳硬延迟、不造依赖或同步语义。

它不减少逻辑读取数量，可能改善依赖读取启动/重叠，也可能损失其他纹理的提前执行。没有纹理 handle、shader key、材质或场景判断。`DM_IR3_MEMORY_CRITICAL_SCHED` 源码默认 0，独立候选 cross file 明确设 1。内存导入、native image 排布、UBWC、WSI、shader 输入和游戏原始 binary 均不在补丁中。

## 来源和缓存身份

`archive.json` 保存 baseline、三个修改文件、原工具、三个阶段源码 ledger、ELF 和缓存键的完整 SHA 映射。`stages/v1..v3/` 中的 prepare/build 脚本、实际 Meson/Ninja 命令、编译命令、cross file、阶段补丁与 build record 均按实际原文件字节归档；这些脚本不是重新发明的工具链。它们原有绝对依赖路径和严格前阶段 receipt 校验未被改写。

| 阶段 | 历史驱动 SHA 前缀 | 作用 |
| --- | --- | --- |
| baseline | `12b8b54a2514` | 原 `turnip-internal-queue-sync` builder 的冻结输出 |
| v1 | `592a3481e5f9` | 首个 FS 主 block 的真实 SSA 链优先级 |
| v2 | `24802641f748` | 标记跨 RA 保留，post-RA 在合法 heads 中消费 |
| v3 | `82cbd84ea664` | 用已有合法 ALU 填关键读取的硬等待槽 |

每阶段使用原有 `SHA256(canonical sorted compact JSON hash_input)` 作为官方 `-Dtu-build-id`，最终为 `a3ec642c90f61394d5d6ee2e926ad0e7f39c64e9714dc0d30fc9136c0b399adf`。缓存键包含原前阶段 driver、完整 source ledger、阶段补丁和 cross file 指纹；IR3 disk cache 仍使用原 ELF BuildID。**未改缓存/UUID算法，不把新驱动伪装成12b，也不通过修改白名单或旧资源表绕过验证。** 实际 Vulkan driver/layout/pipeline UUID 和资源兼容仍须独立真实设备资格。

## 最小重放入口

以下命令从仓库根执行。默认只核对归档、三个源文件、各阶段 patch 和既有 ledger JSON，不重扫 18,218 个源文件、不编译、不接触设备：

```sh
python3 tools/experiments/turnip-memory-critical-schedule/build.py
python3 -m unittest discover -s tools/experiments/turnip-memory-critical-schedule -p 'test_*.py'
```

baseline 必须是 `archive.json` 指向的原已验证 12b 源树、ledger、build record 和 NDK/工具。其原入口是 [turnip-internal-queue-sync/build.py](../turnip-internal-queue-sync/build.py)，继续复用 [turnip-native-bc3/build.py](../turnip-native-bc3/build.py) 的 manifest/工具合同。缺这些原始依赖时明确失败，不下载替代版本、不更换 receipt SHA。原始目录为本次实验的来源锚点，不能把它们静默改成其他历史构建。

在新输出目录准备/实际重建（本次归档工作没有执行这两条）：

```sh
python3 tools/experiments/turnip-memory-critical-schedule/build.py \
  --output .local/build/turnip-memory-critical-schedule-reproduced-v1
# 或一次准备并构建；必须另选尚不存在的输出目录
python3 tools/experiments/turnip-memory-critical-schedule/build.py \
  --output .local/build/turnip-memory-critical-schedule-rebuilt-v1 --build
```

这个入口把 combined patch 应用于 baseline 独立副本，使用原 helper 完整核对源码树，然后复用 `stages/v3/build.json` 的原 Meson 选项、环境和 Ninja 目标，仅替换输出路径。它复现**最终完整源码 ledger 和原编译策略**，不要求在新目录、新 Git 状态下得到相同 ELF SHA；实际新 ELF SHA 单独记录，不能沿用历史82c的设备背书。准备模式也不自动编译，已准备目录不能被覆盖重用。

原三个阶段脚本会严格检查历史 build-record SHA；在不同工作目录/Git状态重新构建中间阶段后，receipt 可能不同。因此不应手改这些检查去套历史 ELF 身份。新入口直接重放已证明相同的 final 三文件源码；各历史阶段仍完整保留用于来源审查。

## 真实证据边界

- 原 build/ISA 资格及阶段解释：`.local/build/turnip-memory-critical-schedule-v{1,2,3}/`；v3 源码 ledger 为 `4545d368f1191b1b11f246a0d1c3c0796865e0994ea33adb1a157e7e50e9efb6`。
- 当前正式同 carrier、cf83、BC3、几何/cap60 的设备对照：`.local/logs/fs-critical-schedule-game-device-v1/`。失败窗口、官方22项原始数据与最终结论保持在该目录；归档不重写它们。
- 本目录没有修改生产两处 provider allowlist、native table、当前 APK 或任何冻结产物。设备准入与性能结论不由此脚本自动授予。

实际完整结果见[GPU依赖读取调度报告](../../../docs/ANDROID_GPU_CRITICAL_READ_SCHEDULING_2026-10-28.zh-CN.md)。
