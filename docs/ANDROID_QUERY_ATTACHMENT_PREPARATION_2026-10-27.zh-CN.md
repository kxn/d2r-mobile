# 查询条件引发的重复附件准备：完整路径对照

本轮从整提交预分析的净成本往下找，发现一条可以明确消除的重复流程：**有效的查询结果不等于查询准备会重置 Render Pass**，旧路径却一律放弃已准备好的附件表示，再重做资源/绑定准备。

修正后，在关闭预分析、查询持续有效的同 APK 对照中，每次完整出图的用户态指令约 **119.57M→71.06M（-40.6%）**、用户态 cycles **42.37M→27.48M（-35.2%）**。普通窗口的 owner Running 约 **22.13→20.10～20.21ms**，replay Running **15.38→12.77～12.93ms**。这不是默认营地模式获得 40% 提升：正常预分析已避开大部分该分支，默认模式的实测 FPS 仍约 40.2。

## 一、先在修复后的路径重新测整段规划

此前 NOPLAN 发生在假 GPU 页依赖修复和 prefer_sysmem 策略之前，本轮重新使用原已发布 `5d4b24c` APK、f830/prefer_sysmem、共享 native 资源、原调用者内翻译 CPU7、stream splitting OFF、1968×900 游戏输出/0.85、2358×1080 交换链，做 A/B/B/A。所有窗口显式 blit，其他优化保持原状。

| 普通 Perfetto 窗口 | 整提交预分析 | FPS | owner Running ms/出图间隔 |
| --- | --- | ---: | ---: |
| PLAN-A1 | 开启 | 40.070 | 20.052 |
| NOPLAN-B1 | 关闭，保守回放 | 38.537 | 22.171 |
| NOPLAN-B2 | 关闭，保守回放 | 38.504 | 22.268 |
| PLAN-A2 | 开启 | 40.246 | 19.798 |

PSO/材质变体均在各窗口内稳定；A1 为 180 个 PSO，B1/B2 为 257，A2 为 261，均 variants=32。后继预热会载入此前不同策略形成的 PSO recipes，因此不能说各组冷启动管线总数完全相同，也不能将未增长简单写成工作量逐项完全相同。

另用已跑通的官方 Simpleperf instructions:u + cpu-cycles:u、默认 MONOTONIC，与官方 Perfetto 完整 Surface 边界转换/累计计数差分，取得严格边界上下界：

| 同窗 PMU | 用户态指令 M/出图间隔 | 用户态 cycles M/出图间隔 |
| --- | ---: | ---: |
| PLAN-A1 | 73.491–73.541 | 28.589–28.608 |
| NOPLAN-B1 | 119.016–119.095 | 42.307–42.335 |
| PLAN-A2 | 72.260–72.315 | 28.027–28.051 |

规划扫描确实消失了，但整条路径增加约 46M 指令。官方 leaf 采样也显示 bindings、texture view、Pool 访问和图像依赖准备膨胀，不能只拿消失的 walk 当作已省 CPU 时间。函数附加累计 counter 的打印前缀问题仍按前一报告处理；只用 sampling leader 的 period 估算函数分布，整线程计数使用完整记录边界差分。

## 二、具体重复链与修正的成立条件

`vk_draw_record_impl` 已从活跃 Render Job 借用了附件/View/Framebuffer 等表示，也已做过 Buffer、图像和描述符准备。到查询阶段，旧判断只要 `attachment_job && sample_live` 就跳到 `attachment_cold_retry`。

回退不是一个简单标志：它断开 image split / UBO plan、放弃当前 shader/texture operation，再回到 `attachment_prepare`，重做这一段准备。关闭预分析后保守认为查询结果全部可能有效，于是把大量原本已准备好的 draw 送进此分支。

实际查询实现有两个明确的无操作返回：未开启计数，或者当前跨度查询已经打开。两者都不申请 query slot、不重置 query pool、不结束通道。仅“结果活跃”不能证明必须回退。

源码 `3eec36b40871600ce9a74d06e9c910a26bd116c6` 将回退条件缩小为：

- 显式旧行为参考模式；或
- 有待执行的 mip clear，执行时可能结束通道；或
- 正在计数且没有已打开的查询，prepare 仍可能分配/重置 query pool。

现有 active job、表示有效性、V.cmd/secondary 身份、资源依赖和回退检查保留。没有把查询准备提前到可能跳过/重试的 draw 之前，避免预留却未执行的查询槽；没有改变 report/reset 前缀、提交/fence、NVN 输出、绘制次数或游戏状态。

旧条件保留为冷启动 `--query-attachment-reference`，默认使用修正；`--no-query-attachment-reference` 显式选择修正。JNI 在 native startup 前固定，后端存入 `V.options`，运行中不反复读环境变量。CLI 验证实际后端日志，拒绝热接管改策略。没有新增每 draw 哈希表或缓存管理层。

## 三、GPU 内容与查询语义验收

沿用完整 NVN/Vulkan host fixture，追加已有 300 draw 测试的复用/重做约束，并保留原查询结果与像素检查。五组覆盖同步、异步、旧条件、逐 draw 查询和关闭附件复用，Core/SYNC validation 均未报错。

| 300 draw 样例 | 真实 query 数 | 附件复用命中 | 冷重做 |
| --- | ---: | ---: | ---: |
| 旧条件，跨度查询 | 3 | 0 | 298 |
| 修正，跨度查询，异步 | 3 | 297 | 1 |
| 修正，跨度查询，同步 | 3 | 297 | 1 |
| 修正，逐 draw 查询 | 300 | 0 | 298 |
| 附件复用关闭 | 3 | 0 | 0 |

所有跨度查询的前缀 256 / 65,536、总数 76,800 相同，300 draw 保留；跨提交、RESET/REPORT、中间回退和纹理/深度/模板等原有检查继续执行。这些是独立 GPU 语义测试，不算 Android 游戏原生运行里程碑。

首次 host 配置遗漏原 `DM_GUEST_HEAP_GPU_TESTS` 链接选项，native-resource 引用未解析；随后使用现有选项补齐真实 SVC/stubs 后重建通过。失败构建与因缺少 binary 未启动的 runner 原样保留，没有改源码加假符号或削弱断言。启动工具 26 项检查通过。

## 四、同一新 APK 的 2×2 真机对照

canonical APK SHA256 `32ec523bc6b2284e6aeec295e8faf6b840630ddf631b238e6fd8e64ea6c1d7a5`，宿主 ELF `13f971806a5def24ca72ce73928009e1e1232aaf7b59b223b136d48e55749981`，固定产物在 `.local/build/query-attachment-reuse-v1/`。四组均按第一节相同设置，等待 tick≥2500、八次不同状态观测稳定；窗口内 variants=32、PSO 不增长。采集普通 Perfetto 与 PMU 的窗口分开，不把带 Simpleperf 的窗口 FPS 当普通运行结果。

| 普通窗口 | 预分析 | 查询回退条件 | FPS | owner Running ms/间隔 | replay Running ms/次 | owner MHz |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| OLD-NOPLAN | 关闭 | 旧 | 38.591 | 22.133 | 15.381 | 2075 |
| FIXED-NOPLAN | 关闭 | 修正 | 38.861 | 20.101 | 12.767 | 1514 |
| FIXED-NOPLAN 同 PID 重复 | 关闭 | 修正 | 38.745 | 20.209 | 12.933 | 1491 |
| FIXED-PLAN | 开启 | 修正 | 40.346 | 19.991 | 12.850 | 1524 |
| OLD-PLAN | 开启 | 旧 | 40.148 | 19.977 | 12.769 | 1562 |

普通回放的 queue lock 约 0.8–1.1µs/次；replay 非 Running 约 0.005–0.014ms，没有因修正释放出毫秒级隐藏锁等待。Planning 包含在 replay 内，不能相加。整条 owner 包含原生游戏执行，也不能全部叫纯翻译。

| 同窗 PMU | 用户态指令 M/间隔 | 用户态 cycles M/间隔 | PMU 窗口 FPS |
| --- | ---: | ---: | ---: |
| OLD-NOPLAN | 119.527–119.613 | 42.356–42.393 | 38.561 |
| FIXED-NOPLAN | 71.034–71.090 | 27.462–27.489 | 38.942 |
| FIXED-PLAN | 71.660–71.710 | 27.848–27.872 | 40.173 |
| OLD-PLAN | 73.060–73.106 | 28.447–28.463 | 40.150 |

记录均无 lost samples/records；`security.perf_harden` 每段从 1 开始、由现成工具编排临时打开计数并恢复 1。使用已验证的 ReportLib 累计计数边界与官方 Perfetto 时钟转换，没有新计时器/采样器。源码和 exact ELF 匹配；所有原数据、脚本、SQL 和失败记录保留。

解释应分开：

1. 旧 NOPLAN 的大幅指令/周期膨胀确实有可以删除的重复准备；修正有明确净工作量收益，默认保留。
2. 频率随工作量下降，从约 2.08GHz 降到 1.49–1.51GHz，因此 35% 的 cycles 节省只体现为约 2ms owner Running 差，并没有同等比例涨帧。没有据此宣称已经降低温度或功耗。
3. 修正后无预分析与有预分析的整线程指令、cycles 已接近，但有预分析仍较快。它减少查询/附件等 GPU 工作，也支持状态段等 CPU 路径；**1.45ms 扫描不能当作可免费削掉的整帧时间**。默认保留预分析。
4. 正常营地已通过查询活性证明避开大部分旧回退，不能将 NOPLAN 对照的 40% 当成默认营地收益。正常两组未取得明显涨帧，指令差也有独立冷窗口和场景变化边界；不把它写成稳定 2% 全场景提升。

## 五、普通图标启动发布

仅升级宿主 APK，使用原 stopped-file SHA CAS 更新与其匹配的 profile/receipt；保留原资源、存档、显示、比例和散热偏好。新默认使用修正的查询回退条件，预分析仍开启；未启用前一轮尚未完成恢复资格的 native-buffer 候选。

新 profile SHA256 `b9445b1903655a28a6c7e5a5467ed03881e34691187416274e924a279666c7fd`，receipt `57e0f460ed7e90a1c40b063d0755a83de2e4500e0104e08cc234329937dda751`。实际 APK 与资源/配置锚点分别核对，先前完整资源验证作为未更换 payload 的历史资格引用，没有冒充这次重新处理过全部游戏文件。

两次真正 MAIN/LAUNCHER、没有测试 extras 的冷启动（PID 32765、1372）均进入原生真实营地；实际 shared/lazy/字节策略、inline CPU7、stream OFF、prefer_sysmem、query conservative_retry=0、planning enabled、native buffer geometry=0。普通显示仍是用户原 **2358×1080 输出、0.75 3D 比例**，与上述 1968×900/0.85 基准不同，不能搬用其 FPS/带宽。每次原生命周期 commits=2 后停止。截图没有明显缺画面；不据短营地验收宣称全场景或长时间温度已通过。

原始证据分别在 `.local/logs/planning-net-cost-v2/` 和 `.local/logs/query-attachment-reuse-v1/`。发布收条为后者 `publication/result.json` 的 committed=true；它取代当前安装的 5d4b24c 身份，旧发布/恢复记录保持历史原样。没有 CPU 模拟、游戏流程定制或叠改加工 binary。
