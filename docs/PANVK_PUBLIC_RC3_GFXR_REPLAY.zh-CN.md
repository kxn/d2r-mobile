# 公共 RC3 的完整前缀回放对照

本工具只在独立普通 UID 包运行原封未改的 Arm GFXReconstruct replay SDK。原 replay APK 为 `88b0ef8c…`，完整旧 startup-prefix 为 `cc826955…`（41,222,050 字节、186 次原 API submit）；捕获来自旧 offscreen host／c5ec 驱动，绝不能改写成 HUD55／公共 RC3 来源。待测驱动单独限定已下载公开 RC3 `b46b9f04…`，不改驱动、APK、bridge、trace、着色器或 API 参数。

优先复用完整前缀：旧 c5ec 的官方 `--sync --validate --memory-translation none` 回放在 submit186／文件索引149328发现 DEVICE_LOST；加入 render-pass serialization 仍失败。中途 trim150–174 则到 EOF，说明不能用 trim 结果排除早期状态／历史。公共 RC3 是否复现同一旧 Vulkan 流尚未验证，本轮只改变待测驱动。SDK bridge 已检查私有驱动 executable inode／PanVK driverID20；公共 RC3 有 HMI／ICD 入口，因此不需要重编 APK；实际 loader／VVL 链仍须用本轮日志确认。

controller 从已实测的9c64同步回放版本派生。原 first-PID、粘性多PID拒绝、精确 owned-stop、单安装／单启动和独立包清理函数保持相同；本轮只接入公共驱动身份及现 HUD55 selected35 root baseline。生产 APK／已安装 ELF／35 文件／shader chain 均前后核验，并对 driver、shaders、overlay、temp、cache、modules 整目录做完整只读哈希清单（128项分批、支持真实文件名中的空格，拒绝符号链接）；保留全局 GPU layer 设置和 PanVK property 原值。原游戏必须已经保存并停止；发现运行中进程或现有 production transaction 即拒绝，不启动、不代存、不强杀原游戏。独立 fixture 内核对原工具文件后才能清理，不改系统层配置。

步骤：先 source root review；默认运行只输出已核验的离线计划，不调用 ADB。root 调度唯一一次 `--execute`，使用源 Ready 中冻结 argv。必须保留 capture 原 c5ec 身份、测试驱动 b46 身份、完整官方 SDK／VVL／Mesa 日志和本次 PID／UID。DEVICE_LOST 或任何 VUID 都保留失败状态并执行原 owned 清理，不能写成游戏通过。原 capture receipt 自身带 GFXR 添加 extension dependency 的 VUID01387；旧 snapshot Invalidate 01390 也是工具插入问题，不能算作应用错误。若出现 R16 comparison VUID06479 则保留标注，不做过滤；其修复由另一条主线独立处理。

VVL full-WSI 老路线在 Android ANB import 就返回 INVALID_EXTERNAL_HANDLE，未到原 DEVICE_LOST，不能继续把直接 provider 加 validate 当作已验证。direct ICD 路径明确 bypass Android layer chain；原 RenderDoc API probe 也曾在 instance 阶段失败。GFXR offscreen trace 没有 Vulkan WSI／Present，可沿已实测官方 SDK+VVL 路径绕开该工具接入障碍；不证明当前 HUD55 整机故障来自同一调用，也不提供像素、FPS、热稳态、CPU共享或可玩性结论。`--debug-device-lost` 在现有工具源码中明确未实现 Vulkan；不为它重复上机。`--isolate-render-passes` 可作为以后有依据的独立实验，本轮不加入。

本轮有限离线检查覆盖来源／完整前缀拒绝、HUD55 baseline拒绝、空格及128分批完整目录、缺失／空目录区分、原PID/stop函数未变，以及真实计划／旧驱动拒绝。只使用现成官方工具读取捕获元数据，未加计时器、采样器、验证器或 GPU/驱动诊断代码。实际回放由 root 另行批准调度。
