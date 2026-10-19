# Owned 排除窗口与原 NVN 纹理负检查缓存（独立实验）

本层基于 scoped API `1358455`，仅将固定查询范围的负检查凭证接入原 `image_upload`。仅在测试程序创建任何图像之前才允许切换 `vk_test_texture_owned_scopes(1)`；同值调用始终是 no-op，因此原完整 suite 的晚调用不会失败。只有切换值才需要冷状态。此模式；没有游戏启动选项、任意 host pointer 注册入口或实际 NVN Buffer import。本层不能用作游戏性能或真实导入通过的证据。

启用后每次负检查快返之前，都先对同一个 `tex.mem / tex.L.size` 查询做 scoped refresh。必须同时保持原上传 stamp、range changes 和 exclusion version；不以 global epoch 相等绕过 overlap/lifetime 检查。观察失败、重叠、过渡、时钟耗尽和 slot 复用都继续原 `nvn_dirty_since`。只有原页检查实际返回未修改，才保存新负证明。上传时清空整个 scoped proof，永不从新 epoch 推进上传 stamp；保留 N 先预留、N+1 先发布时的原语义。

默认分支保留原 global/range 策略；union 将图像内原 24B proof 扩成 48B，即每个 image 增加 24B，测试开启时每次候选快返增加 indexed metadata refresh（无注册表/页/内容扫描，无分配和等待）。含测试入口的构建有一个默认 0 分支；没有 `DM_VULKAN_TEST_EMBED` 的正常目标没有选择器，编译器可消除此路径。不是免费优化，尚未以实测证明适合默认开启。

GPU oracle 复用原 MemoryPool/TextureInitialize、packaged image 上传及真实 ImageToBuffer 检查，不重新实现采样器或计时工具。相同 1MiB tracked Pool 中，排除一页后：另一纹理的稳定使用可由原 counters 证明不重复 dirty 检查；排除页直接写不破坏不重叠 proof；普通页真实写异常、begin/release、同地址槽生命周期重建和原 N/N+1 发布仍触发正确数据检查。测试仅持有明确自有页面，GPU 队列 fence 完成后 release；没有 NVN/驱动 import。

官方 Lavapipe + Khronos VVL/synchronization validation + 原异步完成路径：scoped 模式 9301 checks；原模式及强制 reference 各 9300 checks。NDK 28.2.13676358/API29 编译原纹理模块为 AArch64 对象。没有手机操作、真实游戏调用或 FPS/带宽收益报告。

独审修正：旧 setter 对同值晚调用也报错，原 ae4b895 默认完整测试实际复现失败；修正后保留真正切换的冷状态检查，并在已有 GPU 图像后显式再次选择同值。原完整默认 suite + 官方 VVL/sync/async 22,035,661 checks 通过；旧失败日志和旧冻结件保留。
