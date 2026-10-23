# Compute 绑定录制状态去重候选

父版本 exact `52181dc6b7cbdb7df7168d62201a84dd27d05672`。只去掉同一次有效录制中与最后实际命令完全相同的 compute pipeline/descriptor bind；不跳 dispatch、push constants、CPU/GPU 发布、资源解析、别名/布局、依赖、写回或寿命消费。它是 Vulkan 命令状态的表示，不是资源 ready 缓存。

## 完整入口与边界

原 guest dispatch、query publication、event ADD、bright、image retile 五处共同调用 core 的 `vk_compute_bind_pipeline` / `vk_compute_bind_descriptors`。两函数分开，保持 guest 原 pipeline→push-bias→descriptor 顺序。graphics 绑定点独立，现有 graphics cache 不变。

状态保存真实 command、pipeline、layout、firstSet、完整最后一次 set 向量及 dynamic offsets。只比较最后一次完整调用；不合并多个 set 更新、不推断 layout 兼容性。支持 8 sets/16 offsets，超过范围原样调用 Vulkan 并使 descriptor proof 无效，不截断、不拒绝合法输入、不分配。layout/command/pipeline 的 NULL 是无效标记，不把无效 Vulkan 调用伪装成命中。

六个原 begin 全部核对：primary、graphics secondary、PL second secondary 使用薄 `vk_command_begin`，成功后失效，**不是只看相同 handle**。另外三个原 begin（独立 upload cmd、startup copy probe、overlap transfer probe）不使用 V.cmd compute helpers，因此保留原 API，不写录制线程状态。三个原 execute-secondary 点（普通pass一个、PL两个）全用薄 `vk_command_execute`，执行后失效 primary 状态。completion worker 原 ResetCommandPool 不写此全局录制状态；下一次真实 begin 必须重新建立 proof。新 secondary 即使最终被取消，也仅造成保守 miss。

原 descriptor set 在一个录制中不可变，由原 submission context 保活；retained handle 只在实际 fence+CPU completion 后可重用，下一 begin 使相同 handle 的旧 proof 无效。guest 与所有内部 compute 记录者用同一状态，故内部 helper 绑定必然覆盖 guest proof；不增加引用/释放，不改变 layout/pipeline 的原长期所有权。

## 改前预算与成本

现有官方调用栈中 guest dispatch 是小职责；driver 全录制约10–13%包含 graphics 和其他必要命令，不能全部算这里的收益。没有已测同一录制内 pipeline/set 的重复比例，也没有本候选的 Android 时间或指令预算，净时间下界为0。

可证明收益是每次 exact hit 分别少一次真实 vkCmdBindPipeline 或 vkCmdBindDescriptorSets；两者可独立命中。内部 query/event/retile 经常分配新 set，descriptor bind 本来仍必要，可能只省相同 pipeline；bright 或其他内部 pipeline 穿插后 guest 必须重绑。此候选不改变长寿命资源表或 descriptor 材料化成本。

新增 LP64 host 实符号 BSS 168B（整进程一份，不是每context一份），无分配/锁/原子/计时器/生产统计。常见当前单set/0offset消费必经两个宿主helper入口、command/handle/标量比较和一个8B set比较；命中以helper替代driver入口，miss 则在原 driver 调用之上增加helper调用并更新最后表示。begin/execute 仅失效3个句柄（24B），不清整个向量，不增加每draw大结构复制。实际 CPU/FPS/发热收益仍须统一 Android 候选测试。

## 功能资格与失败记录

!APP 新夹具通过原 NVN dispatch→重复dispatch→真实 event ADD→dispatch，观察真实 Vulkan API，重复项必须不再 bind，内部 ADD 与返回 guest 各必须重绑；原 GPU 数值断言保留。另使用原 compute SPIR-V、真实动态 SSBO 布局及 GPU 结果验证 set向量、firstSet/count、dynamic offset、layout/pipeline变化、command切换、同 handle reset/rebegin、secondary实际GPU写入及execute后primary重绑。normal/ASan×serial/async四例；另原 query 数学1536 cases和shared-image copy/retile内容两例，共六例，Core/SYNC/LVP2。

首轮 direct读回缓冲缺 TRANSFER_SRC 的 Core VUID失败、一次原shared fixture缺lazy准入的配置失败均保留；修复仅为 fixture使用正确usage和复用原已成功的夹具模式，未改生产资源规则。APP排除最初裸字串命中源码根目录名，改为精确JSON test字串/符号核查，未擦原PP。所有GPU测试经公共 flock 串行。未执行游戏模块、Android构建、RAW或设备操作。

参考原作者提交85133c的compute状态思路：
https://raw.githubusercontent.com/d2r-mobile/d2r-mobile/85133c27f97f81f9ca5ba333975f1c1178675a2f/dm/android/native/nvn_vulkan_compute_state.inc
本实现按现有架构独立适配：不是固定set0/单set，所有内部compute和真实录制/secondary边界都参与；不引入256offset清零或仅guest缓存。
