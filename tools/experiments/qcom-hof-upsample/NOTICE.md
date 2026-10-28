# 来源与版权

此HOF适配为本仓库的独立实验，使用Khronos公开QCOM1接口与规范，没有包含SGSR的内容自适应filter函数，也不声称由高通提供或认证。

`originalComplete()`来自本仓库已审查的SGSR1-v2适配，其本体是当前cf83原 `ps_upsample_p` 经现成spirv-cross导出的原五tap行为，仅作变量更名，来源及原字节SHA见 `source-receipt.json`。它不是新写的玩法或游戏binary补丁。原始shader文本另见 `../sgsr1-upsample/original-current-upsample.glsl`。

此前SGSR研究与适配基于官方仓库 `SnapdragonGameStudios/snapdragon-gsr` 固定commit `d926f074bcb9d714e179f1ce0fcb9ee2eeb5074e`；保留其原始BSD-3-Clause版权及条款于 `LICENSE.sgsr`，来源链不丢弃。该版权文件不表示本HOF算法是官方SGSR，不将原游戏shader内容重新声明为高通BSD代码。本仓库新编写适配部分沿用仓库MIT许可；各来源分别按其原条款处理。
