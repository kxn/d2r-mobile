# 同尺寸性能、呈现等待与纹理编码：当前证据

## 1. 实际交换链已统一，纹理编码仍需隔离

2026-10-27晚的两臂均由真实日志核准：游戏输出1968×900、主3D 1673×765、
比例0.85、最终VkSwapchain 1968×900、同原营地位置。没有改系统显示尺寸。
官方SDP九指标与同窗Perfetto结果：

| 工具窗指标 | 我们，共享/B3 | 上游，系统默认驱动策略 |
|---|---:|---:|
| FPS | 43.933 | 43.111 |
| GPU读，MB/完整Surface | 103.566 | 106.795 |
| GPU写，MB/完整Surface | 43.737 | 45.811 |
| Fragment指令，G/完整Surface | 1.8754 | 2.3961 |
| 已证owner Running，ms/Surface | 18.384 | 18.558 |
| owner运行期MHz | 1654.118 | 1649.122 |
| 全应用Running，ms/Surface | 34.005 | 37.609 |

这对已满足同像素尺寸，不继续沿用不同交换链的旧数据回答严格尺寸差距。
但只有一对、各8秒，且driver、资源编码和后端不同，不宣称全场景领先，
也不把全部工作节省归给共享。GPU计数含合成器，不是单pass或CPU memcpy。
上游是保留b811原后端的驱动控制诊断包，不是release native逐字节相同。

owner身份来自原native日志TID及官方trace PID/TID/lifetime对应，未按最忙
线程猜。原通用报告一度缺少跨schema的receipt关联；比较器已补严格路径/
SHA/PID/APK适配，两端scheduler与原Running数据相符，没有补造PMU budget。

当前上游实际用原BC资源；我们6217个native条目中3379为ASTC4、2838为
ASTC6，其余24855个cache条目仍为原BC/普通格式。历史同营地GFXR非附件
采样图像中，上游BC3为2442、ASTC为0；这些是对象数，不是读取次数，
不能据6217的全包比例计算当前帧带宽收益。两种编码、共享与后端应分开看。

证据：`.local/logs/surface-parity-20261027/`，含硬准入、原日志、TID证明、
九指标、完整CPU报告与恢复凭证。两包、原profile及所有原资源已恢复，
两游戏停止，本次GUI/collector均清理。后续原包状态另以最新实验凭证为准。

## 2. 同队列呈现线程：等待搬走了，没有被消除

使用同accepted39197b/native437 APK，固定同尺寸、B3、共享、shader、
fixed/extra录制去重、sparse OFF、ADPF OFF。B只启用现有呈现worker，
仍为一个VkQueue，没有第二队列bridge或换CPU核。

| 普通工具对照 | A，原呈现 | B，同队列worker |
|---|---:|---:|
| FPS | 43.285 | 43.439 |
| owner Running，ms/Surface | 18.654 | 18.059 |
| 新worker Running，ms/Surface | 0 | 0.618 |
| owner+worker Running，ms/Surface | 18.654 | 18.678 |
| 全应用Running，ms/Surface | 35.079 | 35.194 |
| owner用户态周期变化 | — | −0.067～+0.062% |

owner看似少0.594ms，实际worker另花0.618ms，总CPU没有减少。owner原本
在present内睡约3.148ms；B改为在submit queue lock内睡3.406ms。唤醒来源
从kgsl-events变为持锁的dm-present，直接证明等待搬到了队列外部同步。

B worker的present约22.208ms，其中Running0.528、S19.398、R/R+2.282ms。
主要长S由kgsl-events唤醒，不是醒后在CPU7排队几毫秒：862次完整调用中，
最后kgsl唤醒至返回约0.270ms，首段Runnable平均0.063ms。同核竞争主要在
最后长睡眠之前。19ms睡眠不是19ms shader时间，具体fence/WSI/ioctl缺少
调用栈，未唯一定位。824次owner锁入口发生在该长S期间，等待确实传回owner。

故默认不改用worker：当前机制未解除关键等待，还增加线程与在途复杂度。
这不是仅因没有涨FPS就否定异步方向，而是整轮与逐事件数据已显示成本
和依赖仍然存在。

证据：`.local/logs/wsi-shared-queue-current-v1/whole-profile-comparison/`与
`present-timeline/`，保留全部逐次原时间戳、状态和waker，不凭空编frame ID。
测试结束原save2/stop，原APK/profile未改。

## 3. 提交次数的口径修正

`guest_submits`与`internal_submits`互斥。各1/guest是应用API总2/guest，
不能把internal那列叫作所有vkQueueSubmit。固定b811的真实六帧GFXR同样
12次应用submit（渲染与呈现各一次），并非对方总1、我们总2。初始化/
状态恢复submit已排除；不把driver内部隐藏提交混进应用API计数。

没有发现“搬上游现成合批就省一半”的差距。延迟提交涉及QueueFlush、
fence前缀完成、CPU观察及共享内存门控，不能为少一次API破坏这些语义。
证据：`.local/logs/submit-present-batching-audit-20261027/`。

## 4. 调频与外部上限必须分开

本机是WALT；既有trace显示准备/回放运行期频率约1.51～1.56GHz，present
附近约1.73～1.75GHz。频率分布值得调查，不再仅当作干扰因素。但逐次
等待前后多数频率不变，也不是每次睡眠就立即降频。

停机后的只读快照发现vendor CPU cooling state5/6，按实际available+boost
完整表精确对应policy3上限2188800、policy7上限2150400kHz。GPU cooling
也为非零state8/max10，缺实际OPP表，不认定它等于500MHz。公开驱动是
执行器，不判断触发原因；不能仅凭非零状态宣称过热。

原ON1 trace还保存CPU0一次限值更新：sensor_monitor（thermal-engine-v2）
唤醒kworker，后者运行并发出595200～1900800kHz limits事件。这是实际
CPU0执行链，不外推成CPU7/GPU的触发证明。实际daemon无启动配置参数，
可读conf是空模板；可执行文件与devfreq目录权限拒绝已保留，不绕过权限。

来源与SQL：`.local/logs/cpu-gpu-dvfs-feedback-v1/`。该来源缺口不阻塞共享/
资源准备主线，也不通过关闭保护或自旋伪造负载来试图换频率。

## 5. 正在实施的同编码入口

普通NVN ASTC4/6资源候选已离线生成，无重编码；但b811实际动态ASTC→BC
copy分支只处理4×4，不能部署后忽略该边界。其header内部分策略helper
没有在实际建图路径被调用，不能拿未使用函数推断运行行为。

为保持共享并直接使用原始BC3，先扩现有16字节4×4 native资源契约到
UNORM/sRGB两种格式：原始CASC字节、原NVN排布、原GPU布局查询和现成
producer均复用，不新写encoder/tiler，不改游戏模块。独立driver仅增加
格式准入，runtime/packer保留原identity/大小/mip/thumbnail限制。未知
driver仍保留FOREIGN交接。新driver实际ELF SHA为
`0e53ffa2bbbff0a4f520eb080039a848f0853276ece59caa8df9a029cf2b4024`；
已基于完整source差异和下述真实NVN回归加入实验candidate的exact provider
列表；resident/lazy实际正向验收另行记录，不替换日常资源、不报告FPS收益。

独立普通UID真机验证已通过两阶段：v1保留原ASTC4/6回归并加入BC3
UNORM/sRGB；v2覆盖BC3 13×9、4级mip，比较同一压缩块的共享导入与普通
非external optimal VkImage，8组GPU采样读回逐字节相同，另含非端点
RGB565及sRGB CPU参考。两份原始CASC资源各256×256、9级mip已分别完成
driver布局加工/压缩块回读；v1/v2物理payload SHA相同，producer身份
与recipe分别记录，未重标旧8064身份。两次测试包均卸载、进程及临时目录
均清理，原391 APK、437 ELF、28模块、shader、profile与native表未改变。

v1两份实际bake产物通过原packer生成独立BC3表，原NVN公开metadata
fixture各61项检查通过，覆盖完整Builder的size/alignment/mip offset及
thumbnail形状回落。这一步只在host执行。之后普通UID原NVN两格式均已
真实通过完整9mip及逐baseMip绘制、5层原thumbnail CPU排布/B2T、GPU alias
writer、CPU Flush、T2T及释放回归；两格式315/313检查，都是FOREIGN模式，
不能冒称resident/lazy已验证。初次fixture在采样前因旧MAP_SHARED backing
不满足现有RW alias契约失败；修复只接回原真实guest heap/SVC，未放宽
生产alias准入。该失败及修复前后源码/包均保留。

证据：`.local/logs/native-bc3-gpu-probe-v{1,2}/actual/`与
`.local/logs/native-bc3-real-pack-v1/`，保留原始日志、query、SHA和清理记录。

实施入口：`tools/experiments/turnip-native-bc3/`与
`scripts/dev/experiments/native-bc3/`。全部6217条原始BC3输入现已准备，
与当前名单及顺序完全相同，共49批；原CASC及原NVN逐mip往返、冻结v2
producer CPU解析通过，随后6217/49批实际driver加工全部通过并清理。
原packer已完成新表与完整cache合并，物理payload443,588,608字节、表
6,565,312字节。原CASC工具合成候选overlay并逐项读回全部6218替换，
TVFS/root/config修改范围证明通过；原UI107个payload中106个SHA保持，
仅必要config及manifest按原contract重建；合成记录不构成游戏验收。
原始BC3有效块401,317,552字节，当前同名单混合
ASTC为318,250,336字节，少20.70%；这只是离线有效块量，不能换算当前帧
流量。resident/lazy正向验收已在后续同APK两driver测试中完成，真实游戏
性能对照尚未完成。
全包记录：`.local/build/native-original-bc3-full6217-v2-inputs/`及
`.local/logs/native-original-bc3-full6217-v2-host-verify-final/`；同名单有效
块量核对：`.local/logs/native-bc3-real-pack-v1/offline-format-footprint.json`。
实际NVN：`.local/logs/native-bc3-nvn-gpu-v2/actual/`；全包加工：
`.local/logs/native-original-bc3-bulk-v1/actual/`；完整pack与overlay：
`.local/build/native-original-bc3-linear-{pack-v2,game-overlay-v1}/`。

## 6. 更应优先验证的布局代价

源码与历史记录确认，B3/0e53 native compressed images强制whole-linear，
不是HOST_ALLOCATION共享所必需。旧AA185本已使用原FDL tiled布局；f830
是为减少opaque padding选择全线性，历史全ASTC4的同名单payload从约
696.4MB降到443.6MB，但没有同编码、同driver的线性/平铺GPU净收益对照。

旧官方6帧的实际descriptor绑定与draw归属中，308张native纹理覆盖
2874/14496个draw，约479个/帧；其中358个/帧在主HDR组。303张按普通FDL
阈值本可用tiled baseMip，现被线性覆盖。该比例不是GPU时间预算，也不是
当前帧带宽计数；它提供了比冷资源占用更强的实际使用覆盖依据。

相反，ASTC→混合BC3复制确实把一张128×128/7mip/42层数组扩为RGBA8，
实际allocation从1,032,192增至4,816,896字节；但两端历史6帧都没有实际
绑定含该view的descriptor或提交其copy，不能以该扩容解释营地稳态FPS。

已独立构建BC3 tiled候选`3e9376ed…ee50`：仅排除BC3的强制线性分支，
ASTC策略不动，原FDL tiled/small-mip fallback复用；因布局变化，原UUID
算法内profile盐版本化。实际query新layout UUID为
`4504211f903049f5ae3acd7eda74b7bb`，没有重标0e53的旧payload。相同919
producer重新加工两份256×256/9mip资源，size由94,208变135,168字节。
新driver NPOT普通image参考及原NVN完整两格式/每baseMip回归均已真机
通过并清理。13×9本身仍走small-mip linear，不能单独证明tiled正确；
实际256/9mip NVN回归覆盖了tiled与线性mip尾部。尚无该候选FPS收益。

证据：`.local/logs/{mixed-compressed-copy-audit-v1,native-linear-layout-audit-v1}/`、
`.local/build/turnip-native-bc3-tiled-v1/build.json`、
`.local/logs/native-bc3-tiled-{probe-v1,nvn-gpu-v1}/actual/`。

后续3e新driver对全部6217输入独立重新加工，49批全部通过，未复用或
重标线性payload。两包原始块、source/descriptor、ID/顺序、thumbnail及
完整31,072条cache SHA相同；仅物理布局及其身份变化。平铺payload实计
696,385,536字节，线性443,588,608字节；这是整包物理数据量，不等于
运行时驻留。两套完整overlay均已通过6218项CASC读回及原UI/config证明。
控制核对：`.local/logs/native-bc3-real-pack-v1/linear-tiled-control.json`。

同一72ad APK/7a37 ELF另完成两driver×两格式的resident+lazy正向真机
验证：每次全mip采样后断言实际owner==2、现成startup统计lazy active及
64KiB window；alias GPU写、B2T/T2T、CPU Flush、同物理页/无payload副本、
最终释放均保留通过。仅跳过与immutable lazy政策不相容的destructive
union测试，未使用provider override。四case363/361检查，两包清理成功，
原391/437/profile/table/28模块保持。root直接归约原结果中必需断言：
`.local/logs/native-bc3-real-pack-v1/resident-lazy-root-verification.json`。

已用原SDK重封包为同签名正常主包carrier992c6f15/7a37；六库、DEX、assets
125项字节不变，正常game/nativeStart入口未加入fixture EXPECT环境。
该carrier供三臂共用，避免换runtime ELF混入布局对照；其source ledger
明确来自本次fixture编译，未伪称原391。当前仅准备完成，游戏/SDP整轮
净收益仍待实测，不能以这些GPU正确性通过替代FPS结论。
