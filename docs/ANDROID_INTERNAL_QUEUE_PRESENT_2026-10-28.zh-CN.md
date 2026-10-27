# 同队列呈现等待：标准内部同步队列实验

状态：实现和规范APK构建完成，原0e53真机扩展资格被拒，未安装候选游戏包；
仅开放该标准扩展的独立驱动正在构建，整轮收益尚待验证。保持默认关闭，
不将源码支持或一次能力查询当作实际创建成功，不将少等CPU等同于涨帧。

## 问题与依据

本机Q735真实Perfetto及实际系统libgui反汇编已经确认，Android
`BufferQueueProducer::queueBuffer`在提交当前buffer后，等待前一个queued
buffer的GPU生产fence。该等待落在`vkQueuePresentKHR`调用内，当前约
3.526ms/完整Surface为S睡眠。同队列呈现worker曾把等待搬成主owner的
`queue_mu`等待，没有显著净收益；依据与界限见
[本轮完整结果](ANDROID_SURFACE_AND_WAIT_RESULTS_2026-10-28.zh-CN.md)。

Khronos的[KHR内部同步队列提案](https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_internally_synchronized_queues.html)
明确以`vkQueuePresentKHR`可能有长时间但无需全程互斥的工作为动机。
正式[vkQueuePresentKHR宿主同步合同](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)
允许以`VK_DEVICE_QUEUE_CREATE_INTERNALLY_SYNCHRONIZED_BIT_KHR`创建的
队列无需应用外部整程互斥。它没有删除GPU semaphore/fence依赖，也没有
关闭系统节流，更不需要伪装NativeWindow连接API或修改系统库。

冻结0e53 Mesa26.2.5源码`tu_device.cc`广告该extension且feature为true；
runtime `vk_queue_submit`在实际driver_submit范围按queue flag加内部锁。
Android release-image桥由单一呈现worker调用，导出真实SYNC_FD；后续
系统BufferQueue CPU等待已在driver前缀之外。此处仍须用真实设备的
extension、feature、flagged queue创建与最终画面逐层验证，不能以源码
替代资格。

## 实现范围

- `nvn_vk_queue_sync.h`只补旧SDK缺少的公开KHR revision1声明，注册号505、
  feature sType1000504000、queue bit4；不是私有驱动扩展。
- 新cold mode3要求真实Android surface、async、同队列WSI worker、实际
  extension和feature。任一不满足显式失败，绝不无合同地去掉外锁。
- 显式将feature加入device pNext链，并以flag4创建唯一队列，使用匹配
  flags的`vkGetDeviceQueue2`取回非空句柄。其余feature链仍保留。
- 只对该已协商队列，呈现worker不再整程持`queue_mu`。主渲染owner的
  外锁、资源锁、命令池及GPU完成worker语义不变；Acquire、Present、
  timing反馈仍在同一WSI线程顺序执行，重建/销毁先drain该线程。
- 保留原逐交换链图像binary semaphore、真实生产fence、NVN ticket和
  guest/internal提交顺序。没有丢帧、降尺寸、关clear或改shader。
- 新选项`--present-worker-internal-queue`要求cold、`--async-submit`、
  `--dedicated-present-worker --present-worker-shared-queue`。普通模式及
  日常profile默认不选择新mode。Java/JNI签名一同修改，不混旧DEX。

独立源码审查覆盖feature链、flags匹配、swapchain排空和逐image semaphore
复用。一个明确的潜在新增成本仍保留：若呈现worker的release-image前缀
开始晚于下一次渲染submit，KGSL导出的fence可能保守纳入后一个submit，
造成过同步；这不应被隐藏成收益。必须检查实际时间线及吞吐。

## 构建与验证身份

源实现`d100c3ca7daa3be381b9acd36d0ba1d5051815e5`。
正常规范构建入口：

```sh
python3 scripts/dev/build-android-apk.py --tlsdesc --shared-pools \
  --shared-import-windows --native-resources --native-jobs 4
```

冻结目录`.local/build/internal-queue-present-v1/`：

| 产物 | SHA256 |
|---|---|
| canonical.apk | 7dcdde423c078485bbe406337959602634f22f816d2f085d51172d341c2acc09 |
| canonical.apk.build.json | 842a9e65a2415a12466a4b9ee046ce1c626a2d90607f7eb8f73b676824b40632 |
| libdmhost_android.so | 3aed8a6dae0f369209157987d1ae96a33543e2a19ce7d92fb486a887f36a177e |

全部1053项源码输入与嵌入六库SHA在冻结时核对通过；正常APK无wrapper，
未改原始游戏模块或资源。32项现有启动工具检查通过，新增覆盖mode选择
与attach/缺前置选项拒绝；这些不是实际GPU并发正确性验收。

此正常APK还包含之前已独立Bionic验证的stderr原子重定向修复，且不把
fixture carrier编译当成普通发布。因此性能因果只能比较新同APK的A/B，
不能把旧992与新候选差直接归给内部队列。

## 必需真机实验与验收

1. 独立普通UID冷query核实际0e53 driver maps、extension和feature；不改
   游戏包、用户存档或GPU状态。它只解决支持性问题，不提供性能结论。
2. 同一新APK：A为普通inline；B只增加上述内部队列worker选项。保留原
   BC3-linear资源、0e53 driver、共享、inline CPU7、shortwait30us、固定
   录制去重、shader链、单提交策略、PSO稳定要求及原营地clone。
3. 每臂实际game/swap1968×900、main1673×765、.85；B必须实证日志
   `internal_requested=1 extension=1 feature=1 queue_flags=4`及非空队列、
   单队列worker，且进入真实游戏、无明显黑块/缺面/撕裂。不能只核Intent。
4. 官方同窗SDP九指标与Perfetto gfx/hal、scheduler/frequency/Surface，
   检查整帧、owner及present worker Running/S/R、全应用、频率、GPU读写/
   停顿/忙闲、实际Surface显示和原guest-submit数量。原BQP睡眠可能保留在
   worker；关键是owner下一submit是否仍等待、GPU是否得到有效重叠，
   还是背压仅迁移到Acquire/worker ring/其他依赖。
5. 沿原Android生命周期fresh save2后停止，不让手机空跑；原用户save及
   原资源目录由既有事务保留，工具与包身份完整记录。

## 首次真实冷查询：被Android扩展白名单阻断

独立包`org.kxn.scahb`的冻结APK
`afc93e06a9262e3268523eba50640a7bcfaaee24df9a4b18e61084717beb313b`
以普通UID10331实际加载0e53，driver UUID与device UUID均匹配原记录。
真实广告`VK_KHR_internally_synchronized_queues=0`、feature=0。按前置合同
停止，未尝试flag4创建，更没有无合同解锁；7dcd候选游戏APK尚未安装。
本次不创建logical device、queue、AHB或hint session。实际包已清理，
原游戏992仍停止，官方SDP暂停前后记录数/最后时间戳相同。

根因在驱动自己的Android兼容白名单，不能归罪系统loader：生成的
`vk_extensions.c`内`vk_android_allowed_device_extensions`把该项设为false；
`vk_physical_device.c`据此过滤广告，`vk_device.c`也拒绝创建时启用。
真实hardware/API为1.4，`tu_is_vk_1_1`并非这次拒绝来源。

因此下一驱动候选只给`vk_extensions.py`的此项白名单开放，不改Turnip
实际锁实现、image layout/profile盐、共享契约或WSI。原build的
platform-sdk-version为34；不会为此全局升36并额外解锁其他扩展。该KHR
feature只需已有Vulkan1.1的GetDeviceQueue2，没有新增Android函数ABI。
候选仍仅在已核Android16机型实验，须对照生成表确认只多此一项，并逐层
核准实际UUID、扩展广告、flagged creation和正确出图。源码已实现不等于
当前0e53实际可用，原失败收条保留。

同一只读冷查询实测基础ADPF SESSIONS=1，但SURFACE_BINDING、
GRAPHICS_PIPELINE、AUTO_CPU、AUTO_GPU均为0。之前普通hint成功不能推出
自动pipeline计时可用；本次没有创建或改变session。

原始查询：`.local/logs/native-bc3-three-arm-game-v1/public-cold-query/`，
含sc-ahb.log、actual maps/driver SHA、result.json及清理记录。

目前不能给出净毫秒/FPS收益。即使全部隐藏3.526ms睡眠，当前owner约
18ms实际Running仍存在；GPU也可能已连续执行。该实验有明确公开合同与
可测覆盖，但不是60FPS保证。另一路普通Simpleperf正在细分现owner高层
职责，独立AHardwareBuffer/ASurfaceControl出图保留为后备功能实验。
