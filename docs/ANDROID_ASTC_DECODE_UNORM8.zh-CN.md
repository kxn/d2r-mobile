# ASTC LDR 的可选 UNORM8 解码

这是默认关闭的通用 Vulkan 候选。CLI 设置
`DM_VULKAN_ASTC_DECODE_UNORM8=1`，Android 冷启动 Intent 设置
`astc_decode_unorm8=true`。设备必须实际公布并启用
`VK_EXT_astc_decode_mode`；不支持时保持原行为。开关不支持进程内切换。
原启动工具可使用 `--astc-decode-unorm8`／`--no-astc-decode-unorm8`
进行冷启动对照；`--attach` 拒绝这两个选项，未指定时保持关闭。

Vulkan 原来的线性 ASTC LDR 解码中间表示为 RGBA FP16。
该扩展允许通过 Image View 选择 RGBA UNORM8。Arm 说明它可以降低解码后纹理
缓存的数据占用，并在相应硬件上使用更合适的过滤路径；不能据此声称外部 DRAM
流量减半或 FPS 翻倍。sRGB 本身已有不同的解码规定，本候选不改变它。
只选择 UNORM8，不使用需要额外 feature 的共享指数格式。

规范明确禁止 UNORM8 view 包含 HDR 块。NVN 的格式枚举、上游 Metal 的 LDR
格式映射、当前驱动没有开启 ASTC HDR，都不能单独证明上传内容没有 HDR。
因此本实现不按游戏、文件名、材质、资源地址或场景猜测。

完整 CPU 冷上传时，原 NVN deswizzle 先把每个 mip 写入现有上传 staging。
仅当是原生 ASTC UNORM、2D、单层、全部 mip 都通过内容检查时，才发布该实际
VkImage 的 LDR 证明。检查读取本次实际上传的 staging 字节，不重新读取可能
变化的 guest 内存。关闭、不支持、sRGB、数组、3D、CPU 解压回退均不扫描。
普通热路径脏页证明命中时也不重新扫描。

解析使用 Mesa 26.2.5 已有 ASTC 元数据方法的逐段原样提取；原许可及输入 SHA、
段号、段 SHA 见 `dm/third_party/mesa-astc/provenance.json`。薄封装先约束尺寸、
权重数量和位数，再调用原 block mode、void extent、CEM、endpoint-size 方法。
HDR 常量、HDR CEM、错误或未知情况保留默认模式。不执行像素解码、endpoint
展开或 infill，没有新增 codec，也不分配 RGBA 副本。

任意 shader、transfer、attachment 或未知 GPU 写入都会撤销 whole-image 证明；
复制目的地也通过原 image dependency 入口撤证。未传播复制来源的证明。
原始可写 VkImage 逃逸是 sticky：后续 CPU 上传也不能重新取得该证明。
既有只读内部借用接口仍遵守其只读访问合同。资源提升后的实际复制写入同样撤证。

View 缓存显式区分默认与 UNORM8；证明变化递增原 recipe generation。
同一次绑定在冷上传后也重新核对模式，避免先命中旧 recipe 后使用错误 view。
最终 `vkCreateImageView` 才局部组成 decode→usage 的 pNext 链，持久化的
description/recipe 不含栈指针。旧两类 view 按原图像生命周期保留，不新增 GPU
等待或提前销毁。未知内容写入后的旧 UNORM8 view 不再用于新的绑定。

新增成本是冷上传对压缩字节的一遍 CPU 读取与每块固定元数据运算；不是零成本。
现有 staging 要求 HOST_VISIBLE|HOST_COHERENT，并不保证 HOST_CACHED，读取
写合并映射可能更慢，必须在 Android 实测。仅初始化解析所需状态，不对原解码器
未使用的像素/权重数组逐块清零。常驻成本为图像证明、view 模式和 recipe 模式
字段，以及可能同时保留的第二类 view。没有新的常驻纹理副本或每 draw 内容扫描。

## 验证入口与当前边界

`dm/gfx/vulkan/tests/astc_metadata.cpp` 覆盖 LDR/HDR 常量、HDR CEM、错误块、
非法尺寸和模式组合。可由 host CMake 目标 `astc_metadata_test` 执行，并可给它
多个已有 `.astc` 文件验证。`astc_reference.cpp` 是可选独立参考 oracle，链接
现有 Arm astcenc 5.2.0 的完整库及匹配 header；库不加入 Android 运行时。
参考工具的常量块只能使用文档声明有效的字段，不能把默认 false 的 is_hdr_block
当作 FP16 常量是 LDR 的证据。

原 NVN GPU 夹具设置 `DM_ASTC_DECODE_FIXTURE=1`。Android 则使用
`run_game=false, run_vulkan_tests=true, astc_decode_test=true`，并选择
`astc_decode_unorm8=false/true` 做冷启动对照，保持原 nativeTests JNI 签名。
它先检查真实生产 view-key/pNext/撤证函数，再在 ASTC 支持设备上调用原 NVN
上传、GPU 采样及 ReadTexels：4×4/6×6、全部 mip、HDR 最后一级撤证、CPU
重传、实际 GPU copy 目的地撤证、raw-handle escape、sRGB 和数组回退。
无 ASTC 的主机明确报告 `gpu_fixture_executed=0`。

当前离线结论只证明解析、构建和已有 renderer 回归；LVP 不支持 ASTC，不能证明
新的 GPU 解码路径正确或更快。候选必须等待实际 Android GPU 夹具、普通游戏
外观与官方 Arm 计数器对照，之后才能讨论默认开启。工具数据需要区分纹理缓存
读取、外部纹理读取、整机 GPU 工作变化与整帧表现，不能用单 draw 时间归因。

Mali-G710 的独立 Android 包已完成 OFF／ON 原 NVN GPU 检查，分别
16,725／16,727 项，包含实际 4×4、6×6 采样及上述撤证、回退条件，
Khronos 同步验证未报告 VUID／SYNC 错误。首版夹具错误地用无 stage 的
裸访问接口触发 sticky escape，保持原断言失败；后仅将夹具改为原 typed
shader scope，生产实现未改以通过检查。此结果仍不证明普通游戏画面、
带宽、FPS 或持续散热收益；默认继续关闭。

依据：

- [Khronos 扩展说明](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_astc_decode_mode.html)
- [Khronos Image View 解码模式和 HDR 限制](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewASTCDecodeModeEXT.html)
- [Khronos ASTC 格式说明](https://docs.vulkan.org/spec/latest/appendices/compressedtex.html)
- [Arm 对缓存及过滤路径的说明](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/high-quality-rgbm-texture-compression-with-astc)
