# PanVK：隔离三命令流共用一个固件组的候选

本候选从原 `zenithblue-oss/panvk-kbase-android` 的 `5b87b74d3e2580d5816e7f2c629f6f016a462f28` 和其原有 179 项补丁出发；Mesa 基底为 `5a07217f034b3e50d8c7c7794f97a2df1742613b`，未修改驱动 SHA 为 `c5ecf526c0113c976366818c931173f661adb7df4f519b9a2e9b6ed9456a71b2`。

这是驱动候选，不能将编译、主机控制流检查或其他手机的结果写成 PHP110 真机通过。

## 具体变化

仅修改 Mesa 的 `src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c`：

- 一次创建可容纳三个命令流的 CSG，提前向三个 subqueue 登记相同组句柄。
- 原 VT／fragment／compute 的独立 ring 与执行流保留，分别绑定 CSI 0／1／2。
- 先终止所有已绑定 queue，随后每个唯一 CSG 只销毁一次，清除组所有权。
- 创建组后、第一 ring 分配前的失败也能回收该组；后续分配、映射、绑定中途失败沿原错误路径回收。

没有修改原 179 项补丁的 BC 解码方式、图像布局、分配要求、着色器编译、host import、页保护、堆代际或 Vulkan 完成语义。Panthor DRM 分支也没有改。

## 依据及边界

[MineGraph 的精确源码](https://github.com/MineGraph-bilibili/panvk-Android/blob/703db0ebb9afb2c97ae31f24dba5981c7c765fc1/src/panfrost/vulkan/csf/panvk_vX_gpu_queue.c#L1119) 对 MTK 的多组执行提供了具体不同的实现：将三个 CSI 放进一个组。它的整个后端还存在 BC、堆回收、编译器等很多改动；此处只隔离组组织，不整批合入。

当前 c5ec 的原真实游戏完整启动捕获，在独立官方 GFXReconstruct 重放中可复现提交 186／API 149328 DeviceLost；逐提交等待和 render-pass 序列化没有消除它。末次提交也含驱动内部 BC 解码，因此还不能确定唯一根因。

必须保留两点纠正：原超时日志 progress／marks 为零在关闭 KBASE_DIAG 时属于未写诊断，不能据此说 GPU 从未前进。虽然原调用参数标 CSG scope，`panvk_instr.h` 在 kbase 上实际提升到 SYSTEM；不能声称原同步 scope 错误。候选保留该实际 SYSTEM 同步，只验证同组固件调度是否消除当前复现。

原重放器 rebind 的 09499／03339 是额外 BDA 用法／分配标志问题，不是 PanVK 根因证据。本候选不改变 BC 内存要求，首先复用现有合法 `--memory-translation none` 全前缀捕获，避免把工具缺口混入变量。

## 重建

将上述原 179 补丁已应用的 Mesa 源复制到新的独立目录（保留 symlink，不复制 `.git`），使用 `scripts/dev/driver-experiments/panvk-single-csg-v1.patch` 做 `patch -p1`。核对所有常规源文件：只有上述单个 queue 文件应改变。

复用原 fork 的 `scripts/build-android.sh`，显式指定 `MESA`／`BDIR`／`DDIR` 为独立目录，`HOST_TOOLS` 指向原冻结工具；原 Android deps 仅只读复用。使用原 Meson 1.12.1、NDK 28.2.13676358、API 29、g615-v11-csf 通用 profile 和原 -j2。profile 只是原补丁集名，不覆盖实际 G710 识别。

原 host codegen 工具 SHA：mesa_clc `297bd8634e2ab6e528c6a1e07f44e99ca61a58fc193e28501e57f75285de2060`，vtn_bindgen2 `3a3ee2a941d8803725698798f305d341f52c079d9e42a010063b2d7025c505a5`，panfrost_compile `f46d1bb7871982bf3e45d7bf0e98b31dc982ad7f8111e8acba932eb4890b8137`。

主机检查提取实际修改的 create/destroy 两函数，用 API stubs 对句柄 0／255、正常路径和组创建／每个 CSI 的分配／mmap／VM bind／CSI bind 失败做 28 个用例。normal 与 ASan/UBSan 均通过 1310 项断言；这证明控制流、回收次数及顺序，不证明真实内核调度行为。NDK 完整驱动产物与源码/工具/参数指纹在独立 source-ready 记录中给出。

没有安装候选、修改生产资源或运行游戏来冒充以上离线验收。真机验证另用唯一 owned 实验，结束核清理与生产原资源完整性。
