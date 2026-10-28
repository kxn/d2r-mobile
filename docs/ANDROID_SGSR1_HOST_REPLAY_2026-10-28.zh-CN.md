# SGSR1 最小替换的历史抓帧画面验证（2026-10-28）

**接入链已经通过一次历史静帧验证：只替换原3D放大shader，输入、增益、UI与后续合成均保持原接口，没有产生明显黑块、几何错位或UI模糊。** 同1.0比例下部分细纹理更平滑/块状，不能称画质胜过原滤波。尚未测试真实低分辨率输入、移动闪烁、当前Adreno运行或性能。

这轮继续[原pass合同核验](ANDROID_SGSR1_PASS_CONTRACT_2026-10-28.zh-CN.md)，不改之前两份报告与证据。新证据目录 `.local/logs/snapdragon-super-resolution-host-replay-v1/`，所有源、配置、工具、输入输出与图片SHA汇总在 `receipt.json`。没有ADB、游戏CPU执行、硬件GPU使用、生产代码修改或部署。

## 实际执行与准入

使用既有frame711完整GFXR，游戏输出2750×1260、原放大比例1.0；不在CPU上缩小图片，也不改原捕获资源、分辨率或其他shader。复用上一轮同一捕获、同一工具、同一llvmpipe设置导出的原版结果，先校验捕获和工具SHA未变，避免无必要重做基线。

官方 `gfxrecon-replay --replace-shaders` 的文件规则为 `sh<捕获module ID>`。替换目录只有一个文件 `sh206`，工具日志准确记录一次 `Replacement shader found`。其内容是上一轮已经编译通过的独立候选，不是修改后的游戏binary，也没有改pipeline/layout或命令记录。

替换前用现成 `spirv-cross --reflect` 对照原FS、候选FS及原VS：

- FS输入均为location2 `vec4`，匹配原VS同位置输出；颜色输出均为location0 `vec4`。
- set1/binding1和binding2均为原sampler2D；set1/binding3均为原std140 slot7，vec4数组、offset0、stride16、block_size65536。
- 两者访问原UBO的前三组vec4，实际descriptor range48没有扩大，不添加常量/descriptor或图像。
- 原UI与3D compose shader211、原增益采样和最终RGBA8附件保留。

完整配置与反射保存在 `interface-contract.json`、三个 `*-reflection.json` 和 `dump-candidate.json`。

回放固定 `/usr/share/vulkan/icd.d/lvp_icd.json`、`LP_NUM_THREADS=2`、offscreen、`rebind`和`remove-unsupported`。**指定两个draw的资源已全部导出，随后仍因原QCOM私有尾块142343返回255。** 不能称整份捕获成功回放；跨驱动与移除不支持feature也使它只能作为历史接口/画面辅助证据，不是原手机原生结果。没有使用回放计时来声称性能。

## 保持了什么

官方资源导出对照证实：

| 检查项 | 结果 |
|---|---|
| 原3D输入image2429 | 原始字节完全相等 |
| 原增益image2480 | 原始字节完全相等，仍在原滤波结果之后乘入 |
| 独立UI image2146 | 原始字节完全相等 |
| VS／放大／compose所需UBO | 原始字节完全相等 |
| 新放大结果 → compose的image2584 | 两次导出的原始字节完全相等，准确传递 |
| 放大输出与最终输出alpha | 全部255，没有改变 |
| UI alpha为0的144,137个同坐标像素 | 最终RGB改变0个；此处alpha是保留3D的权重 |

最后一项是本捕获同像素采样映射下的诊断，不外推为任意viewport/UV的通用证明。半透明UI下场景本来就可见，那些输出随3D滤波变化属于原compose语义。

数值差异是“与原滤波相差多少”，不是画质评分或真值误差：

| 输出 | RGB平均绝对差，8bit码值 | RGB绝对差99百分位 | 最大单分量差 | alpha改变 |
|---|---:|---:|---:|---:|
| 原放大pass输出 | 1.053/255 | 8/255 | 34/255 | 0 |
| UI合成后最终图 | 1.035/255 | 8/255 | 34/255 | 0 |

最终图RGB平均有符号偏移约为+0.014、+0.018、+0.019个8bit码值，未出现全局压暗或丢失原增益的明显特征。这不保证所有场景的高光/颜色都正确；上一轮实际输入大于1，因此候选仍需要保留HDR值域的适配，不能换回官方默认上界1截断并沿用本结论。

## 实看画面与范围

已分别用 `view_image` 检查原版与候选完整最终图，以及人物、石墙、传送点、药水栏的原生像素并排裁切。没有CPU重采样或人工生成低res输入；PNG只是现成工具原始RGBA导出的无损展示。

- 人物、石墙边缘与传送点位置一致，没有看到黑块、缺物体、整体色调变化或UI处理侵入。
- 字体/药水栏保持原来的清晰度，与独立UI字节和完全覆盖像素的数值检查一致。
- 原生1.0输入下，候选会改变局部锐化与细纹理，部分石墙、地面表现更平滑、块状。**这不是已证明的画质改善。** SGSR目标是用较低3D输入改善最终画面，本轮1.0验证只负责排除接错资源、增益/alpha/UV/UI接口等明显问题。
- 静帧不能评估移动闪烁、细线稳定性、技能噪点或转场；没有真机精度/驱动编译结果，也未量化新增gather/ALU的GPU成本。

本机可查看的产物：

- 完整原图 `baseline-142297.png`、完整候选 `candidate-142297.png`。
- 原生像素并排 `compare-character.png`、`compare-stone_wall.png`、`compare-portal.png`、`compare-UI_potions.png`。
- `absdiff-maxRGB-x8-142297.png` 是明确放大8倍的最大RGB差异灰度，仅供定位；不是原图，也不是细节恢复展示。
- `comparison.json` 保存完整统计、输入SHA与UI检查。

## 下一步只补真实缺口

1. 取得/复用当前0.85实际放大draw的输入纹理尺寸、slot7 UV缩放/texel/边界，与历史1.0合同连接；当前仍缺这一点，不能用本轮2750×1260常量替代。
2. 保持游戏输出与交换链尺寸不变，在真实0.85先对照原滤波/候选，再用真实0.75输入比较画质与GPU整轮成本。不得先用CPU缩图假扮游戏低res。
3. 用移动画面覆盖地面、细线、亮技能、暗地牢与半透明UI，验证取消upper-clamp后的高光、噪点、光晕、闪烁，再决定是否正式做默认关闭的可选画质功能。

候选保留为独立host实验输入，不进入通用NVN语义，不增加相机/场景猜测，不默认启用。此次能说明“这个最小接入点确实能按原合成链出图”；不能说明已经提升低res画质、实现真机超分或提升帧率。
