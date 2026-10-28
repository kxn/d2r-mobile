# Store 附件域过滤：真实事件减少，整线程暂未净减负

2026-10-28。**同新 APK 的开启/关闭对照，Store 索引事件每次 guest 提交
减少约 62.05%，但 owner 用户态指令增加约 0.84%–0.97%，cycles 也略增。
当前不能称已省 CPU 或涨帧。** 候选保留为默认关闭的实验，继续拆新增
管理与其他工作变化；没有因为即时 FPS 不升而删除机制。

## 改造和正确性

Store 查询只消费已经成为物理 pass 逻辑附件的目标。候选维护初始
`S.targets` 与已经遍历的 `SET_RT` 的单调累计域；能证明与这个域无 serial/
内存别名的 Shader 读取，不再追加 Store 的 READ 事件。以后增加附件时，
此前的负证明会检查新增目标；已证明相关的资源在本次构建内保持相关。
未知描述符继续产生全局需求，描述符 epoch 变化仍使整个派生索引回退。

没有第二次完整命令预扫，复用原 descriptor/bucket footprint。但目前
仍逐次解析 Shader 读取槽和访问 descriptor 缓存；**只省事件产物，不是
省掉全部约 2.46M 指令的前向读取准备。** 新增约 24KiB 静态元数据，
开启时每次构建清理 16KiB，另有每次访问的证明判断和范围比较。

生产规划器的主机差分测试覆盖 11 场景/762 个合法 Store 判断，并通过
ASan/UBSan；三个故意漏掉失效规则的实现被真实判断差异拒绝。随后在
真实 ARM64 游戏中分别运行 OFF-CHECK1 与 ON-CHECK1：原 Store 参考扫描
核对次数分别 21,543→48,243、18,254→119,389，均零不匹配。开启候选的
实际跳过路径被观测，画面人工检查未见明显新增黑块/几何错误。它不是
全游戏场景保证。

功能检查独立于性能窗口，使用原 `DM_VULKAN_STORE_CHECK` 算法，不开
Vulkan validation 或自定义计时。正式 normal/PMU 冷启动均核准该功能
关闭，比较和不匹配计数始终为零。

## 实际固定条件

- 同 Q735/Adreno735、原营地角色与位置，game/swapchain 1968×900、
  3D 1673×765 / 0.85；来自两端真实创建日志及状态。
- 同普通 APK `b53a5f3e754949d4ff089c7f667b2a90bed0258e02c9f1bde9b70e43f821789e`，
  host `c2f136bce06e2913ed2c188a229919aaf0c4d9028de580ebe54dab4c34a830b9`，
  源 `f2d7712`。1056 个构建输入和全部 APK 原生库逐项核准。
- cf83 Shader、12b8 Turnip、原 BC3 native 资源、table6890/producer919，
  CLEAR ON、inline CPU7、30µs、fixed+extra ON，sparse/stream/hints OFF。
  两臂唯一配置差异为固定启动的 Store attachment domain 布尔值。
- 没有复采官库/SDP9，不用旧 c09 当本轮同 binary 的 OFF 臂。

## 普通运行与整轮变化

OFF-N1→ON-N1，均原健康门通过的20秒完整 Surface 窗口：

| 指标 / Surface，频率除外 | OFF | ON |
| --- | ---: | ---: |
| FPS | 50.4511 | 50.0497 |
| owner Running ms | 16.41584 | 16.45264 |
| owner 运行期 MHz | 2063.75 | 2056.81 |
| 全应用 Running 线程和 ms | 31.51284 | 31.47287 |
| owner S 睡眠 ms | 3.09063 | 3.26360 |

FPS −0.80%，没有大幅 CPU 频率或向其他线程迁移的证据。官方调度/阶段
查询中，Store finalization Running −0.01616ms；planning 排除该嵌套区间
后 +0.02636ms；replay 再排除 planning 后 +0.15651ms；其余 owner
−0.13287ms，严格闭合。不能将这些包含关系直接相加。

Present 调用内睡眠增加约 0.18217ms，与帧间隔增加约 0.15899ms 同量级。
这定位了本对照的更长等待范围，不证明 GPU 硬件本身变慢或给出相位因果。
本轮没有实际 GPU 频率/DRAM 计数，保持未知。

工作快照独立按 OFF1085/ON1031 次 guest 提交归一，不能换成 Surface：
Store 事件约 15,174→5,758.89（−62.05%）；descriptor 访问约
12,023→12,026.26，Shader 槽阶段约 4,852→4,853.52，基本保留。
ON 新增约 12,026 次过滤、5,682 次范围比较、542 次负证明复审/guest。
约 78.46% 的 considered 访问被跳过，包含旧路径本来会同序号去重的访问，
**不是净事件减少 78.46%。** 正常窗另有纹理准备约 +2.24%、miss +7.30%，
作为其他工作变化保留，未直接归因于过滤。

## 官方 PMU 净工作

OFF-P1→ON-P1，使用原 Simpleperf instructions:u、附加 cpu-cycles:u，
原32秒记录覆盖官方 Perfetto20秒裁窗；四个边缘与原时钟转换/归约不变。

| owner 用户态事件 / Surface | OFF | ON |
| --- | ---: | ---: |
| 指令 M | 71.12868–71.17024 | 71.76882–71.81832 |
| cycles M | 30.55152–30.56679 | 30.77606–30.79298 |
| owner Running ms | 16.43035 | 16.52478 |
| owner 运行期 MHz | 2066.20 | 2073.80 |
| 全应用 Running 和 ms | 31.0321 | 31.7950 |

整线程指令 +0.59858–0.68963M，周期 +0.20926–0.24146M；区间是
采样边缘计数界，不是统计置信区间。两原工具/健康门通过、丢样/截断为0，
真实 owner/PID/ELF 验证通过。`:u` 不包含内核或其他线程指令。

两端 present/Surface 约 0.997006/0.997003，未见 ON 每上屏帧多执行
完整 guest 周期的证据，边缘仍有未完整 replay。全应用其他线程时间也有
增加；PMU 窗的纹理准备/miss 却下降，与正常窗不同。故不能把全部额外
0.64M 指令直接按功能名归给过滤器，需要同一批样本的高层祖先归因。
PMU 工具 FPS 不替代上面的普通运行结果。

同批实际样本的初步互斥归因已进一步修正判断：planning 约
6.056→5.705M I/Surface，局部约少 0.35M；其中新增 domain 判断约
+0.412M，而事件/资源解释约 −1.038M，walk 与 finalization 等又增加
部分成本。整线程另有 state/attachment/hazard、program/PSO 和游戏代码
归属增加，也有资源准备及 driver replay 减少。各小组仅几十个样本，
保留采样误差和一个栈/时序范围分歧；不能说已确定全部新增的因果，也
不能再说管理把 Store 的省工完全抵消。原始矩阵在 `pmu-attribution-agent/`。

## 下一步及设备状态

保留 v1，按已有 PMU 分解 domain 判断、walk/event/finalization、资源准备
及其余 owner 的净变化。已将独立工作树验证的负证明提前返回 v2 应用到
主线实验代码，避免重复读取 footprint 和回写相同 proof；15 个主机场景/
840 个判断及四个错误实现负控通过，尚未在真机启用 v2。
不把更短机器码冒称已经净减负，也不把源头事件减少冒称整帧收益。

本次入口的22个用户文件已原位恢复，偏好和接受的桌面锚点全部一致；
已重新安装精确 c09 普通版本，恢复用户文件后未冷启动。游戏和本轮拥有
的官方工具均停止，原 perf_harden=0 保持。Mali/平板未使用。

全部实际收条/原始工具数据在 `.local/logs/store-attachment-domain-device-v1/`：
`functional-qualification.json`、四个 normal/PMU 臂、
`normal-whole-analysis-agent/`、`pmu-whole-comparison/`、`work-count-agent/`、
`final-restoration.json`。安装曾因 vivo 本次应用确认页等待而客户端超时，
原失败保留；按授权确认后安装 SHA 核准，没有据超时重复安装或更改全局
安全设置。候选 APK/源资格在 `.local/build/store-attachment-domain-candidate-v2/`。
