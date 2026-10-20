# 已定义压缩数组的实际 ASTC 普通 UID 验证入口

基于 56c91f7 原测试代码，只加独立 Activity、冷 JNI 选择、构建和既有事务编排。两个 case 必须各自冷启动：

- existing-defined：原 shared_import_windows_test + DM_ASTC_COPY_NEGATIVE=existing，原 BC3 128²×42层×7mip，检查旧 layer0 与 ASTC incoming layer41 的真实 GPU 像素。
- mixed-array：原 mixed_compressed_copy_test + DM_MIXED_COMPRESSED_ASTC_SOURCE=1，3层×3mip 保留，8×8 ROI 实际 ASTC，所有像素/旧句柄字节检查不改。

强 heap startup shim、AA185、真实三 UUID、普通 UID/首 PID、精确 driver maps/stat/SHA、无游戏模块和独立包清理沿用原工具。原 135168B resource4 随原工具保留仅为已有协议上下文，不把本次结果称为 native 材质采样、真实游戏或性能验收。LVP 的 BC oracle 不等于实际 ASTC。

独立 APK 只包含六个库和测试 DEX/manifest/license；未包含游戏。默认离线 plan，不访问设备。两个 case 的实际硬件通过均为后续授权 gate。
