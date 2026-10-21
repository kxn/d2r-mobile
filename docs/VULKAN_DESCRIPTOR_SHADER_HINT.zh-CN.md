# 描述符 history 槽位提示

候选基于 `80a71bc`，与 private image budget 分支独立。来源为现有 Q735
官方 Simpleperf 99 Hz 样本及精确 ELF 的 NDK 反汇编，不增加运行计时或采样。
80a 的 NvN 630 个样本中，bindings self 为 32、inclusive 为 154；其中
7 个样本的 PC 是 history 槽/header/epoch 读取，不能称为可删除的 key 拷贝。
原 before/after-record ADB 括号记录 history 命中 232907 次、miss 99871 次；
该括号大于工具窗口，不作为精确每帧或每秒分母。

原每阶段 `descriptor_last` 只保存一个最近内容，A/B/A 切换会回到完整
history hash。新增每 submission 16 个 `(shader, layout, history_slot)`
提示，每项 24 B，共 384 B。提示只保存位置，不保存 descriptor payload、
CPU 内容或旧 entry 指针。原 stage-last 命中路径不查询或维护提示。

stage-last miss 后读取当前 history 槽，要求 epoch、shader、layout 和刚刚
重新解析的全部 values 完全一致，才使用当前 set。失效、槽替换、碰撞均回到
原 hash 路径。命中仍更新原 stage-last 并复制原 key，以免 A/B/A 后连续 A
失去原便宜的 single-last 命中；本最小版只省 hash，不能宣称省这次 key 拷贝。

每 draw 的资源解析、CPU 上传、native use、别名与 hazard、storage bias、
动态 Uniform offset 生成全部保留。回收时清空提示，原 history epoch 仍校验；
reference 开关仍走原路径。miss 新增一次有界 O(1) 提示读取和相应比较，
命中率及新增成本要由候选对照核查，不把父函数样本占比当净节省。

验证沿原 NVN command recording、submit 与 GPU ReadTexels：shader A/B/A、
同地址 CPU 写、static/narrow/extended offset、强制槽碰撞、descriptor
template/reuse/reference 切换、上下文回收与逐像素结果。owned metadata
测试另核当前槽 realloc/替换、epoch、hint 碰撞及真实 recycle 清空；这些
测试分支不编入正式运行时。它们不代替 Android 原游戏、FPS 或热验收。

## 2026-10-21：真机结果与合入边界

最终候选 `d5ecbcd` 还要求当前 history entry 的 capacity 足够容纳本次
重新构造的 key，再做完整值比较；提示不能绕过资源依赖、当前值检查或
上下文 epoch。新增成本仍为每 submission context 384 B，stage-last 命中
路径不维护提示。已合入 `36bb836`，与 guest-entry 私有图像预算共存。

Q735 实际运行本候选重新构建的原游戏模块，完成三段原输入行走、官方
Simpleperf/Perfetto 采集、原生命周期 save2、停止与完整恢复。模式为
f830/native6217/sharedON、fixed .60、2750×1260、1 MiB 窗口，不是默认
.75 dynamic 或持续野外测试。两张截图没有新的明显黑块或几何缺失，
已有绿色地面散点仍在，不声明全场景等价。

与既有 80a 工具窗口相比，bindings self 为 32/630 → 32/624 个用户态
样本；独立 8 秒 stat 的 NvN task-clock 为 6599.88 → 6704.08 ms，
用户态 cycles 为 10.081 → 10.090 G。Perfetto 的 replay Running 为
26.386 → 26.261 ms/完整 Surface 间隔，期间加权频率和实际工作量也不同；
Surface 间隔均值 33.2279 → 33.2849 ms。因此没有明确的净 CPU 或整帧
收益证据。新提示 PC 没有落到样本不表示零命中，旧 history 总命中也不是
新提示命中率；端点 PSS 少 35.11 MiB 不能归因于这个 384 B/context 的缓存。

保留理由是：当前值完全相等且提示有效时可以省去原 hash，容量、epoch 和
完整值检查保证复用条件；新增成本有界，短测没有发现明显资源或稳定性退化。
这不是已经解决主要 CPU 热点的结论。合并后的 host Vulkan 验证另覆盖
异步/串行 descriptor 用例各 94,412 项与 image-budget 用例各 1,590 项，
Core/SYNC 无报告；该组合 host 结果不能冒充合并版 Android 真机验收。

实际凭据：`.local/logs/native-candidate-short-d5-Q735-v1-root-actual-review.json`
（SHA256 `b6c555eddd034fc25ffb71e69da4b5aec1d3287a21a7c3b13d2f92040226db8e`）。
合并版验证：`.local/logs/main-budget-hint-v1-host-validation/result.json`。
