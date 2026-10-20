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
