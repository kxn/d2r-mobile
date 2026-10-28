# 12b 实际离屏验证的 HOF v2 源码

这是独立实验归档，不是主游戏集成。上一级目录保留旧 compile-only 输入，其坐标合同后来在离屏 v1 被证伪；不得将旧归档重标为通过。

此处 GLSL 与实际离屏 v2、历史 .85 三过滤对照的 HOF 输入逐字相同。注释中“compile probe 尚不创建资源”属于冻结源码当时的说明；后来离屏 fixture 已建立真实权重 image/view/sampler 并绘制，源码字节未为归档改写。

只重建官方 SPIR-V 输入，不运行设备：

```sh
python3 tools/experiments/qcom-hof-upsample/qualified-12b-v2/rebuild.py
```

使用现成 `glslangValidator` 与 `spirv-val`。必须重建出 `d5a54a112042303f669f15edec359fa9985c7712cbe8ed255a7f5f94fff80aa5`；工具版本变化导致字节不同应保留失败，不替换原收条。

权重生成/资源创建合同见 `weight-contract.md`。实际执行与画面结论见仓库 `docs/ANDROID_QCOM_UPSCALE_FUNCTIONAL_AND_STATIC_2026-10-28.zh-CN.md`。原 raw capture、SPV、APK、驱动与图片留在本机 `.local`，此目录只归档源代码、版权链与证据收条。这里不提供自动安装/设备执行入口。

版权与来源沿用上一级 `NOTICE.md` 和 `LICENSE.sgsr`：HOF 是本仓库的独立接口适配，不是官方 SGSR 内容自适应算法；`originalComplete()` 原游戏 shader 的来源不因此重许可为高通 BSD。
