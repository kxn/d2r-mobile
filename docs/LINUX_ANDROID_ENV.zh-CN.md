# Linux / Android 开发环境

源码基线：D2R Mobile `afad65c895386a96dfb863f2317e41d61222a5a9`。
环境搭建日期：2026-10-15。当前主线是复用原项目 dm/host，完成 Android ARM64 原生执行。
约束见根目录 AGENTS.md，完整计划见 [ANDROID_NATIVE_PORT_PLAN.zh-CN.md](ANDROID_NATIVE_PORT_PLAN.zh-CN.md)。
dm/runtime 的 Unicorn 原型已经停止，其测试结果不计入原生移植进度。

## 使用

```bash
cd /home/dl/d2r-mobile
source scripts/dev/env.sh
bash scripts/dev/check-env.sh
bash scripts/dev/verify-dev.sh
```

模拟器界面：`bash scripts/dev/run-ryujinx.sh`。
在有桌面的终端运行才能看到窗口；没有 DISPLAY/Wayland 时，脚本使用 Xvfb。
命令行调试：

```bash
D2R_NO_GUI=1 bash scripts/dev/run-ryujinx.sh --suspend-on-start /absolute/path/to/program.nro
```

GDB 地址：`127.0.0.1:55571`。一次只允许一个调试客户端。
默认 Vulkan 驱动为 CPU 软件实现 lavapipe，不选择 NVIDIA 计算卡。
需要使用核显时，在启动前设置：

```bash
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/intel_icd.json
```

当前用户已加入 `render` 组；现有桌面登录会话可能需要重新登录才能获得设备权限。
Intel UHD 630 已通过实际 Vulkan compute → buffer 回读校验（512 个结果），验证层无报错；未测游戏帧率。

## 已安装与验证

| 项目 | 安装位置或版本 | 已验证范围 |
|---|---|---|
| Clang / LLVM / LLD、CMake、Ninja、SDL2、Vulkan 开发与验证工具 | Debian 系统包 | 三个原项目纯 C 测试，共 39 项通过 |
| .NET SDK | `.local/dotnet`，10.0.402 | 原项目 `dmsh_xlate` 编译成功，`--source-sha` 可执行 |
| Python 环境 | `.local/venv` | Pillow、numpy、lz4、capstone、unicorn 导入成功；版本在 `.local/python-packages.lock` |
| Android SDK | `.local/android-sdk`，API 36、Build Tools 36.0.0 | SDK 安装完成，ADB 可运行 |
| Android NDK | r28c，28.2.13676358 | 已编译出 Android ARM64 `.so` |
| hactool | `.local/src/hactool/hactool` | 从 SciresM/hactool 源码编译成功；本体 NSZ 解压、更新合并与 ExeFS/RomFS 提取通过 |
| Ryujinx | `.local/emulators/ryujinx/Ryujinx` | 从源码编译成功，自制 NRO 的真实 GDB 通路通过 |

GDB 测试由 `make-gdb-probe.py` 生成不含游戏内容的 ARM64 NRO，使用仓库原有
`dm/verify/oracle/swgdb.py` 检查暂停、寄存器、读写内存、断点、继续和越过断点。
这是单线程小程序测试，不能证明 D2R 多线程确定性；日志中出现过
`Failed to suspend thread ... in time`，正式采样前必须检查所有线程暂停的语义。

测试结果：`.local/logs/gdb-probe-result.json`。
模拟器日志：`.local/logs/ryujinx-probe.log`。
环境检查：`.local/logs/environment-check.log`。

## Ryujinx 来源与可重建性

上游 `https://git.ryujinx.app/projects/Ryubing` 在搭建时无法连接。
使用保留历史的源码镜像：
`https://github.com/Leuconoe/Ryubing`，固定提交
`793ca017ff4f6e48b798a62d2486405bfc42ef39`，master 历史；未采用其 MCP 功能分支。

本地模拟器源码补丁：`scripts/dev/ryujinx-controls.patch`，包含本机 GDB 监听、
脚本输入、HID 共享内存回读、异步截图请求和客户端断开时释放输入。
重新构建：`bash scripts/dev/build-ryujinx.sh`。

该提交需要 Ryujinx.LibHac 0.21.0-alpha.133，但它的上游包源也不可达。
从下列项目的 build-deps Release 获取同版本包，放在 `.local/nuget-feed/`：
`https://github.com/NextendoNetwork/Ryujinx-Nextendo/releases/tag/build-deps`。
已比对 Release API 公布的 SHA-256：
`554bf2315368f193335c0ac60d73d73fb8c318aaf3f8a13bddf4ef30e160b631`。
这是镜像发布者提供的校验值，不是独立的上游签名证明。
其余 NuGet 包从 nuget.org 还原，源映射在 `scripts/dev/nuget.config`。

**这不是 D2R Mobile 作者的私有改版，也尚未证明与其基准一致。**
仓库的 Ryujinx 1.3.2 标注只对应 vendored 着色器库；作者的模拟器提交和补丁并未公开。
现在已有可编译、可调试的 Ryujinx，以及下文列出的自动化工具；
模拟器侧尚缺商业游戏启动、游戏截图成功路径、多线程一致性及固定游戏时间等实测；Android 原生侧已有 SDK 与构造阶段通过的记录。

## 当前资源与缺项

1. 游戏文件已提取到下列路径：三个 ExeFS SHA-256 与项目指定版本一致，18 个必需资源档案齐全。
2. 固件和用于回归的初始存档。仓库根目录现有 `prod.keys` 已加入忽略规则；
   启动器会在缺少 `work/keys/prod.keys` 时链接它，不覆盖现有密钥，也不输出内容。
   该密钥已成功用于这批游戏的提取。
3. Android 真机已连接：PHP110 / MT6983，API 36；原生宿主已执行到 NVN 图形边界。
4. 作者未公开的模拟器改动、起局和录制脚本；需要取得或按公开协议重新实现。

本地位置：

```text
work/keys/prod.keys
work/firmware/
work/data/switch/3.3.94012/Update/ExeFS/{main,sdk,subsdk0}
work/data/switch/3.3.94012/Update/RomFS/data/
```

模拟器配置与存档隔离在 `work/ryujinx/`；启动器将其 `system` 指向 `work/keys/`。
固件需要在该模拟器配置中安装，放进 `work/firmware/` 本身不等于安装完成。
游戏程序、资源、密钥、固件和存档均不提交到 Git。

## 本次游戏解包记录

本体 NSZ、更新 NSP、DLC NSZ 已从用户指定共享复制到 `work/packages/`。
NSZ 使用 venv 中的 NSZ 5.0.0 解压；它对小型 NCZ 的重叠头部计数有错误，
导致 DLC NSP 的 PFS0 文件大小和偏移错误。本地修复与可重复应用脚本：

```bash
source scripts/dev/env.sh
python scripts/dev/patch-nsz.py
python scripts/dev/verify-game.py work/data/switch/3.3.94012/Update \
  --report .local/logs/game-verification.json
```

中间文件保留在 `work/packages/expanded/`、`expanded-fixed/` 和 `work/extract/`，
便于重新核对；没有修改共享里的文件。DLC 两个 NCA 的 SHA-256 前缀与各自内容 ID 一致。
最终更新目录含 46 个 RomFS 文件，总计 8,973,547,258 字节。
DLC 的 RomFS 已另外提取至 `work/data/switch/3.3.94012/DLC/RomFS/`，未并入更新目录。

原有 `dm/tools/extract_game.sh` 只接受同时包含本体和更新的 NSP/XCI，
不能直接处理这次分开的 NSZ + NSP。此次实际流程为：NSZ 解压 → 各 NSP 拆 PFS0 →
读取包内 ticket 的 common title key → hactool 使用 base Program NCA 合并 update Program NCA。
更新类型在此版 hactool 中显示为 `Patch RomFS`，识别不能只搜索 `BKTR`。
临时 title.keys 位于 `work/extract/config/switch/title.keys`（权限 600），
通过工具进程的 `XDG_CONFIG_HOME=work/extract/config` 加载；未更改 HOME。
访问共享用的临时 SMB 凭据文件已删除，密钥内容没有写入源码或报告。

## 后续验证路线

1. 以 Ryujinx 为参照运行器，补齐解包目录加载、脚本输入和截图，接入现有 GDB 状态读取。
   当前源码有基础目录加载接口，但尚未验证它接受项目要求的 ExeFS/RomFS 目录组织。
2. 固定游戏构建 ID、初始存档、随机种子、建局时间和输入顺序；对照侧使用相同的
   游戏修改规则，排除仅适用于宿主的改写。按游戏消息/服务端 tick 采样，不按墙钟猜时序。
3. 输出项目定义的 `ticks.jsonl`、`c2s.jsonl`、`uimsg.jsonl`、截图及最终存档；
   先验证重复录制一致，再与移植版本比较。现有全套验收不能直接运行。
4. Android 已有 ARM64 装载和测试运行器，继续完善系统服务适配，再增加 NVN→Vulkan 后端，复用已有
   SPIR-V 翻译工具。Linux 共用工具和 Vulkan 代码；x86 游戏执行层另行实现。

Eden 下载已经停止，不用它替代 Ryujinx 状态对照。

## 新增自动化验证（无游戏文件可运行）

- `scripts/dev/smoke-oracle.py`：构建原创 ARM64 NRO，在两个独立 Ryujinx 进程中
  各记录 25 个连续 tick。NRO 自行构造公开文档规定的五类单位结构，六个哈希槽均逐拍变化。
  通过原项目的状态提取函数读真实客体内存，比较两次结果；第三个进程注入环形坏链表，
  确认采集失败且清单标记为 incomplete。它不包含或模拟 D2R 游戏逻辑。
- HID 验证：脚本设置按键和左右摇杆，回读游戏消费的共享内存环形队列，随后验证清零。
  超出范围的摇杆输入必须报错；断开客户端会清除覆盖输入。
- `dm/verify/selfcmp.py`：补充缺失的 tick 比较工具。按 tick 对齐，定位各槽首个差异；
  拒绝重复、倒序、漏拍、bad 非零、格式错误和不足 10 个公共 tick。
  默认两边 tick 范围也必须相同，局部比较需显式 `--allow-partial`。
  返回码：0 一致，1 有差异，2 数据无效，3 样本不足。
- `dm/verify/oracle/record.py`：指定函数入口地址后采集 ticks.jsonl 和 manifest.json；
  拒绝非顺序执行的入口首指令、短读、读取失败、坏链表和拍号断档。
  结束时保留暂停状态。**不自动固定游戏种子、时间、存档或命令包。**
- `dm/verify/oracle/control.py`：通过同一 GDB 连接控制输入和截图。
  截图等待下一次呈现的真实帧，处理 RGBA/BGRA 和翻转；启动阶段等待渲染器初始化，
  无新帧则超时报错。已测无帧超时，以及颜色/翻转转换；游戏截图成功路径尚未测。
- `scripts/dev/test_oracle_tools.py`：8 项边界测试，覆盖格式拒绝、部分比较、读取失败、
  截图像素转换及模块表解析。GDB 客户端修正了跨行误识别模块名，并禁用 TCP Nagle 延迟。

示例（先按当前加载程序的实际映射设置 `TICK_ADDRESS`；对应游戏版本的入口偏移为 `Game.nss + 0x5d3bc0`）：

```bash
source scripts/dev/env.sh
python dm/verify/oracle/control.py info
python dm/verify/oracle/control.py press 0x1 --seconds 0.2
python dm/verify/oracle/control.py screenshot work/frame.png --timeout 10
python dm/verify/oracle/record.py --tick-address "$TICK_ADDRESS" --ticks 25 --out work/run-001
python dm/verify/selfcmp.py work/run-001/ticks.jsonl work/run-002/ticks.jsonl
```

一次只能连接一个 GDB 客户端；采集器和独立控制命令不要同时连接。
测试结果保存在 `.local/logs/oracle-result.json`，其中链接各进程日志、tick 和录制清单。
这些采集与输入能力尚未替代作者缺失的 c2s/uimsg 录制、消息驱动起局、固定时间和完整回放脚本。

## Android / Linux 共用原生探针

源码：`tools/device-probe/`。它测试当前平台本机指令执行及 Vulkan 运算，不执行游戏 ARM64 二进制。

```bash
source scripts/dev/env.sh
python scripts/dev/build-device-probe.py
bash scripts/dev/run-device-probe.sh
# 手机已连接并允许 USB 调试后，安装并自动收集报告：
python scripts/dev/test-android-device.py
```

产物：`.local/build/device-probe/d2r-device-probe.apk`，ARM64，Android API 28 起，目标 API 36，
原生库 LOAD 段为 16 KB 对齐；APK 签名和 zip 对齐已检查。
APK 是本地开发签名的调试应用，不申请网络或外部存储权限。
手机界面显示 JSON，ADB 脚本从应用私有目录读取结果。
独立探针已在连接的 PHP110 上运行通过；原项目原生宿主的结果见 dm/host/android/README.zh-CN.md。

原生检查包括：匿名内存 RW→RX 权限切换并执行返回 42 的本机代码、Vulkan 设备/特性查询、
SPIR-V compute pipeline 创建、512 项运算、屏障与 fence、映射内存回读逐项校验。
驱动挂起时 fence 等待上限为 5 秒；超时保留在途资源至进程退出，不销毁仍在使用的对象。
探针只选被指定 ICD 暴露的第一个设备，Linux 启动脚本默认 lavapipe。
集成 GPU 报告的 device-local heap 是共享内存容量，不能作为独立显存或性能指标。

实际结果：
- `.local/logs/linux-device-probe.json`：CPU lavapipe，512/512 通过。
- `.local/logs/intel-device-probe.json`：Intel UHD 630，512/512 通过。
- 两次均启用 Vulkan 验证层，对应 `*-validation.log` 没有报错。

Intel 测试使用独立 ICD，不创建 NVIDIA 设备。上述结果只说明基础执行和驱动通路可用，
不证明 NVN→Vulkan 兼容性、D2R 完整运行或帧率。
