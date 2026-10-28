# SGSR1 原3D放大入口与颜色合同核验（2026-10-28）

**已用旧完整抓帧证明：可以在原 `ps_upsample_p` 的滤波部分接入SGSR1，保留独立UI，不必在最终Present重复放大。另一个实证是：原输入确实含大于1的颜色，不能直接套用SGSR1默认的上界截断。** 本轮没有ADB、游戏CPU运行、部署或生产代码修改。

这是[前一份调研](ANDROID_SNAPDRAGON_SUPER_RESOLUTION_RESEARCH_2026-10-28.zh-CN.md)的独立补充；前文及其证据指纹保持不变。所有新证据在 `.local/logs/snapdragon-super-resolution-pass-contract-v1/`。

## 抓帧资格与身份桥接

使用既有frame711完整GFXR及官方 `gfxrecon-convert` 导出的 `full-api.jsonl`。该帧是2026-10-18的Adreno735历史捕获，输出2750×1260，本次实际导出的原放大常量为1.0比例。**它不代表现在1968×900、0.85比例的运行状态，也不提供当前帧率、GPU成本或所有场景颜色合同。**

旧 `.local/build/current-q-sdp-snapshot/api.jsonl` 与新完整转换文件的末端block index存在1的差异。下文统一使用新 `full-api.jsonl` 的block index；通过shader module ID、SPIR-V原字节SHA、pipeline及资源对象桥接，不把两份index混用。

捕获导出的shader原字节与已有声明式shader cache完全相等：

| 捕获module | 游戏源身份 | key | 用途 |
|---|---|---|---|
| 75／82／89／25834 | `ps_ui_p.bin`，变体0／1／3／4 | 详见身份JSON | 单独UI目标上的735次draw |
| 199 | `ps_tonemap_p.bin:0` | `a1b1aab66933fa8f` | 3D调色 |
| 202 | `ps_fxaa_p.bin:0` | `ebcfcd1fa0a77dbc` | 3D抗锯齿 |
| 206 | `ps_upsample_p.bin:0` | `52aac1f2306b8fc5` | 3D原放大/锐化/增益 |
| 211 | `ps_compose_p.bin:1` | `9a650561e5c40791` | UI与放大后的3D合成 |

module206的SHA256为 `c5b46230de7b4c6c8ff64afe4abe39a02a0003eee0cd6e9054921585132518f0`。当前优化链中同一源shader的字节不同，不能用当前字节SHA假装与旧捕获相同；身份JSON另保存旧cache准确匹配。用现成 `spirv-cross` 反编译辅助解释语义，未自行实现着色器反编译或采样工具。

## 已证明的真实执行与资源顺序

| 顺序/调用block | 实际操作与资源 |
|---|---|
| 132704…134372 | 735次UI draw写image2146；全部FS精确匹配 `ps_ui_p`，目标RGBA8、2750×1260 |
| 142244 | `ps_tonemap_p` 读取3D与3D LUT，写image2325，B10G11R11浮点 |
| 142260 | `ps_fxaa_p` 读取2325，写image2429，B10G11R11浮点 |
| **142276** | **`ps_upsample_p` 读取2429与增益2480，写image2584，RGBA8、2750×1260** |
| **142297** | **`ps_compose_p` 读取独立UI2146和3D2584，写image2678，RGBA8、2750×1260** |
| 142329 | 最终blit将2678送往交换链；这里已含UI与原3D放大 |

“UI在之后”准确含义是**最终UI合成在放大之后**；UI图自己的draw实际上更早完成。这不是推测的按尺寸分类：735个draw的FS原字节、它们写入的附件、后续compose的真实descriptor已连成同一资源链。兼容层不需要重排UI draw。

### 原放大pass的绑定

实际pipeline23282，secondary command buffer20247，执行于primary16230／submit142324。draw的深度测试、深度写、混合均关闭；颜色RGBA全写。

| set1 binding | 实际对象 | 格式/采样/用途 |
|---|---|---|
| 1 | view2501 → image2429 | `B10G11R11_UFLOAT_PACK32`；linear、clamp-to-edge；主颜色 |
| 2 | view2585 → image2480 | `R8_UNORM`；nearest、repeat；按原输出UV采x通道作最终乘数 |
| 3 | buffer33、动态偏移2939904、range48 | 原slot7的三组vec4：UV缩放/尺寸、texel步长/锐化系数、UV有效边界 |
| output | framebuffer2589、view2588 → image2584 | `R8G8B8A8_UNORM`；原alpha固定1 |

原滤波语义可概括为：

```text
uv = clamp(originalUV × c7[0].xy, c7[2].xy, c7[2].zw)
filtered = center × (1 + 4k) − k × (left + right + up + down)
gain = texture(binding2, originalUV).x
output = (filtered × gain, 1)
```

原程序还将NaN采样坐标替为0；左右上下采样保留原texel步长。实际c7的锐化系数为0.06。不能把增益搬进每个SGSR采样，不能将UI一起锐化，也不能先执行原锐化再叠一遍SGSR锐化。

后续compose使用UI的alpha作保留3D的权重，核心形式是 `mix(uiColorAfterOriginalOptions, sceneColor, uiAlpha)`，不能改写成假定普通straight-alpha的另一套混合。该历史帧的两个颜色模式分支flag均为0；这不证明未来HDR/辅助颜色模式永远关闭。最小接入保留compose整条不动。

## 官方资源导出给出的值域证据

用既有官方 `gfxrecon-replay` 的资源导出，在llvmpipe上重放同一个GFXR，指定仅导出上述两个draw的附件及绑定。固定软件ICD和2个CPU worker，没有使用主机硬件GPU、ADB或游戏CPU模块。使用 `rebind` 和 `remove-unsupported`，所以属于**历史捕获的跨驱动辅助证据**，不冒充当前手机原生结果。

目标资源全部导出后，工具仍因旧QCOM私有尾部metadata142343返回255，和该捕获此前的导出限制相同。不能称整个replay成功。导出JSON含目标draw、descriptor、原格式、尺寸和原始二进制；新增程序只整理这些现成输出，不自建计时器或图像滤波器。

| 资源/通道 | 已导出内容 |
|---|---|
| 主颜色2429 R | 最大1.828125；512个分量大于1 |
| 主颜色2429 G | 最大1.421875；160个分量大于1 |
| 主颜色2429 B | 最大1.90625；2637个分量大于1 |
| 增益2480 | R8取值67…255，即约0.263…1；不是恒定1 |
| 放大输出2584 | 全3,465,000像素alpha为255；其原字节SHA与compose输入2584完全相同 |

各通道统计不是独立像素数量，也不是fragment执行次数。浮点格式本身不等于一定超范围，但这里已经取得实际大于1的内容；另一方面，这些范围也不能外推为所有地牢、亮色技能、HDR设定的上限。3D tonemap采样的LUT仍为RGBA16F，不能仅凭函数名字“tonemap”证明其输出必在0…1。

SGSR1官方代码在边缘分支执行 `clamp(color + deltaY, 0, 1)`。原流程则是先滤波、后乘可能小于1的增益，再由RGBA8附件转换截断。提前截到1可能损失原本乘增益后能保留的高光；例如原值1.5、增益0.5，提前截断会将0.75压成0.5。因此不能原样接入并声称保持原颜色合同。

## 最小可选实现边界

优先候选是**只替换原pass的滤波内核**：复用主颜色binding1、增益binding2、原slot7、原varying与原RGBA8输出；SGSR结果仍在最后乘原增益，alpha保持1，compose与present不变。这样不增加纹理、render pass、CPU回读或同步边界。

独立host候选已放在 `host-candidate/sgsr1-original-upsampler-contract.frag`，经 `glslangValidator` 和 `spirv-val` 的Vulkan1.1检查通过。它以固定版本官方SGSR1为来源：保留算法自适应滤波、边缘阈值和delta限制，去掉RGB最终上界1截断、保留下界0，推迟高光截断到原增益及RGBA8输出之后；这是**需画质验证的SGSR1适配候选**，不称官方HDR支持或原滤波数学等价。

候选保持原UV缩放/钳位和NaN处理，SGSR texel大小来自真实输入纹理。为防止更大的gather footprint越过有效子区域，距边界保守三texel内使用原五tap；原步长与纹理texel不符时也保留原滤波。这些是独立host输入，不是已验收生产代码；fallback的算术重写未承诺与原FMA逐位相等。实际部署前应把能提前证明的资源条件放到pass资格核查，避免把诊断/不必要的判定永久留在每个fragment中。

通用NVN不能知道“这是游戏最终3D、那是UI”；因此不应在通用图像绑定、尺寸判断或present里隐式替换。应作为单独可选画质功能，以原shader源身份与反射合同明确列入构建manifest、隔离pipeline/cache变体、默认关闭，再由用户选择实验。它不属于补齐NVN语义，也不是已有Metal优化的自然等价物；不改变游戏binary、玩法或相机。是否正式增加该画质分支由主线review决定。

### 接入前剩余具体缺口

1. 当前0.85帧的**实际输入图像尺寸、slot7缩放/texel/裁剪范围**还未从同一实际draw闭环。历史帧为1.0，不能照抄2750×1260常量；若已有当前完整调用/资源导出足够则直接复用，否则只补这个缺口。
2. 一帧高光数据支持“不能提前截断”，不保证修改SGSR upper-clamp就覆盖所有输入范围；需亮部、暗部、技能、地宫的移动画面检验，核对色彩、噪点、光晕和时间闪烁。
3. 原输入的数值传递函数未被完整证明。当前只保留输入数值域，不额外作gamma/PQ转换；不能因附件UNORM/UFLOAT就擅自转换。未来开启原HDR分支时单独验证。
4. 未创建Adreno pipeline、未取得原生编译与GPU成本。通过SPIR-V检查不保证实际设备精度、效率或画质；没有任何涨帧结论。

后续实际对照固定游戏/交换链尺寸，先比较同0.85原滤波与候选，再比较0.75候选；让UI与compose保持原值。静帧只看细节，移动视频看闪烁/噪点/高光变化；用现成原厂工具核验整个GPU批次的读写/忙闲/停顿及新增滤波成本。不要用旧1.0捕获的host结果宣称当前0.85超分效果，也不退回最终Present全屏锐化来替代真正的3D输入实验。

主要机器可读证据：`api-pass-contract.json`、`module-source-exact-hash-bridge.json`、`resource-content-summary.json`、`host-candidate/receipt.json`及汇总 `receipt.json`。原完整捕获、现成工具、脚本与产物SHA均收录在汇总记录。
