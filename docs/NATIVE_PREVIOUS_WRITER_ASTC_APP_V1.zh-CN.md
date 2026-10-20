# 前一提交 producer 的实际 ASTC 普通 UID 验证包装

此树复用 8eee 的独立 Activity/JNI、真实 early memfd heap、AA185 和原认证 firepit135168B/native table/family/window 夹具；生产 runtime 保持 f637327，测试扩展只在 `DM_VULKAN_NATIVE_WINDOW_APP_FIXTURE` 编译与专有 cold 环境选中时运行。需要新编译 host SO，因为原 f637 的多阶段字节测试是 RGBA，而 AA185 的图像导入合同仅限 ASTC。不能把旧 RGBA 或旧 family372 通过冒充此验证。

原 full9mip、tail5、两次 window union/固定绑定、双向 alias 与 retirement 全部保留。新增36检查：前一 chunk 真 B2T mip4 与原 compute26 的同 backing 原地复制各一次，后一 chunk 先记录无关 pending pass，再 FS mip4、CS mip5、T2B mip6；FS/CS 对照原标准上传 reference，T2B 对照原压缩 block。断言 retained writer 仍在、current chunk 被拒绝，以及 unknown ALL writer 必须走完整目的范围。没有新 shader、codec、GPU 计时器或资源协议。canonical 与六库／DEX／原始资源／270 shaders 依赖闭合后才能交 root 实际执行；此文件不代表已在 Android GPU 通过。
