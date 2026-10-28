# 原3D放大入口的SGSR1-v2实验源码归档

这里只保护已经实际编译过的v2输入，不接入运行时、不增加开关，不含SPIR-V、APK、驱动、图片或游戏binary。v3数值修正仍只是[离线提案](../../../docs/ANDROID_SGSR1_V3_NUMERICAL_PLAN_2026-10-28.zh-CN.md)。

- `sgsr1-original-upsampler-contract-v2.frag`：实机两PSO编译探针使用的准确候选源码，未重新生成或改变算法。
- `upstream/sgsr1_shader_mobile.frag`：固定官方commit的原始输入，原字节保留。
- `original-current-upsample.glsl`：当前cf83原FS经现成spirv-cross导出的文本，用于审查候选的完整原fallback，不含原游戏模块或SPIR-V。
- `LICENSE.sgsr`：官方SGSR BSD-3-Clause版权及条款；候选保留原SGSR版权头。
- `source-receipt.json`：源码SHA、来源和实际编译输入/结果的映射。原始较大证据仍在其中声明的本地忽略目录，不作为本归档的构建依赖。

在安装现成 `glslangValidator`、`spirv-val` 后，从仓库根目录运行：

```bash
python3 tools/experiments/sgsr1-upsample/rebuild.py
```

它只在新的 `.local/build/sgsr1-upsample-v2-rebuild/` 目录运行原编译和校验命令，并要求产物准确匹配：

```text
ff7953bb9e0c8b4f21f17896e107b801fbef6378c7e0e9a1a0bf6dcbe18bdfc1
```

等价原命令为：

```bash
glslangValidator -V --target-env vulkan1.1 -S frag -o OUTPUT/candidate.spv tools/experiments/sgsr1-upsample/sgsr1-original-upsampler-contract-v2.frag
spirv-val --target-env vulkan1.1 OUTPUT/candidate.spv
```

`OUTPUT`需为预先建立的独立输出目录；不要覆盖已有v2证据。本次准确重建核验使用glslang15.2.0和SPIRV-Tools2025.1。其他编译器可能生成不同但合法的SPIR-V；入口保留失败及版本记录，不将新产物冒称已经实机验证的ff7953bb。

v2保留原C7/UV/gain/alpha接口：坐标highp，边界或资格不满足时执行原完整五tap；SGSR结果仍在最后乘原增益，gain之前不截断上界1。它已经在准确12b驱动上创建管线，**没有v2绘制、当前0.85画质或性能通过**。实机编译还确认滤波/权重被降低为FP16，因此需要后续数值范围/异常回退修正。原1.0回放图片属于v1，不能挪作v2验证。

结果边界和首次安装编排失败/同会话恢复分别见[实际编译结果](../../../docs/ANDROID_SGSR1_ACTUAL_COMPILE_RESULT_2026-10-28.zh-CN.md)与[失败记录](../../../docs/ANDROID_SGSR1_INSTALL_ORCHESTRATION_FAILURE_2026-10-28.zh-CN.md)。本归档没有设备操作入口，也不会重跑GFXR或修改主游戏。
