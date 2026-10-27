# 相同游戏尺寸下的写入、上游压缩与共享 CPU 路径

此前的 51.1 对 45.9 MB/帧并非相同游戏尺寸：当前日常为 2358×1080/0.75，上游基准为 1968×900/0.85。本轮补齐相同游戏输出与 3D 比例的当前版本对照，并直接读取系统分配/合成报告确认上游压缩。没有修改两边 APK、driver、游戏模块或资源，不以历史采样权重代替新窗口。

## 当前官方窗口

使用原 Snapdragon Profiler 2026.9 九项计数器及原官方 Perfetto 8 秒轨迹，严格按完整 Surface 间隔整理。A1→上游→A2 为独立冷启动；当前两次均等待 PSO 186/特化 32 稳定及 tick≥2500，上游采集前后日志未见新建 pipeline/shader。单个上游窗口不能证明所有温度、区域或驱动条件下的长期表现。

| 窗口 | 游戏输出 / 3D 比例 | FPS | GPU 读 MB/间隔 | GPU 写 MB/间隔 |
|---|---|---:|---:|---:|
| 当前 MATCH-A1 | 1968×900 / 0.85 | 42.490 | 103.599 | 44.788 |
| 当前上游 MATCH-UPSTREAM1 | 1968×900 / 0.85 | 43.179 | 106.430 | 45.693 |
| 当前 MATCH-A2 | 1968×900 / 0.85 | 42.495 | 以原 JSON 为准 | 44.730 |

当前两次平均写入 44.759 MB，较上游少 0.934 MB（2.04%）；平均 FPS 42.492，较上游低约 1.59%。目前没有明显的 GPU 总写入劣势；这不是在相同驱动/纹理编码下证明某一个后端优化独立节省 2%。两边主 3D 均为 1673×765，但当前交换链仍为 2358×1080，上游交换链 1968×900；两边 driver、ASTC/BC 资源路径和 Android HUD 也不同。没有为了让数字一致改变用户默认配置。

计数器定义是 `Total number of bytes written by the GPU to memory, per second.`，读计数同样明确为 GPU。它不是包含 CPU 的整机 DRAM 写入，且为全 GPU，包含发生在 GPU 上的 Android UI/合成。不能将 CPU memcpy 的带宽节省要求全部体现在该指标，也不能据它确认整个系统内存带宽瓶颈已解决。

## 上游确实已经使用 UBWC 输出

采集后独立冷启动上游，仅使用已授权 ADB 读取系统 SurfaceFlinger/HWC 报告，不在性能窗口运行 dumpsys。实际游戏 SurfaceView 的四个 imported gralloc 记录均包含：

```
w/h:1968x900, usage: 0x10000900, ... compressed: true
```

HWC 对应同名 SurfaceView 的格式明确为 `RGBA_8888_UBWC`，显示控制器的源裁剪为 1968×900。不是仅根据厂商名称、扩展支持或分配请求位推断实际压缩。记录见 `.local/logs/hdr-mip-continuation-v1/upstream-surface-layout.txt`；这是输出缓冲证据，没有检查其私有中间 HDR 图像。

因此上一轮 Turnip 输出压缩修复是在补齐系统高通驱动已经有的路径，不能宣传为当前实现独有。上一轮同 APK OFF/ON/OFF/ON 的 17.22% 写入节省仍然成立，它回答的是我们原线性输出的额外成本，而非证明对方没有压缩。

## 共享内存的作用与剩余工作

相同窗口中，实际 shared/lazy active，已用 Pool 的 logical import 为 359,661,568 字节（343 MiB）。这是 GPU 实际使用的 Pool 逻辑范围，不是所有注册但尚未使用的池大小，也不把多个导入节点映射的重叠地址相加冒充物理内存。

代码核对：`sync_used_run()` 在 shared 分支执行现有 host phase/cache/coverage 处理后返回，不进入下面的 dirty-generation、脏页遍历、shadow 比较和上传路径。`shared_cache()` 对 HOST_COHERENT 导入直接返回；lazy 路径仍保留物理覆盖证明，不能把共享描述成所有依赖检查都消失。

MATCH-A1 前后，旧 Pool 同步路径的 full/used/protected 计数均未增加；这些计数不依赖额外 pool-copy-accounting 开关。旧 Pool readback 范围/字节、CPU pending/upload wait 计数也为零。上述证据限于这些具体路径，不证明没有任何类型的等待、图像加工、CPU 工作或 Vulkan 复制。共享后读写同一分配仍要满足资源生命周期、真实生产者完成和可见性。

官方 Perfetto 给出的新 CPU 数据：

| 窗口 | 整应用 Running 合计 ms/间隔 | 最忙线程 Running ms/间隔 | 最忙线程运行期间平均 MHz |
|---|---:|---:|---:|
| 当前 MATCH-A1 | 35.403 | 18.865 | 1614 |
| 当前上游 | 38.261 | 18.720 | 1714 |
| 当前 MATCH-A2 | 36.018 | 18.829 | 1651 |

整应用值是多线程、多核累加，不能当成串行帧时间；最忙线程在当前版本包含原生游戏执行和内联 NVN 翻译，不能全部称作翻译时间。上游没有同名 replay marker，表中按实际 Running 选择其最忙线程，不凭函数名假造归因。两边频率不同，Running 接近不能推出指令或 cycles 一样。

当前整体 CPU Running 低于该上游窗口，但没有只切换共享开关、保持同一 APK/driver/原生纹理路径不变的因果对照，不能把约 2～3 ms 全归于共享。当前原生纹理表要求匹配的共享路径，直接关共享会拒绝资源，并不是一个合法性能对照。此前降低 CPU 成本的音频、绑定及其他改动也可能贡献整体差异。

进一步的 CPU 研究应集中于共享后仍存在的命令遍历、资源解析/绑定、覆盖与依赖准备，先用当前宏观 Profile 排预算。不能继续把旧双份 Pool 上传说成当前仍在执行的大头；也不能在 GPU 写入已相近后假设还有同量级的纯 GPU 总写入差距。

## 另一个已验证的通道合并方向

展开已有 API 后确认：两个无 draw 的 HDR mip Pass 也执行了第二颜色附件的 clear，不能删除其 Store。复用仓库已有默认关闭的 `deferred_mip_clears` 路径，保持原四次 clear，只有不可变有界计划证明无消费者、无 query/event/alias 冲突且恢复原目标时，才将 disjoint mip 的物理清除延后，使主 HDR Pass 延续。

当前日常 2358×1080/0.75 下，同 APK 两组官方窗口为：

| 模式 | FPS | GPU 读 MB/间隔 | GPU 写 MB/间隔 | 最忙线程 Running ms/间隔 |
|---|---:|---:|---:|---:|
| 关闭 | 40.141 | 115.041 | 51.257 | 19.783 |
| 开启 | 40.014 | 114.795 | 51.102 | 19.834 |

运行确有 admitted/materialized/continued，未出现 rejected。开启截图未见明显缺画面，但不是全场景验收。另作六帧原 GFXReconstruct API 结构采集，实际 HDR0 延续；与旧关闭结构比较，Pass 数 49→48，逻辑全区 STORE 估算少 15.358 MB、LOAD 估算少 20.478 MB。参考为旧 480ce73，当前为 6f3b131/B3，因此仅以明确的 HDR/mip 区域证明通道延续，不将所有新旧命令差异归于这个开关；新旧 draw 总数还存在 2416/2419 的场景变化。捕获没有 replay，不用捕获时 FPS 或逻辑字节冒充硬件带宽。

当前 prefer_sysmem 的真实硬件写入未出现相应的大幅下降。驱动源码的 PREFER_SYSMEM 早返回支持一种解释：SYSMEM 渲染已直接访问外存，Vulkan pass 边界不必对应一次 GMEM 整图落盘/重载；有反馈、FDM/MSRTSS 等条件仍能强制 GMEM，未取得这个具体 Pass 的 GPU 执行模式快照，故不把解释写成所有 HDR Pass 已实测为 SYSMEM。

当前源码重建的原 Linux Vulkan 语义夹具，异步/同步两组均通过，每组 23,806,545 项检查，包含已有 28 类 mip clear、别名、采样、计算、查询/事件与深度回退；无 validation/sync error。它们只证明夹具覆盖的 NVN 语义，不替代 Android 游戏。额外准入/别名检查的指令成本尚未以 PMU 分开，故保留既有受控候选，继续研究合适的实际 GMEM 场景和净管理成本；没有因为暂时没涨 FPS 删除候选，也没有把逻辑字节节省当作当前已降低 DRAM。主线默认暂不改变。

## 交付状态与证据

本轮未修改主线 APK、profile 或 driver。测试完已核对六个身份锚点、APK、系统尺寸、图层设置和调试属性。再从普通图标无 extras 冷启动，确认仍为 2358×1080/0.75、共享/lazy 与应用内输出压缩开启，mip 候选关闭，原 save2 后停止。上游隔离副本原 HOME 后停止；原厂工具与本次采集器已关闭。Mali 未使用。

全部新窗口、实际状态、原 `.sdp`/数据库/Perfetto、六帧 API、截图、存档和停止收条在 `.local/logs/hdr-mip-continuation-v1/`。`result-summary.json` 是原工具输出的整理，包含 CPU 频率及范围、上游 imported gralloc 的原文和文件哈希。`ordinary-restoration/result.json` 记录日常配置恢复验证。没有新增自制计时器或采样器。
