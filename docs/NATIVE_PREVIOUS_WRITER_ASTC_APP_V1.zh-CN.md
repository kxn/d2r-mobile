# 前一提交 producer 的实际 ASTC 普通 UID 验证包装

此树复用 8eee 的独立 Activity/JNI、真实 early memfd heap、AA185 和原认证 firepit135168B/native table/family/window 夹具；生产 runtime 保持 f637327，测试扩展只在 `DM_VULKAN_NATIVE_WINDOW_APP_FIXTURE` 编译与专有 cold 环境选中时运行。需要新编译 host SO，因为原 f637 的多阶段字节测试是 RGBA，而 AA185 的图像导入合同仅限 ASTC。不能把旧 RGBA 或旧 family372 通过冒充此验证。

原 full9mip、tail5、两次 window union/固定绑定、双向 alias 与 retirement 全部保留。新增40检查：前一 chunk 真 B2T mip4 与原 compute26 的从原 GPU 捕获的有限 physical-byte scratch 恢复旧版本各一次，后一 chunk 先记录无关 pending pass，再 FS mip4、CS mip5、T2B mip6；FS/CS 对照原标准上传 reference，T2B 对照原压缩 block。两个 positive 都交换有效 ASTC 块并断言 FS 参考像素发生真实改变；scratch 只是夹具有限 GPU Buffer 历史，不计生产收益。断言 retained writer 仍在、current chunk 被拒绝，以及 unknown ALL writer 必须走完整目的范围。没有新 shader、codec、GPU 计时器或资源协议。canonical 与六库／DEX／原始资源／270 shaders 依赖闭合后才能交 root 实际执行；此文件不代表已在 Android GPU 通过。

## e253a09 接线纠错

e253a09 普通 UID 实测只执行原372项，新增行缺失，严格控制器正确拒绝并完成清理。原因是 CMake option 仅加入 JNI 源，没有把同名预处理宏传给 `dmhost_android`；cache 的 ON 不代表 conditional C body 编译。后继给实际 target 定义宏，并在 canonical 打包前及独包打包时分别检查最终 host ELF 中唯一新 oracle 标识，缺失必须失败。完整交付还归档真实 compile flags、实际预处理 body 和该 include 的真实 NDK dependency；不能再凭源文件存在、producer hash 或 JNI configuration true 宣称新增测试已执行。原失败与旧 sourceReady 均保留。
