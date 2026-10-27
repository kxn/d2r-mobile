# CPU/GPU等待与调频：把频率变化纳入因果调查

当前不能继续仅把CPU升降频列成实验干扰。需要检查的闭环是：GPU/呈现工作
改变等待和唤醒节奏，调频器观察到的CPU需求随之改变；后继CPU准备速度又
影响下一次GPU提交。这是待验证的机制，不等于已经确认了vivo的私有策略。

## 已经查实的调频与等待位置

本机Q735历史sysfs记录显示policy0/3/7使用`walt`，不是仅凭Android通用
经验假设`schedutil`。公开Qualcomm WALT参考代码使用上一窗口负载、预测、
top-task、hispeed、RTG/uclamp及上下调限速；不能只用整秒CPU利用率推频率。
这不是本机vivo完整内核源码，私有PowerHAL/hook仍未取得。

标准内核普通dma-fence等待进入睡眠，并不自行设置`in_iowait`。不能假定
所有GPU等待都会获得I/O boost。公开参考CPU调频器与KGSL忙碌度入口尚未
找到“GPU越忙就直接提升应用CPU频率”的通用连接，不据此排除私有联动。

原始本机证据、固定官方内核源码与链接保存在
`.local/logs/cpu-gpu-dvfs-feedback-v1/policy-review/`。

## 现有官方Perfetto逐事件分析

复用v3 OFF1/ON1的同窗官方trace，不增加采样或运行时计时。
PID/TID、完整Surface BOOTTIME窗口沿用原counter receipt。后继Runnable行
提供waker；不把sleep行的waker字段错误地当作本次唤醒来源。

| 指标 | OFF1 | ON1 |
|---|---:|---:|
| GPU驱动waker关联的长睡眠次数，单次≥0.5ms | 792 | 799 |
| 上述睡眠平均长度，ms | 3.658 | 3.370 |
| 上述睡眠完整位于`Vulkan present call` | 100% | 100% |
| 睡眠开始点CPU7频率均值，MHz | 1758.9 | 1747.3 |
| 唤醒点频率均值，MHz | 1777.3 | 1756.5 |
| 唤醒后2ms点频率均值，MHz | 1714.2 | 1698.5 |
| 唤醒点频率低于睡前的次数 | 75 | 63 |
| 唤醒点频率高于睡前的次数 | 111 | 109 |

睡眠采用完整原区间，且唤醒后20ms仍位于正式窗口，避免读取窗口外未知
频率。其余大多数episode前后频率不变。**不是每次等待都立即降频**，
但这个事实也不能排除跨WALT窗口的累计/预测反馈。

主要长睡眠明确位于真实`vkQueuePresentKHR`调用内部；不是提交队列锁
的长竞争。Present可能涉及GPU、Android WSI及合成，waker名称不能继续
细分为哪一条内核等待，也不能把它全算成片元着色器纯执行时间。

只对CPU实际Running与原频率区间求交，进一步得到：

| 阶段运行期加权频率，MHz | OFF1 | ON1 |
|---|---:|---:|
| submission planning | 1521.9 | 1511.5 |
| command replay | 1560.7 | 1556.7 |
| present调用中真正Running部分 | 1749.9 | 1728.3 |

这证明不同阶段得到的频率不同：不能用整线程均值掩盖准备/回放运行较低、
呈现附近较高的阶段分布。它与调频窗口滞后的解释相容，但尚未证明后者。
全窗上下调频事件大多发生在owner Running期间，也不能概括成CPU一睡就降。

SQL、原始CSV、工具命令与窗口在
`.local/logs/cpu-gpu-dvfs-feedback-v1/timeline/`。

## 下一步的判别，而不是忙等保频

1. 先完成双方交换链同尺寸的正式对照，排除额外呈现/合成像素改变等待
   与频率的混杂。游戏/主3D/实际swapchain尺寸均须由真实日志核准。
2. 核查现成ADPF的target与reported work是否同一个周期。当前这些实验
   实际未启用会话，不能说已经证明设备不支持；旧开关无净收益也不能
   证明当前周期合同正确。若只报告submit而target来自整游戏帧，先修正
   或明确实验范围，不能盲开再把变化全归给调度。
3. 对呈现worker只考虑已有实现的单因素诊断，保留所有GPU工作、真实
   semaphore与队列外部同步。把阻塞移至另一个线程可能只移动等待；
   要同时看owner运行频率、下一帧提交间隔、GPU idle与全应用成本。

不增加自旋来伪造CPU负载，不用平均用户态cycles除频率倒算内核时间，
也不把一次升频当成已解决60 FPS。调度反馈是否是主要损失，需要在保持
相同画面与GPU工作量时，通过受控干预验证。
