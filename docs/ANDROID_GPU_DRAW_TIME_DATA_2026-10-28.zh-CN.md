# 绘制时数据资格：不能用初始快照给 Shader 循环排优先级

本轮没有新的真机提速结论。已完成一项会改变优化优先级的核查：HDR 的列表计数在绘制前由 Compute 重写，初始快照不能代表实际读取；使用现成 GFXReconstruct 回放工具取得写入后及 draw 前内容后，这份历史帧的候选四项索引合并仍没有覆盖。原型保留，但不为这一帧预支收益或安排整轮真机 A/B。

## 先找实际 producer，而非直接数初始内存

原厂 SDP 的 frame 711，目标 Fragment Shader module17311 对应 `5c1ca4e7c3e4657c`。历史输出为 2750×1260，不是当前正式对照的 1968×900 输出、1673×765 主3D；旧帧只能提供结构与数据参考。

初始 ByteBuffers 的 `sequenceID=0xffffffff` 数据中，两段列表头的低6bit和后10bit计数全部为零。但原提交明确先执行 CS module126/pipeline2303：它通过 set2 binding4 写入 buffer2240 的 `[19314880,19320016)`，覆盖两段头部；binding5 写索引列表。原 SPIR-V 存在10处头部 `OpStore`，包括清零和读改写，并非仅声明可写。其后有 Compute 写到 Fragment 读的屏障。

核对实际提交展开的11个 Compute、15个复制/填充等操作以及71个 graphics module，未找到其他头部写入来源；该 Fragment module 的321个实际 draw 都在 producer 之后，读取 offset19317440 的那一半。这里的 draw 数不代表 Fragment invocation、循环次数或 GPU 时间。

完整与 stripped 捕获的 block index 不相同。下面仅列完整 GFXR：

| 操作 | 完整捕获 block index |
|---|---:|
| primary command buffer begin | 138328 |
| 头部 producer dispatch，4×5×1 groups | 138349 |
| secondary command buffer begin | 138380 |
| 目标首个 HDR draw | 138566 |
| Render Pass begin / end | 141289 / 141291 |
| execute secondary | 141290 |
| queue submit | 141438 |

不把 JSON 中的 `[Binary data]` 当成已知 Push Constant，也不另写 Shader 模拟器推断最终内存。

## 现成工具导出写入后的数据

对原 `sdpframe_002.gfxrz` 仅做标准 zlib 解压，未改捕获命令。使用官方 `gfxrecon-convert` 和 `gfxrecon-replay --dump-resources`，由官方回放工具完成原 Compute 和绘制，导出 producer 前/后 Buffer 与目标 draw 前 descriptor 内容。主机显式限制为 Lavapipe，未使用承担计算工作的主机硬件 GPU；这不是 Android 游戏原生执行或性能测试。

工具遇到的限制完整保留：

- 原设备 feature 集合在 Lavapipe 不支持，直接回放创建 device 失败。核对本帧无 ASTC/ETC、depthBounds、sparse bind、继承 query 的实际使用后，使用工具原有 `--remove-unsupported`。不能将功能调整后的回放冒充相同设备。
- `--memory-translation remap` 无有效 memory type 对应；采用原有 `rebind`。producer 的三个 Buffer 已核对无跨 VkBuffer 分配重叠，也没有图像绑定同 allocation；同一 buffer2240 内所有 descriptor/view 的相对偏移保留，包括头部/列表声明尾部16B重叠。这个检查不保证整帧所有资源都跨驱动等价。
- 目标命令已完成导出，随后在 block142343 拒绝 QCOM 私有压缩尾块，最终返回255。没有将其报告为完整回放成功。
- 需同时开启 `DumpAllDescriptors` 才有 descriptor 数据；仅给 Draw 的 descriptor 列表但关闭该选项时，本版工具只导出了附件。
- Compute mutable-buffer 导出 JSON 的 offset 为0，不能拿它代替原 API offset。已按原绑定、导出长度与前后字节切片交叉核对实际范围。

producer 导出的5136B在执行前与两段初始快照逐字匹配；执行后第二段2576B又与首个 HDR draw 的 binding4 导出逐字匹配。第二段第158个 word 从 `0x01000000` 改为0，其余相同，证明 producer 确实会改变头部。

| draw前第二段字段 | 原始 descriptor 范围内 word 分布 |
|---|---|
| bit0..5 | 0：644 |
| bit6..15，候选四项合并目标 | 0：644 |
| bit16..23 | 0：516，1：128 |
| bit24..31 | 0：289，1：355 |

第一段 producer 输出的四个字段最大值同样为0、0、1、1。统计包含 descriptor padding，不是644个有效 tile，更不是按实际像素加权的执行次数。该历史回放没有为“完整四个列表索引合为一次读取”提供覆盖证据；不能外推到当前版本、其他地点或战斗。

## 对优化工作顺序的影响

候选已完成局部 SPIR-V/边界证明，但没有当前真机收益，保持为离线实验。先看实际会执行的 VS/CS/FS 读取链和 GPU 内部停顿，不能因为一个大循环出现在热点 Shader 中，就认定循环本身很热。主线继续以原厂工具区分吞吐、依赖读取、缓存和同步；CPU 只检查导致提交延迟的锁与等待。

证据位于 `.local/logs/counted-list-existing-data-v1/`：`producer-audit.json`、`producer-memory-qualification.json`、`full-draw-index-bridge.json`、`replay-data-evidence.json`，以及5次原工具日志、原导出 JSON/Buffer。完整 GFXR SHA-256 为 `0da268a81b64dca33359ce2244f5729c018a04c8593fad40e9de9830ddc99ed6`；draw前第二段 SHA-256 为 `adb0fb5d580f2994268d5cbba39ade26bb6847f90ee34cb0ccd5799059eab972`。原始、失败记录和本地原型均保留。
