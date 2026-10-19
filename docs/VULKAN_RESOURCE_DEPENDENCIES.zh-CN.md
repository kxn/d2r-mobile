# Vulkan 资源访问依赖的细化

目标是在原 NVN 语义不变的前提下，按实际资源的读写关系允许 GPU 阶段重叠。
不能将 CPU 不再阻塞等同于 GPU 内部没有宽屏障；也不能把 TBDR 某个 API/draw
时间当作独立工作成本。比较使用现成同步校验、Arm 队列计数器和整帧吞吐。

## 当前证据与缺口

正确采样精度、2772×1240 固定 3D 1.0 的 Mali 窗口，GPU 848MHz 时为
22.3885 FPS；Arm GPU 利用率 99.7%、片元受限占比 100%。Perfetto 30 秒里
上传 CPU publication/GPU access 等待均为零，资源上限等待 66.664ms。
这些事实没有证明 GPU 内部同步足够细：绘制、计算、传输的多数后备屏障
来源仍是 ALL_COMMANDS，图像布局仍整张记录。实际 GPU fence 保持真实完成语义。

## 第一块：在途缓冲上传（实现中，未验收真机性能）

- 继续复用原在途提交/完成 worker 的资源所有权，不增加 CPU GPU 等待。
- 在缓冲解析入口声明实际阶段：顶点/索引输入、顶点着色器、片元着色器、
  计算、传输、间接命令；未知或不覆盖的阶段保守归入 ALL_COMMANDS。
- 每份提交拥有独立的资源访问页位图和阶段位图，跨提交保留到真实完成后
  才回收。4KB 页只用于访问来源检索；它会包含同页不同字节的额外阶段。
- CPU 变化仍按实际 4 字节连续段取独立暂存。直到收集完这一提交的上传，
  才统一录制位于主命令缓冲之前的上传前缀；保持原来的单次队列提交。
- 前缀前面的依赖为“这些目的缓冲范围的先前使用阶段 → TRANSFER”。
  前缀后面为“TRANSFER → 当前提交对这些缓冲范围的使用阶段”。使用实际
  字节 offset/size 的 BufferMemoryBarrier，不再对所有内存建立上传依赖。
- GPU→CPU 回读所用的传输读也必须记入先前读者，否则会遗漏上传覆盖它的
  WAR。后续提交在其他阶段读取新上传范围，继续经过现有传输生产者可见性
  检查；不能因为当前提交只读于一个阶段就遗忘未来消费者。
- 未知使用、特殊未对齐尾段和原全局参考模式保留保守路径。GPU 真实输出与
  CPU 新数据共用页时仍只上传变化段，不能用旧 CPU 字节覆盖 GPU 写入。

宿主已通过 544194 项，包括真实门控 GPU 队列下提交不等待、同页 GPU 输出/
CPU 新数据保存、跨页阶段并集、未知阶段回退和完成后退役。该数量不代替
真实游戏或性能验收。日志 `.local/logs/resource-stage-host-validation.txt`。

## 后续接入次序与成立条件

1. **图像子资源访问。** 在缓存图像上记录 aspect/mip/layer 和使用阶段、
   读写类型；采样与 storage image、附件、copy/clear 分开声明。重叠查询
   要包括多 mip/layer 范围，深度/模板分量，以及现有 alias/可变格式升级。
   未覆盖路径必须使对应图像进入保守状态，不能默认为从未访问。
2. **一次操作先汇总再判依赖。** 描述符准备不立即更新历史，先汇总一次
   draw/dispatch 的全部来源和目的。否则同一资源在两个绑定中的使用可能
   被错误解释为两次前后操作。需要结束 render pass 时保留正确 load/store
   内容，同一通道内固定功能混合/深度的光栅顺序不额外加全局屏障。
3. **计算依赖。** 由真实绑定范围推导 RAW/WAW 内存可见性与 WAR 执行顺序。
   独立图像/缓冲的旧片元工作不应成为新 compute/vertex 的默认来源；先前
   写入对不同后续阶段的可见性要分别记录，不能一次读取就清空全部历史。
4. **通道开始依赖。** 用实际附件与着色器访问的冲突替换通用 pass 开始
   屏障，允许旧片元与新顶点/tiler 重叠。显式 NVN primitive/indirect 顺序、
   未知可写顶点资源和 alias 边界继续保守处理，不能按场景/相机判断。
5. **逐 mip 布局和 pass 保留。** 在子资源访问正确后处理；不能用整图的
   layout 字段宣称逐 mip 完成。减少物理 pass、附件外存写回和重载要分别
   用原厂计数器核验，保留原项目的覆盖证明成立条件。

验收顺序：同步验证与数据/画面回归 → 同一原始游戏、原生分辨率、固定 3D
的候选/参考对照 → 同频率下整帧 FPS、读写字节/帧、队列 endpoint stalls、
MCU/flush 工作与 CPU 等待。计数器可重叠，不能按阶段相加或声称它们都是
独占 GPU 执行时间。30 FPS 目标在真实稳定窗口达到前保持未完成。

第一块候选 APK `1dc15e4785d1418fdc4aee7c444f887e732835bc52f7a283a7159da4e57637e2`、
宿主 ELF `eb643354…` 已通过 Mali 544975、Adreno650 540815 项 GPU 校验。
650 不支持 timeline，因此其成功不覆盖门控队列测试。Mali 原游戏 PID18796
已进入原生营地并通过同步校验，日志未见 VUID/SYNC-HAZARD，场景截图正常；
两次生命周期提交保存后才停止用于外部原厂性能采集。新设计与现有宽屏障
仍共同存在，不能把这一块验收写成整个资源依赖图已完成。

## 进一步拆分上传阶段（第二块候选）

第一块依旧把本次所有上传的 source/destination 阶段取并集，这会让不相交的
顶点数据上传被另一份片元数据的依赖拖住。因此每份上传保留自己的阶段，
只将来源/消费者阶段都相同的上传合并；早期阶段数据组先于片元依赖数据组。
排序前用目的 VkBuffer + 页验证所有上传页不相交；页重复、范围跨页或记录
超过有界证明容量时保持原顺序，只合并相邻同阶段组。未知来源仍保守回退。
每组独立记录范围屏障、拷贝、消费者可见性；不增加 vkQueueSubmit，不增加
CPU 等待、不改真实 GPU 完成语义。

新增 GPU 门控测试用逆序的两个阶段声明、同页旧 GPU 输出和两页 CPU 新写入，
验证早先 GPU 读取、后续 GPU 读取及旧 GPU 输出均正确；声明额外阶段为保守
超集，并不宣称这个测试测得了 GPU 阶段重叠性能。宿主 552397 checks 通过。
第一块原厂窗口 22.056625 FPS，GPU 始终848MHz；不是受控 A/B，不能宣称
相对旧22.3885 FPS有收益或回归，也不能因即时未涨帧撤回成立的范围缩减。
第二块及图像/计算依赖仍需真机与原厂 Profile 验收。

第二块最终候选 APK `70111c92…`（完整源码/产物校验记录
`.local/logs/resource-stage-groups-build-verified.json`）已通过宿主552397、
Mali553178、Adreno650540815项测试；Mali实际原游戏 PID20860进入营地，未见
VUID/SYNC-HAZARD，保存后才重启至外部采集模式。未知用途、重复页或超过
有界独立性证明容量时仍保守处理，不能以此声称整个后端没有宽依赖。
规范依据见 [Khronos 同步章节](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)：
缓冲范围限定内存访问范围，阶段掩码限定执行范围；不能通过只限定 buffer
就认为来自不同阶段的全部操作已自动分开。

分组候选的原厂窗口为21.6546875FPS、GPU848MHz，仍未达到30FPS。
上传CPU publication/access等待为零，资源上限等待61.427ms/30秒；
片元受限100%、GPU利用率99.7%、外存约537.65MB/帧。单窗不证明涨帧，
也不应把这些结果写成图像/计算的资源依赖已经足够精细。保留当前可验证的
资源隔离，后续优先检查尚未细化的GPU依赖与新增范围屏障的真实flush成本。


## 图像访问与纯图像计算候选

新增 `nvn_vk_image_dependencies.inc`，由实际 VkImage 持有 aspect × mip × array
layer 的读者、写者、写入访问类型及已可见消费者阶段。3D 图像的 2D 附件面
归入整个 3D mip；不把深度 slice 当作 Vulkan 数组层。记录跨提交保存，真实
完成后随图像退役；可变格式升级保留旧图像的历史和所有权，新图像独立分配。

一次 draw/dispatch 的图像绑定先收集，实际命令之后统一更新历史；先判全部
旧依赖，再合并同次操作的多个别名绑定和跨阶段读写。同通道的附件访问一并
记录。原拖动图标的递归重绘使用独立嵌套批次，避免覆盖外层尚未完成的绑定。

纹理 CPU 上传（独占暂存）、纯图像 copy/blit、整图 clear 及独占暂存回读，
根据实际重叠子资源建立依赖。写后读/写需要写可见性，读后写保留执行顺序。
GENERAL 布局内不需要转换的依赖，按同一来源/目的阶段对合并成 MemoryBarrier，
不逐 mip 发一次 API，也不将不相干阶段对取并集；这与
[Khronos 同步示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
建议的内存屏障合并方式一致。

只依赖图像和宿主不可变常量的计算，已改用这些实际依赖。含 guest UBO/SSBO/
texel-buffer 的计算继续保留现有计算屏障；池缓冲与图像之间的拷贝继续保守。
原始 VkImage 句柄、亮度/外推增强入口保守记为未知读写。显式 primitive/
indirect 顺序及全局参考模式仍完整同步。普通绘制入口的宽来源屏障尚未替换，
逐 mip 的 layout 尚未实现，不能将子资源访问历史写成整个后端细化已经完成。

新增原创 GPU 校验：同一存储图像在一次计算中双绑定并原位读取/改写，连续
计算 RAW/WAW，随后拷贝并覆盖源图的 WAR，以及其余 mip/数组层内容保存。
宿主 lavapipe 同步校验通过552740 checks；该数量不是实际游戏性能结果。
候选 APK `5ce50020…`、完整源码/产物记录位于
`.local/logs/image-dependencies-build-verified.json`。真机及原厂性能验收进行中。


此候选现已通过 Mali553521项同步校验；原游戏 PID24395在2772×1240、固定
3D1.0下进入营地，日志无VUID/SYNC-HAZARD，实际画面未见缺失。完成原生命周期
两次提交保存后，才重启为无自定义计时的外部工具采集 PID25039。

Arm LWI/Streamline2..18s窗口为21.919625FPS（20.5..23.6），GPU490次频率事件
均848MHz。PerformanceAdvisor模型仍为片元受限100%、GPU利用率99.7%，外存
约496.155MB/帧；上一候选另窗约537.651MB/帧，两者尚非受控A/B，不据此宣称
已证明节省7.7%或稳定涨帧。Perfetto30秒资源上限等待44.140ms，上传CPU发布
等待一次0.139ms，实际NVN GPU fence等待9.135秒；这些CPU墙钟跨度不是GPU
独占时间。当前仍未达到30FPS，绘制及guest-buffer计算的宽屏障是后续重点。

Adreno650首次运行在原分块提交测试触发时间戳顺序断言（像素/采样数检查已
通过该位置），原失败日志保留。随后未改二进制、用LLDB检查三个分块/串行
运行的实际计数记录，全套541158项通过；旧701候选参考亦通过，当前5ce候选
随后普通运行541158项通过。尚未确定首次失败原因，不能将它写成已修复。
记录见 `.local/logs/image-dependencies-tablet-timestamp-investigation.json`。


## 绘制通道的资源依赖候选（2026-10-17）

普通 draw 的描述符与附件访问在选择/创建物理通道前汇总，不再无条件执行
ALL_COMMANDS 到片元/深度/颜色的屏障。图像按重叠子资源查询来源阶段；GPU
缓冲生产者按原逻辑字节范围查询，顶点和片元的可见性分别保留。直接 guest
UBO、只读SSBO和只读texel-buffer都登记实际消费者阶段；可写缓冲、未知传输、
显式顺序在实际依赖边界及全局参考路径仍保守处理。含guest-buffer的计算
入口尚未改为完整范围WAR/RAW/WAW图，继续原有保守依赖。

同一真实通道内的固定功能颜色/深度访问沿光栅顺序执行；只读深度测试不是
每draw一次写入。附件store在物理通道结束时独立记入历史，深度/模板未启用
maintenance7独立访问能力，依赖记录保守耦合。像素矩形尚未追踪，因此局部
写不能删掉同一mip/layer其他像素的旧生产者阶段。依据见
[Khronos render pass load/store](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)。

物理通道开始前，对已知图像写入合并一次片元读取可见性，覆盖同通道后续
draw才首次绑定的纹理；避免仅因晚绑定就额外store/load。该屏障的目的仅为
片元读取，顶点/tiler没有被加入目的阶段。未知原生句柄不使用该合并证明。
当前通道的新写入会使相应资源的证明失效。新的实际冲突才结束通道；查询池
重置若另行结束通道，则重查附件依赖，不能沿用原同通道豁免。

宿主原有通道数量、遮挡前缀、只读深度采样及拖动图标重绘断言均保留且通过。
新增原创GPU像素测试检查片元只读storage texel-buffer在两次draw之间观察
GPU更新；宿主553000 checks通过。首轮Mali553521 checks通过；追加深度/模板
耦合与该新测试后的候选e067ab07正在验证，尚无该候选整帧性能结论。


绘制候选的最终追加：图像写入不再误用缓冲写入标志，消除图像-only FS 写入后
重复的全局屏障及无关常量缓存失效。保持实际缓冲写回的保护不变。b16ee113
通过宿主553000（保守图形参考552999）、Mali553781项及实际原游戏同步校验。
其原厂窗口22.2315625FPS，仍未30；物理通道约54/帧，上一另窗约53/帧，外存
约541.5MB/帧，尚未受控A/B，不能宣称净收益已经成立。

下一候选bd4739c0补上实际 clear 通道开始的附件依赖，避免大多数“先清再画”
通道仍走旧ALL_COMMANDS来源；同时把present来源声明为明确的TRANSFER读取。
最终blit仍保留，黑边clear与blit重叠写只同步该swap image的TRANSFER阶段，
没有删除拷贝、改变裁剪、缩放或输出分辨率。原亮度增强的公式和调度保持原样，
仅通过内部资源序号将其实际compute输出登记到图像历史，避免已知写入长期
被当作未知ALL_COMMANDS来源。原有该增强的全局屏障暂留，不把它改成新玩法。
该候选宿主553000、Mali553781项通过，实际营地通过同步校验；原厂帧率采集中。

尚未完成的主项：混合guest-buffer计算仍使用宽计算屏障。后续应组合可写范围
的在途实际读者阶段（必须包含CPU写回所用TRANSFER读者）与跨提交保留的写者
来源/消费者可见性；只读输入使用真正的读取依赖，不能只看CPU是否还等待。
页位图可保守覆盖WAR，但已完成写入仍须保留GPU缓存可见性证明；未知/未对齐
暂存和显式NVN顺序须继续回退。禁止把独立buffer/image之间的阶段对重新合并
成ALL_COMMANDS，也不能通过忽略真实GPU完成来改善表面FPS。


bd4739c0实际营地PID31444已通过同步校验，画面可见角色、营地、HUD，未见明显
缺失；保存后重启PID32027做无自定义计时的Arm采集。原生2772×1240、固定3D1.0，
LWI窗口22.404FPS（21.2..23.8），483次GPU频率事件均848MHz，PA GPU99.7%、片元
受限100%，外存536.432MB/帧；物理通道约54.019/帧、compute11/帧。Perfetto30秒
上传CPU/GPU等待均未出现，资源压力36.006ms、实际guestGPUfence墙钟9.602秒。
该结果仍未30，也不证明相对另窗有显著涨帧；继续精确缓冲计算依赖，并检查
相对旧53通道窗口多出的约一个通道，不能将较宽依赖换成无谓的tile写回/重载。
证据 `.local/logs/graphics-dependencies-v4-original-game-official-summary.json`。

## Guest 缓冲与待处理顺序的细化（2026-10-17 晚）

实际计算现在汇总 UBO、SSBO、storage texel-buffer 的逻辑字节范围。只读范围
查询跨提交保留的写者/消费者可见性；可写范围额外查询在途提交和当前录制中
的读者阶段，处理 RAW/WAW/WAR。读者阶段仍以池的保守页图查询，不宣称已经
实现所有字节范围的精确读者图。CPU 完成不能消除写入缓存的可见性要求，
GPU 依赖也不能提前完成 NVN 栅栏或 CPU 写回。

发现并修复了两类描述符标志的混用：BufferUsageFlags.Write 是 bit0，而
TextureUsageFlags.ImageStore 是 bit2；纹理 bit0/bit1 是缩放属性。只读整数
纹理不能因此被登记为 GPU 写者；真正的 texel-buffer 写入也不能漏登记。
存储对齐副本单独保留初始拷贝、计算改写及拷回依赖，不再把原逻辑范围误当成
计算直接访问的缓冲。递归原有 UI 重绘使用独立操作状态，描述符缓存仍按提交
保留。新增实际 GPU 测试覆盖跨三次排队提交的 texel-buffer 读写及 CPU 发布。

第一版 `0ab9cbc0` 的官方 LLDB 记录显示，21 次完成的计算入口中，15 次仍因
NVN 顺序标志、2 次因对齐副本回到宽来源，仅4次走细化路径。后续 `103ce11e`
让已完整声明资源的直接绘制/计算使用实际依赖，并将顺序标志继续保留到 TOP
时间戳、间接命令及未知访问等边界；没有简单清除这些标志。对齐拷贝也改用
实际 TRANSFER 依赖。完整提交参考模式及未知来源继续保守同步。

`103ce11e` 通过宿主553267、完整提交参考544603、Mali554048和Adreno735
553272项校验。真实 Mali 营地原生2772×1240、固定3D1.0，无同步校验错误。
原厂 Streamline CSV 平均24.6825FPS，PA24.8FPS、GPU99.5%、片元模型100%；
545次GPU频率事件均848MHz，53物理通道/帧、11计算/帧，外存568.306MB/帧。
上一0ab另窗21.6733FPS并非受控A/B，不能把差值写成已证明的净收益，也未达30。
Perfetto30秒中，客体栅栏CPU墙钟9.543秒、资源压力2.010ms、上传发布0.114ms；
这些不是GPU独占时间。实际LLDB复核的计算/对齐依赖不再调用宽计算屏障，来源
为COMPUTE、TRANSFER或两者组合。记录：
`.local/logs/compute-resource-order-original-game-official-summary.json`。

真实Adreno735也进入2750×1260营地，未报SYNC-HAZARD，但有R16_UNORM深度比较
采样VUID06479；0ab日志亦存在，不是103引入，不能记为“全部正确性通过”。
Snapdragon Profiler2026.9已保存当前原游戏的原厂计数器会话。利用率指标用未知
最大频率的默认值归一化，不能据约35%推断GPU空闲；此会话不提供正式游戏FPS。

继续用LLDB检查35次残余全局屏障，集中于原有亮度LUT的三处及绘制前传输可见性。
下一候选 `2098c266` 用同一图像历史声明LUT的TRANSFER读取和COMPUTE写入，私有
快照单独处理旧计算读→新拷贝写→计算读；不修改原来的公式、曝光或游戏逻辑。
只读绘制前的未命名暂存副本按TRANSFER→顶点输入/VS/FS建立可见性，不再为它
排序无关片元生产者。可写/未知缓冲与完整参考模式保留回退。宿主普通/参考均
已通过，真机验收中，尚不能将这版写成已提升帧率。

`2098c266` 随后通过Mali554048/Adreno735553272项及真实Mali营地同步验证。
其原厂窗口15.90775FPS，同时Perfetto的gpu_freq/gpu_freq_max从848降至383MHz，
时加权约444.69MHz；另一个稀疏gpufreq轨道仍显示848000，不能忽略实际频率
上限轨道。PA的CPU受限83.2%不能单独证明代码引入CPU瓶颈，不能与前一848MHz
窗口直接比较FPS。残余全局屏障不再来自LUT和绘制暂存回退，而是依赖表溢出：
LLDB实读writer_records=256、overflow=1。官方simpleperf15秒记录2045样本、
丢失0，vk_buffer_shader_read自身约5.92%（所有进程线程样本），为本次最大
宿主函数自身热点。这是CPU指令归因，不是GPU耗时。原厂/调试器完整记录见
`.local/logs/lut-resource-order-original-game-official-summary.json`。

后续 `b4ed79b1` 合并重叠生产者区间；相邻区间仅在生产阶段相同时合并，避免
将本来独立的TRANSFER/COMPUTE阶段重新耦合。合并时取生产阶段并集，并清除
整个合并区间的可见性证明，允许额外依赖，不能丢掉旧写者。未知/真正溢出
仍保守回退。新增512次实际重叠GPU拷贝测试：检查写入区间仍有依赖、未写入
空隙没有被溢出误标、全部GPU结果及两侧护栏正确。宿主553273、参考544607、
Mali554054、Adreno735553278项通过；真实Mali营地同步验证无错误。此候选仍在
进行无自定义计时的原厂采集，不能预先宣称达到30FPS。

`b4ed79b1` 的首轮窗口22.682FPS，gpu_freq_max在383..848MHz变化、时加权
654.91MHz；同一进程保存冷却后复测22.453625FPS，448次频率事件均848MHz。
冷却窗口外存507.976MB/帧，翻译线程未出现资源压力等待，仍未30FPS。
独立simpleperf15秒2540样本中，vk_buffer_shader_read自身7.44%，image_dependencies
3.82%、image_record2.64%；这是实际CPU指令热点，不是GPU屏障的独占时间。

因此 `59db46ef` 把已合并且互不重叠的缓冲区间按地址保持有序，以二分定位第一个
可能重叠区间，仅遍历实际相交项。读者、生产阶段、可见性、未知回退和真实完成
条件不变。额外GPU测试用乱序插入的独立实际拷贝验证命中、端点和空隙，以及
回读值；普通/完整参考宿主553282/544610、Mali554063/Adreno735553287项通过，
真实Mali营地同步验证无错误。该次原厂13.9095FPS时GPU频率上限固定383MHz，
不能据此撤销索引。随后官方simpleperf2302样本中该函数占比0.87%；不同窗口的
样本占比不是等工作量耗时A/B，但支持继续保留明确减少查询比较次数的实现。

下一候选 `cda76874` 缓存每个真实VkImage最近两个精确视图范围的只读记录和
已建立的消费者可见性。任何图像写入（含操作预清理和未知写入）、重新分配
真实图像均使缓存失效；顶点/片元/计算阶段分别保留，不能把某个mip读者扩成
整个图像。目的是避免相同不可变纹理每次绑定都遍历所有mip/layer，不删除
任何实际依赖或同步CPU完成。宿主普通/完整参考553282/544610及Mali554063项
通过，真实游戏与其余真机验收进行中。

Adreno735 `b4ed79b1` 已取得更完整的官方SDP计数器：最后20秒每项393样本，
Shader Busy82.075%、Texture Pipes Busy55.095%、Time ALUs Working39.593%、
ALU Capacity35.577%，总读取约3.006GB/s、总写入约1.451GB/s。指标含义不同，
不可相加；系统GPU计数器也可能包含合成。独立39秒的SurfaceFlinger timestats
对原游戏SurfaceView实际报告526帧、13.630FPS，不能使用90Hz屏幕刷新率代替。
现成系统工具直接计算FPS，未新增自定义计时器。原厂会话与报告：
`.local/logs/buffer-interval-merge-735-original-game-sdp-counters.json`、
`.local/logs/buffer-interval-merge-735-official-frame-rate.json`。

`cda76874` 随后通过Adreno735553287项、真实Mali营地同步校验，原厂窗口
13.862125FPS时GPU上限固定383MHz。Perfetto30秒真实guest fence CPU墙钟17.322秒、
资源压力1.596秒；不能把这些当GPU独占时间。连续8个实际帧边界的LLDB记录
39..63个生产者区间、无溢出，也未命中全局/宽计算屏障。这个局部路径证明
**不足以证明CPU/GPU同步已经不是主要瓶颈**。

用户要求核实这一判断后，检查原厂事件定义：GPU_ACTIVE计数的是有任务排队的
周期，不等于着色器有效执行周期。已有READY_BLOCKED计数指等待endpoint分配，
也不能直接解释成CPU握手。因原advisor预设未采CSF具体调度等待，给现成gatord
选择脚本新增dependencies预设，追加官方CSHWIF0..3 ENABLED/WAIT_BLOCKED、
endpoint drain及cache flush事件；不修改游戏、不新增计时器、不重启进程。
同一进程12秒官方捕获、2..10秒CSV：CS0/CS1的调度依赖等待周期分别相当于
GPU_ACTIVE周期的31.242%/29.951%，CS2/CS3为13.938%/10.195%，L2flush约0.630%。
这些流的等待会与其他流/执行重叠，**不可相加，也不是独占整帧损失**；系统范围
计数尚未对应到具体Vulkan提交/资源。因此既不能宣布“同步已排除”，也不能仅
据这些比值宣布“等待就是主因”。下一步仍需定位等待依赖及其对关键路径的影响。
完整定义、原厂CSV和数值边界：
`.local/logs/image-read-proof-scheduling-dependencies-summary.json`。

### 在途提交资源上限与等待归因复核

同一旧 Perfetto 的区间关联进一步发现：17.322 秒主游戏线程客体栅栏等待中，
5.474 秒与 NVN 翻译重叠。客体提交序号在入翻译队列时分配，故等待包含尚未
翻译的工作、真实 GPU 完成和 CPU 发布，不能把整个区间归为 GPU 执行或屏障
损失。首次 Vulkan 提交平均在翻译开始后 9.086ms，随后仍翻译 30.739ms。
来源：`.local/logs/image-read-proof-main-wait-translation-overlap.csv`、
`.local/logs/image-read-proof-scheduler-analysis.json`。

候选 `9da60569` 将提前提交共享的在途资源集合上限从 8 增至 16，仅在没有可
回收集合时分配，不改变真实栅栏、CPU 副作用发布和资源生命周期。旧捕获曾因
8 份上限阻塞翻译线程 1.596 秒/30 秒；增加上限允许更多独立批次先提交，新增
成本是更多命令/描述符/暂存资源，分配仍有上界。宿主 553282、Mali 554063、
Adreno735 553287 项通过；真实 Mali 营地同步验证无错误。

新版原厂窗口在 2772×1240、固定 3D 比例 1、无外推/冻结条件下，Streamline
CSV 平均 23.61575FPS，PA 报告 23.7FPS，外存约 529.524MB/帧。GPU 频率与
有效上限均 848MHz；30 秒 Perfetto 未出现资源耗尽等待，客体栅栏等待 7.664 秒，
其中 4.535 秒与翻译重叠。**旧版对比窗口为 383MHz，不能据此把等待/FPS 差值
归功于资源上限调整。** 更早 8 份上限的 848MHz 捕获也曾无资源压力，因此还
需要相同频率和场景的对照。GPU_ACTIVE/PA 受限模型仍不能排除同步关键路径。
来源：`.local/logs/async-window-16-original-game-official-summary.json`。

Adreno735 同版原生 2750×1260 的 SurfaceFlinger timestats 记录 253 帧、
13.545FPS；未达到 30FPS。两台 meminfo 已保存；Mali 旧读数来自已保存后台，
新读数来自前台，不能把 RSS 差值当作增加上限的净内存开销。735 总 PSS 约
5.51→5.54GB，亦非严格同条件 A/B，暂未发现显著增长证据。

为继续对应实际 Vulkan 调用、提交与 GPU 执行，接入 Snapdragon Profiler
2026.9 官方 Trace。必须安装其 `service/android/arm64-v8a/apk/` 下的 Layer APK，
并由 Trace 的 New App 启动、勾选 Vulkan；Intent Arguments 可传入原生固定比例、
异步提交、分块提交和 external_profile 参数。官方 Layer 使用网络 socket，应用
须声明 INTERNET；缺少 APK 导致 vkCreateInstance -6，缺权限导致 socket 创建
EPERM，两项均有真实日志证据。补权限候选 `8a5da6fc` 的宿主 ELF 与 `9da60569`
完全一致，不把这一工具依赖修复宣称为帧率优化。原运行进程在所有必要重启前
均先经原 Android 生命周期确认 asked=1 commits=2 保存。

### 按真实完成资源复用命令池

官方 API-only Trace 已完整取得 2 秒、796040 条 API、137 条整命令缓冲 GPU
时间记录；游戏在采集后仍运行。该窗口 vkFreeCommandBuffers 1406 次累计
91.061ms，且每个渲染通道的次级命令缓冲均反复释放。不能把完成 worker 的
vkWaitForFences 睡眠直接当作主线程停顿；此处选择减少实际分配/释放工作。

候选 `596922dc` 为每份 submit_resources 独立保留命令池、一级/二级命令缓冲
句柄。仅当真实 GPU 栅栏和全部 CPU 副作用都完成，提交线程才整体 reset 该
资源的命令池，再复用其句柄；其他在途资源拥有独立池，不会一起被 reset。
上传前缀也由同一份资源持有。未删除真实完成条件，未增加全局 wait；新成本
为保留各资源集合使用过的最大命令缓冲容量。直接 Vulkan 在途描述符测试改用
自有独立池，其阻塞信号量/回收另一份资源的测试语义不变。

普通/完整参考宿主 553282/544610、Mali554063、Adreno735553287 项通过；真实
Mali 2772×1240 营地同步校验无错误。原厂捕获 13.6975FPS 时频率和有效上限
固定 383MHz，不与上一 848MHz 窗口直接归因比较。30 秒内未观察到资源上限
等待，但客体栅栏仍等 17.276 秒，其中 4.844 秒与翻译重叠，不能宣布同步排除。
报告：`.local/logs/command-pool-reuse-original-game-official-summary.json`。

高通初次开启 Rendering stages 曾在官方 Layer 的 QPrf/VkFencePl 线程空指针
崩溃：get_dispatch_table → QglQueries::OnCommandBufferCompleted。复用版在
同一进程完成 2 秒 Rendering stages + Rendering workloads + API Trace，采集
正常结束且游戏存活。保存会话 captureID=6（会话也包含旧捕获，必须过滤）：
849688 条 API、159 条整命令缓冲 GPU 时间、9136 条流水线阶段、1509 条 surface。
热身后 vkAllocateCommandBuffers/vkFreeCommandBuffers 均 0 次，改为 150 次
vkResetCommandPool，共25.749ms；两窗采集选项/场景/热状态不同，不能把时间
差直接宣称为净 FPS 收益。至少此窗口避免原调试层崩溃，并证明分配释放减少。
`.local/profiles/command-pool-reuse-735-rendering.sdp`、
`.local/logs/command-pool-reuse-735-rendering-summary.json`。

SDK 对 GPU 的标签写作 Adreno726，但实际 Vulkan 属性报告735；其 MSAA/GMEM
自动建议还须与实际 Vulkan 附件样本数和有效字段交叉核实。compute surface
中的 GMEM 计数字段有 0xffffffff 哨兵，不能当成实际流量累加。TBDR 阶段时间
仍可能包含依赖/重叠，不把逐项耗时相加当作优化收益。

用户要求 Mali 空闲时停止进程，已写入 AGENTS.md。分析阶段已核实游戏与
采集工具进程不存在，末尾日志确认原生命周期 asked=1 commits=2；不为存档
重新启动已退出进程。后续只在实际测试/采集窗口启动，结束后保存并停止。

### 普通缓冲复制与常量更新的阶段范围

继续分析已取得的官方 Trace capture6，4716 次 pipeline barrier 中有 99 次
ALL_COMMANDS|HOST→TRANSFER；抽查其后紧接 vkCmdCopyBuffer，关联到通用
mtl_sub_copy 和录制常量更新的无条件 vk_transfer_barrier。它们的源/目标都已
知，却仍等待无关片元/附件工作。没有证据把其累计 CPU 调用耗时当作 GPU 损失。

候选 `9c7b8c53` 复用既有缓冲 hazard 历史：复制源只等待相交生产者，目标同时
等待相交读者/生产者；常量更新的 staging 源私有且提交前写完，只检查目标。
新消费者限定 TRANSFER。未知访问/写者表溢出/完整参考模式仍走保守回退。
不改复制数据、游戏算法、CPU 发布或真实栅栏，不新增自定义计时器。

普通/完整参考宿主553282/544610、Mali554063、Adreno735553287项及真实Mali
营地同步验证通过。已有128组真实GPU字节更新/连续复制、护栏、待完成写入
和CPU发布测试继续覆盖RAW/WAR/WAW与跨提交内容。

新版高通官方 Rendering stages/workloads/API 捕获 capture7 正常结束，2秒
3979个 barrier 中 **源阶段包含 ALL_COMMANDS 的为0**；仅余3次
TOP_OF_PIPE→ALL_COMMANDS初始化转换。原99次全生产阶段→传输不再出现。
这证明特定宽依赖被移除，不能宣布同步已不是主要瓶颈或其他场景不会回退。
`.local/logs/transfer-resource-order-735-rendering-summary.json`。

真实Mali原生2772×1240、固定比例1、无冻结/外推，2..18秒官方CSV平均
23.75725FPS（PA23.9），GPU实际频率和上限均848MHz，外存约515.287MB/帧。
30秒Perfetto主线程客体栅栏墙钟7.297秒、未见资源上限等待；首次提交平均
在翻译开始后9.221ms，其后仍翻译24.843ms。上一cap16窗口同848MHz23.61575FPS
并非严格同条件A/B，不宣称净涨帧。高通独立SurfaceFlinger298帧13.758FPS。
当前仍未达到30FPS。Mali已在采集脚本正常结束后原生命周期保存并停止。

下一实验 `early-submit-64` 将 draw 门槛256降到64，仍须实际退出通道与查询
作用域后才提交，不允许靠拆开活动作用域达成门槛。需核实首次提交是否更早，
以及更多提交的CPU成本、资源压力和额外物理通道/读写；未有结果前不宣称提速。

### 提交批次大小与首批提交验证

三个候选均通过宿主553282、Mali554063、Adreno735553287项GPU检查，真实Mali
营地同步校验零错误。保持2772×1240、固定3D比例1、无冻结/外推，官方
Streamline窗口均观察到848MHz实际频率及有效上限：

| 候选 | 官方CSV平均FPS | 首次提交偏移（Perfetto CPU墙钟） | 物理pass/帧 |
| --- | --- | --- | --- |
| 256 draw（9c7b8c53） | 23.75725 | 9.221ms | 53.08 |
| 64 draw（dcf12806） | 25.18925 | 6.339ms | 53.03 |
| 16 draw（9bc59d18） | 25.01656 | 6.523ms | 53.07 |
| 首次完成compute优先，其后64（33126661） | 24.98394 | 6.424ms | 53.05 |

64阈值减少首批录制延迟，16阈值进一步增加提交次数，却未让首次提交更早。
首批compute策略也未在当前营地缩短首次提交。保留安全边界及真实完成语义；
未观察到资源数量上限等待，不等于CPU/GPU依赖已经排除。CPU频率、温度等
并非严格受控，不把这些独立窗口的FPS差当作完整因果A/B。

首批策略初版在任意检查点提交，被既有pass合并测试拦下；未部署该失败版本。
已改为完成compute后的安全边界，绘制间的时间戳检查点不触发首批特例。
独立高通普通运行的SurfaceFlinger为64阈值13.463、16阈值18.254、首批策略
18.072FPS；GPU频率未取得，翻译CPU时间也变化，不能全部归因于批次策略。
各候选证据位于`.local/logs/early-submit-{64,16,head}*`，Mali每次采集正常结束
后均原生命周期保存并停止，分析期间不运行。

### 仅收集上次片元可见性屏障之后的写入阶段

官方高通capture7的最大阶段对为7936→128，共1087次/2秒；7936包含颜色、
早/晚深度、compute、transfer，目标128为fragment shader。核对代码发现
`image_known_writers`只累加，从未在可见性屏障后清空：历史上出现过的阶段会
永久进入后续pass的等待来源。不是ALL_COMMANDS，但仍引入不必要的阶段依赖。

候选`image-writer-window`在该屏障发出后清空阶段集合；后续真实图像写入重新
收集来源。已覆盖的旧写入继续由逐图像写入时钟/片元可见性时钟证明，未知写入
仍走原资源级保守依赖，跨提交不清除写入历史。不删除GPU工作、CPU发布或真实
栅栏，也不改变游戏与shader。宿主553282/完整参考544610、Mali554063、
Adreno735553287及真实Mali营地同步检查通过。官方capture8确认7936→128阶段对
为0次，主要变为1024→128（颜色附件输出→片元读取）；源ALL_COMMANDS仍为0。
会话为`.local/profiles/image-writer-window-735-rendering.sdp`，必须过滤captureID=8。
原生Mali官方CSV24.86125FPS（PA25.0）、固定848MHz、约516.956MB外存/帧，
物理pass53.04/帧。只证明阶段范围收窄，未证明整帧涨速；30FPS仍未达到。

### 按真实片元读者补录pass之前的依赖（开发中）

统一预取仍让每个pass等待尚未真正采样的生产者。已有执行器在次级命令缓冲
录制draw，直到结束才在primary录制BeginRenderPass，因此通道内首次出现的
只读片元资源，若其生产者确实早于当前通道，可以将真实依赖补录在primary。
不需要预同步所有图像，也不需要为该读取拆开通道。

图像使用逐子资源写入时钟与通道起点比较；当前通道自己写过的资源、附件冲突、
未知访问、非片元阶段或保守模式仍走原结束通道路径。缓冲仅允许已知compute/
transfer生产者（它们写入前必已结束通道）；未知来源与溢出不走此路径。
完整全局屏障参考模式保留原预同步证明，以保持其通道分组及原pass级UI重放语义。

新增真实GPU检查：compute/transfer分别生产两张纹理，第二张在首次draw之后才
绑定，两次独立提交均须保持一个物理通道，左右半区逐像素正确。仍保留原
同通道FS写入后再读取、别名、mip/layer、跨提交与原跟手图检查。真机和整帧收益待验。

候选16a3e86e已通过宿主553799/完整参考545127、Mali554580、Adreno735553804，
真实Mali2772×1240营地同步检查零错误。官方LWI/Perfetto窗口GPU频率及上限
固定848MHz，CSV25.398875FPS、物理pass53.04/帧，首次提交6.269ms、翻译CPU
30.455ms。独立不加载GPU Layer的普通运行，SurfaceFlinger501帧24.998FPS；
两边状态快照为固定3D1、原生输出，无冻结/外推，游戏tick441→954、状态时钟
间隔20.503秒。高通同配置普通运行412帧18.292FPS。不能把独立窗口小幅变化
宣称为严格A/B净收益，30FPS仍未达到。两机采集后均原生命周期保存并停止。

### 跨提交的不可变RenderPass/Framebuffer缓存（开发中）

用户提供的[图像用途提交](https://github.com/dodola/d2r-mobile/commit/4375a3fe03)
及[实测报告](https://github.com/dodola/d2r-mobile/blob/main/docs/ANDROID_IMAGE_USAGE_2026-10-16.zh-CN.md)
已核对。其报告的收益来自另一台Adreno642L，不能套用到本机。当前已有按NVN
IMAGE标记保留Storage用途、随图像存活的ImageView缓存；尚缺首次Storage绑定
时的动态升级。已有mutable-format视图升级并非Storage用途升级，不能混称完成。

本机官方capture6已证明2秒内Framebuffer创建1314次/37.548ms、销毁1328次/
26.862ms（API CPU墙钟，工具模式，不是GPU执行时间）。原提交资源回收时会
销毁RenderPass/Framebuffer，因此不是仅缺分析，确实缺少跨提交对象复用。

候选`render-object-cache`独立实现完整键的哈希缓存：RenderPass键覆盖全部
attachment格式/采样数/load/store/layout及subpass引用，Framebuffer键覆盖
RenderPass、全部ImageView、数量和尺寸。RenderPass最多256、Framebuffer最多512；
满时只淘汰没有job引用的条目，均在用时新对象由本次job独占，真实完成后销毁。
Framebuffer自身保留其RenderPass引用，每个job保留对象引用直到原提交资源
可以回收。ImageView实际退休前移除所有相关Framebuffer的缓存索引，避免句柄
复用误命中；旧job的引用仍按真实完成路径归还。缓存锁与Vulkan录制状态分开，
覆盖完成worker退休视图和翻译线程查找之间的竞争。已有ImageView缓存继续使用。

宿主553799/完整参考545127通过；真机、实际调用量减少与整帧收益待验。此项
复用通用Vulkan对象，不改游戏binary、shader、绘制数量或游戏逻辑。采集、报告
处理、代码核对并行执行，实际采集始终使用归档APK/源码哈希对应的固定候选。

e7cfa991版本后续完成Mali554580、Adreno735553804项检查及真实Mali营地同步
校验，均通过。官方SDP API-only captureID=9、PID25797、2秒1203457条API中，
Framebuffer创建/销毁、RenderPass创建/销毁、ImageView创建/销毁均为0次，
窗口包含35次Present。不是空捕获，也不是游戏停止后的采集。会话及汇总为
`.local/profiles/render-object-cache-735-api.sdp`、
`.local/logs/render-object-cache-735-api-summary.json`。此前capture6在另一个
2秒窗口Framebuffer创建1314/销毁1328；不能把不同工具选项下的CPU耗时差或
Present数量直接归因为FPS收益，但热身后的对象创建/销毁确实被消除。

Mali原生2772×1240、固定3D1、848MHz实际频率及上限的官方CSV25.0440625FPS
（PA25.2），物理pass53.06/帧、翻译CPU31.399ms。未证明整帧提升，不因当前
未涨帧就撤回已验证减少对象分配的有界缓存；仍须继续整帧瓶颈分析。不能
宣称CPU/GPU互等已经排除。两机采集后均新鲜存档并停止，Mali分析期间无
游戏/采集进程。目标30FPS仍未完成。


### 首次 Storage 绑定的动态升级（d8489267）

此前仅按 NVN IMAGE 标记声明 Storage 用途，没有标记的纹理后来作为 Storage
Image 使用会成为无效 Vulkan 用法。这是兼容性缺口，不能用既有营地未触发来
证明通用正确性。此候选已补齐该路径。

首次需要时，沿既有可变格式升级的物理资源替换机制，重建带 Storage 用途的
VkImage，复制所有 aspect/mip/layer（3D 保留各 mip 的完整深度），保留客体
纹理身份。旧图像、视图及相关 Framebuffer 引用经真实提交完成路径退休，
不会在 GPU 尚引用时销毁。Storage 与可变格式升级可在一次替换中完成；
sRGB 基格式不支持 Storage、兼容 UNORM 视图支持时，使用 Vulkan
EXTENDED_USAGE，并以视图实际用途限制 ImageView。

图形或计算操作在生成任一阶段描述符之前，先检查所有图像绑定所需的物理
升级，避免 VS/FS 或同阶段多个绑定获得升级前后的不同图像。直接视图绑定
入口也保留合法用途检查。已具备用途的图像不会重复重建；普通纹理不因
这项修复统一添加 Storage 用途。升级本身采用罕见路径的保守同步，未扩大
普通 draw 的屏障范围。没有修改游戏 binary、shader 数学或玩法。

新增 GPU 用例包含 IMAGE 提示开启/关闭，以及无提示 sRGB + UNORM Storage
视图三组：升级前同一提交中的附件清除、缓存 Framebuffer、两个别名 Storage
绑定、compute RAW/WAW、transfer WAR、未触及 mip/layer、完成回调后再次绑定
Storage 和新附件。验证全部子资源的预期像素。Mali 的 sRGB 基格式本身支持
Storage；Adreno735 该路径实际使用了 EXTENDED_USAGE（日志 flags=0x108）。

宿主普通/完整同步参考分别 554485/545813 项通过；Mali 555266、Adreno735
554490 项真实 GPU 检查通过，均启用同步验证且无 VUID/SYNC-HAZARD。Mali
实际原游戏2772×1240、固定3D1进入营地，零同步错误，已原生命周期存档并
停止。APK d8489267ae16ef79cfc8eae8c95827d539639fef7fe022284c43cb2cd2df1550，
250项源码指纹已核对。报告 `.local/logs/storage-promotion-*-validation.json`。
此项首先是正确性补齐，不将它表述为已测得帧率收益。

随后不加载 GPU Layer 的普通 Mali 运行，官方 SurfaceFlinger 记录507帧、25.344FPS；
仍为2772×1240、固定3D1，无冻结/外推。与历史独立窗口不能当作严格涨帧对照。
记录 `.local/logs/storage-promotion-mali-ordinary-timestats.txt`。PID15174已原生命周期
存档（commits2）并停止，30FPS目标仍未达到。


### 图像布局与真实资源依赖合并核对（2026-10-17 夜间）

原优化布局实验每次转换后调用 image_unknown，把实际资源历史扩大成
ALL_COMMANDS 写入。该实验原本默认关闭，不能据此解释此前 GENERAL 模式
的25FPS。新实现保留真实读者/写者，另记前次转换的目标同步范围，以执行链
保证转换顺序；转换前收集当前图像的读写阶段，目标按颜色、片元读取或传输
指定。转换的第一个实际使用可消费已建立的完整顺序证明，任何实际访问后
该证明失效。不是将布局操作伪称某个 shader 的真实写入。

首候选 f02e7620：宿主554486/GENERAL554485/完整参考545814，Mali555267、
Adreno735554491及真实Mali营地2772×1240同步校验通过。官方SDP API capture10
（PID30760）1186467条API、35次Present、6035次Barrier，source/destination
ALL_COMMANDS均0。该捕获在下一候选之前，不能混称最新实现证据。
`.local/profiles/layout-access-735-api.sdp` 与
`.local/logs/layout-access-735-api-summary.json` 保存原始会话/统计。

但同一进程原厂ABBA中 GENERAL物理pass约53.1/帧，优化布局约63.1/帧；
优化模式23.52/23.34FPS，GENERAL25.19/24.52FPS。新片元绑定的显式布局转换
无条件结束当前通道，抵消了 Transaction Elimination 的收益。因此继续修复
这个附加成本，没有把该实验当作默认涨帧优化。

第二候选 d077044d：为每张图像记录真实访问时钟。只读片元首次采样、图像
自当前通道开始以来没有实际使用且 primary 尚未 BeginRenderPass 时，将
转换补录到该 primary 的通道开始前；其余转换先正常结束通道。新增真实GPU
用例先以附件生产纹理，再在下一通道第二次draw首次采样，两次提交都必须
只有两个物理pass，并检查两个像素区域。保留原同通道反馈/别名/部分写覆盖。
宿主555000/GENERAL554999/完整参考546328，Mali555781、Adreno735555005，
真实原生Mali营地同步零错误。

该候选原厂ABBA均值：GENERAL25.2694FPS/53.0725pass，优化25.0759FPS/
53.0638pass。未变tile消除率约0→16.04%；PA外部写入201.799→196.447MB/帧
（-2.65%），总外存515.804→510.948MB/帧（-0.94%）。不能声称已涨帧或
已经达到30FPS；访存收益与整帧收益分别记录。Perfetto前三窗GPU/上限固定
848MHz，最后窗包含短暂383MHz。原始和汇总为
`.local/profiles/layout-prepass-*.apc`、`.local/profiles/layout-prepass-*.pftrace`、
`.local/logs/layout-prepass-official-abba-summary.json`。

第三候选731fccff：只读纹理拷贝/回读/出图源使用TRANSFER_SRC_OPTIMAL，
避免无必要的GENERAL；写目标、同图像自拷贝及复杂图像仍保守。出图和局部
裁剪路径传递真实布局，未删去裁剪边缘处理。宿主555000/GENERAL554999/
完整参考546328，Mali555781、Adreno735555005，实际Mali营地同步零错误。
原厂最后GENERAL窗被稳定限频383MHz，只有14.15425FPS，前三窗848MHz；
因此拒绝将四窗汇总的表面26%差异算作FPS提升。
`.local/logs/layout-source-official-abba-summary.json` 已明确标注FPS对照拒收。
前三窗分别25.5656/25.0670/25.0530FPS，继续约25FPS。后续改用短成对窗口，
结束即原生命周期存档停机，冷却后再做反向对照。

当前第四候选进一步只按实际Storage绑定要求GENERAL，而非把具备Storage
能力等同于当前正在执行Storage操作。保留NVN IMAGE提示对应的创建用途；
整个操作的Storage预检查先按真实shader阶段转GENERAL并锁定本提交，所有
阶段随后才构造描述符。新增sampled-first与Storage别名的实际GPU用例：
同一图像从附件生产，采样左半区并写右半区，同时检查源图两半和输出图。
没有移除必需的Storage用途、修改游戏binary或放宽数据精度。此候选验收待记。


da4dcc47后续已通过宿主557050/GENERAL557049/完整参考548378，Mali557831、
Adreno735557055，以及真实Mali营地2772×1240固定3D1同步校验零错误。
新增control34纯GLSL采样/Storage别名夹具与生成器、各文件/归档哈希记录在
`.local/logs/layout-storage-fixture-provenance.json`。没有修改实际游戏shader缓存。

改用同进程短A/B两窗，Perfetto两窗GPU频率及上限均848MHz。原厂CSV为
GENERAL25.25425FPS/53.065pass，优化24.879125FPS/53.11625pass；不能宣称提速。
PA外部写202.297→195.668MB/帧（-3.28%），总外存516.651→511.281MB/帧
（-1.04%），未变tile消除率0→16.41%。通用层确实减少了写流量，尚不能解决
当前帧率。Perfetto翻译墙钟分别29.797/31.616ms，不能据此独断为GPU独占成本，
当前在用已有官方CPU采样导出符号函数表核对其来源，没有新增手机采集或自写计时器。
汇总 `.local/logs/layout-storage-access-official-pair-summary.json`。PID25300采集后
原生命周期存档commits2并停止；高通测试进程亦已退出。优化布局仍使用既有
显式实验开关，GENERAL默认保持；后续须压低其附加命令/CPU成本，不能仅凭
节省写入就把整帧退化算成涨帧。30FPS目标仍未完成。


#### CPU时间归因修正及默认策略

后续使用Perfetto原有sched_slice与cpufreq的SPAN_JOIN核对，发现同一个实际TID
在本次导入中有两个UTID/进程归属；仅按UTID连接会错误返回零调度样本。按捕获中
稳定的实际TID连接后，GENERAL/优化模式每帧CPU运行时间为28.550/30.498ms，
按运行区间频率估算的周期为52.825/51.459M（后一窗频率覆盖30.439ms）。加权
运行频率约1.85/1.69GHz。因此不能把前述约1.8ms翻译墙钟差直接归为布局代码成本。
这是调度时间×频率估算，不是PMU指令数，亦不是GPU执行时间。导入存在已记录的
vendor systrace_parse_failure；不能据此虚构某个API的独占成本。

已有Arm采样附加精确APK/驱动/libc符号后，两个8秒窗仅827/861个leaf样本，
image_layout_access分别4/4个样本；不足以将总差归因为该函数或特定driverAPI。
原始与汇总 `.local/logs/layout-storage-access-*-functions/`、
`layout-storage-access-cpu-leaf-comparison.json`、`*-replay-cpu.csv`、
`*-replay-cycles.csv`。符号副本保留输入哈希，未重新启动游戏或增加手机采样。

当前版本重新检查原有公共ADPF性能提示（先前测试为约12FPS的旧GPU瓶颈阶段）：
同一da4/PID27325，两窗GPU及上限848MHz，接口active、零错误、target16.667ms，
但官方帧率25.6874→25.1278FPS，没有证明提速或稳定CPU供给。CPU频率加权约
1.93→1.71GHz。接口调用成功不等于有效boost，仍保留可选、默认关闭。
`.local/logs/current-adpf-official-pair-summary.json`。PID27325已原生命周期保存并停止。

布局路径的默认策略改为启用精细COLOR/read-only/TRANSFER_SRC布局，保留
`--general-image-layouts-reference`与原有环境/Intent参考入口。保留理由是已确认的
外存写工作量下降和完整GPU/场景校验，没有证明明显CPU周期成本退化；不因当前
瓶颈掩盖了FPS收益就长期关闭。不能把此默认策略变更宣称为已达到30FPS。
Java直接启动、Vulkan缺省与启动工具保持一致；复杂图像和实际Storage/反馈访问
继续GENERAL，不改原生分辨率、固定3D1、帧速或玩法。新默认APK验证另记。


9a6b02be默认版本已通过宿主557050、显式GENERAL557049、完整同步参考548378、
Mali557831、Adreno735557055项GPU检查。新增默认使用普通启动路径验收，未加载
GPU Layer：Mali原生2772×1240/固定3D1，SurfaceFlinger505帧25.249FPS；高通
原生2750×1260/固定3D1，379帧18.302FPS。游戏tick分别25.0123/24.9916Hz，
没有冻结、外推、降低分辨率或降低原目标帧率。各自产生最新原生命周期存档
commits2并停止进程。报告 `.local/logs/optimal-default-ordinary-summary.json`。
这些是当前实际运行数字，不声称历史独立窗口差异为净涨帧，也没有完成30FPS目标。
当前9a6真实营地为普通非validation运行；同一布局算法的真实营地同步校验属于
前一da4构建，不能把不同产物的验证方式混称。默认策略变化没有更改该算法。

### 资源声明、上传和逐 mip 布局补充（2026-10-18）

用户提供的其他实现及数字仅是候选方向，不作为本仓库的实现状态或收益证据。
本仓库现有对应关系如下；对方的函数名不是本项目接口。

| 方向 | 当前实现和限制 |
| --- | --- |
| Dirty Page | `nvn_dirty.c` 的 mprotect/代数；提交时由 `sync_used_pools()` 仅访问实际需要初始内容的页。 |
| Pool 上传缓存 | 每页 `page_upload_stamps`，跳过未改页；脏页与 shadow 比较，只上传变化的四字节字段连续段。上传集中在提交前，不在每次描述符绑定时重复扫描。 |
| Cached Writeback | `temp_readback()`、`vk_memory_buffer_readback()` 优先 HOST_CACHED；完成 Worker 等真实 Fence、必要时 invalidate，之后发布 guest/shadow，再发布客体事件和完成。 |
| Temporary Pool | 完成后由 `reclaim_submissions()` 复用临时缓冲；上传与读回分型，缓存上限256MiB，仍在途的资源不能复用。 |
| 清屏和 Store | `vk_render_target_clear()`、`vk_pass_store_needed()`、`nvn_vk_cover.inc` 已有 clear 合并、同提交覆盖证明及保守 STORE 回退；不累加估算收益冒充外存总线测量。 |
| 对象与 PSO | 跨提交不可变 RP/FB/View 复用和 Pipeline Cache 持久化已有；后者不等于已有完整 PSO 描述的启动预热。 |

#### 首次实际 Storage 用途

6b158ccbe9498a2f28deda92b2695bd9c85a383747f57d8c56d42f71e3cbd6d2
进一步不因 NVN IMAGE 标记预先声明 STORAGE。标记保留在 NVN 元数据中；实际
Storage 绑定仍由统一 `image_binding_format()` 升级物理存储，保留全部 mip/layer、
GPU 写入和在途视图。原 Metal 已有 LUT 增强也是实际 Storage 消费者，现走相同
升级路径，不能因为物理图像最初没有 STORAGE 而错误关闭该功能。
`--storage-usage-reference` 是进程启动时固定的旧策略对照，环境设置只读一次。
图像用途和视图用途遵循 [VkImageCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageCreateInfo.html)
及 [VkImageViewUsageCreateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageViewUsageCreateInfo.html)。

该构建宿主557393、Mali558174、Adreno735557398检查通过；Mali原生营地同步
validation零错误。官方 Streamline/PA 两个窗口都记录GPU及上限848MHz：

| 量 | IMAGE 提示预留 | 实际绑定时升级 |
| --- | ---: | ---: |
| 工具 FPS |25.2644|25.5686|
| Render Pass/帧 |54.0838|53.0888|
| 外存读 MB/帧（十进制）|323.632|303.188|
| 外存写 MB/帧 |199.781|190.735|
| 外存总 MB/帧 |523.413|493.923|

两个窗口总流量差约−5.63%、FPS差约+1.20%；Pass 数也有差异，尚不能把全部
变化孤立归给 Image Usage，亦不能断言具体硬件压缩模式或稳定涨帧。
`.local/logs/deferred-storage-official-pair-summary.json` 保留这些限制及原始报告。
高通无 Layer 原生2750×1260/固定3D1，374帧18.062FPS；这不是严格对照涨帧结论。
两机均保存原生命周期 commits2 后停止。

#### 提交内完整写入证明和页扫描

后续 a68c0367293824e7add866ad7f91d619a4267103a52045e66139a50f03124d0d：
`pool_gpu_access` 分开记录实际 GPU 使用页、需要初始 CPU 内容的页、此前已完全
覆盖的页。仅精确 Buffer Copy/Update 目的范围可建立完整覆盖证明；在物理命令
录制之后建立，不能在查询地址、结束旧 Render Pass 的嵌套回放之前提前建立。
先前读取已经要求初始内容时，后续覆盖不得撤销这个要求；局部页、未知 Shader
写入、间接可写描述符不推断完整覆盖。

证明只在当前物理提交有效。内部提交重置证明，后续 CPU 写入不会沿用旧证明；
被跳过页不推进上传代数。所有 GPU 访问的阶段/在途依赖照常保留。这个方案省掉
的是确定不需要的初始页内容检查/上传，并不删除 Shader 的真实读依赖、GPU 结果
写回或 Fence。`DM_VULKAN_POOL_OWNERSHIP_REFERENCE=1` 保留完整上传参考。
现有 game-state 记录可补充 `overwrite_skipped_pages` 逻辑页计数；这是工作量
覆盖信息，不是计时器，也不替代原厂 Profile。

`sync_queued_page()` 在相同 publication 锁下先用标准 libc 整页 memcmp 检查
guest/shadow 是否相同；完成 Worker 的写回可能改变脏页代数却不产生新 CPU
内容，不必因此逐字比较1024次。真正不同仍保留原变化字段扫描及等待/上传语义。
已有原厂采样中 `sync_page` 有33/861个 leaf 样本，说明值得检查这条 CPU 路径，
但样本不能证明独占耗时或最终 FPS 收益。

#### 有界逐 mip 布局

单层2D、颜色、单采样、最多16级 mip 按每级记录 layout/transition scope/use clock。
mip0附件与只采样其他 mip 的视图不再视为同一子资源反馈；后绑定另一 mip 的
过渡可在确实无冲突时放在待执行 Pass 之前，避免为过渡多拆一次 Pass。
先对所有实际 Storage 和重叠采样绑定做 preflight，防止后面的 GENERAL 回退
使已经生成的描述符布局失效。混合布局不能用一个假的 GENERAL 对外描述；
原始句柄、整图传输、Storage 升级等消费者先将所需范围统一到真实布局。
数组、深度模板、多采样、超过16级、真正重叠反馈保持保守路径。

新增 GPU 用例同一 Pass 写 mip0、先采样 mip1 再首次绑定 mip2，随后从混合布局
升级 Storage，核对全部像素及再次提交；另有完整写→读、先读→完整写、局部写、
下次提交 CPU 新写入的上传证明用例。宿主优化586765、全GENERAL/Storage提示/
上传参考586764、串行546733通过。两台真机 a68检查分别Mali587546、Adreno735
586770通过，Mali真实营地同步validation零错误。真实性能数据单独采集，不能用
这些测试计数声称已经达到30FPS。

### 2026-10-18：资源声明和 Pool 镜像的后续验证

- ColorAttachment 用途也改成按实际附件使用声明，首次需要时走完整内容升级；
  所有实际颜色目标在本次 draw 的描述符建立前完成升级。sRGB/Storage/多 mip 的
  旧在途引用仍随真实 fence 退休。d6b44cb6 通过 Mali587546/Adreno735586770
  GPU 检查和 Mali 原生营地同步校验。原厂对照约25.40/25.41FPS，通道53/54发生漂移，
  不能宣称该标志有独立整帧收益。保留 `--attachment-usage-reference`。
- Pool 不再注册时就建立整池 GPU Buffer 和 CPU shadow。首次实际 Buffer 使用才创建，
  图像上传仍走原独立路径；并且只初始化首次实际访问到的4KiB逻辑页。初始化位图与
  dirty generations 分开：未初始化页即便 CPU 全零，也必须把真实零写入 GPU；
  页一旦交给 GPU，就继续用既有范围冲突、脏页比较和 fence 后发布规则。
  全池同步参考路径先初始化对应全池，不能对未初始化 shadow 作比较。
  `--eager-pool-mirrors-reference` 保留旧创建与整池初始化策略。
- 当前营地共7池，登记1756364800bytes，仅4池需Buffer、合计1167065088bytes；
  实际初始化约127MiB，不是整个1.09GiB镜像。初始化按位图查找未暴露过的页，
  不增加 GPU 等待，不把 GPU 完成或 CPU dirty 代数当作所有权证明。
- 4d8f7c1e 通过宿主默认595098/旧镜像595098/串行555066检查；
  Mali595879、Adreno735595103检查。新增用例确认纹理池保持无Buffer、
  初次Buffer读取看到新CPU编辑、全零初始数据正确、已写GPU纹理保持。
  原全池同步参考的非阻塞队列用例不适用：它故意全池等待，旧用例要求不等待；
  新增镜像用例已在到达该用例前通过，不能把全套参考检查记作通过。
- 原生营地、固定3D=1、无GPU layer 的SurfaceView记录：Mali24.618FPS，
  PSS3922467KiB；Adreno73518.293FPS，PSS2559783KiB。均原生命周期新存档后停止。
  旧整池镜像同APK对照PSS约6137094KiB（Mali）；进程/场景有漂移，
  PSS差值不能逐字节归因。未达到30FPS，也没有把内存节省写成涨帧。
  逻辑初始化字节和登记字节不等于真实驻留分配或DRAM总线流量。

本机证据：`.local/logs/lazy-pool-pages-native-summary.json`、
`lazy-pool-mirrors-official-pair-summary.json`；可重现APK/ELF/250项源指纹在
`.local/build/{attachment-usage,lazy-pool-mirrors,lazy-pool-pages}-candidate/`。

另做了标准SPIRVTools变换的通用成本筛选：对固定捕获的全部62片元模块，以
官方MOC r56的寄存器/stack/运算成本选择候选，再在手机r32驱动回放。
固定帧RGB相同，但实测LS读/quad增加5.84%，不部署；不是因FPS未涨而放弃。
Uniform静态访问范围收紧也未改变MOC机器成本，保留分析。当前手机正式shader链
仍为Mali122643330366/Adreno25c59aa26fd4，没有按游戏shader键建立优化白名单。

### 单层深度图的专用 Layout（53e05a9d）

先只扩展单层、单 mip、非MSAA的2D深度/模板图像；深度与模板仍联合转换，
不依赖单独aspect layout或maintenance7。真实附件使用EARLY/LATE_FRAGMENT_TESTS，
真实只读采样使用FRAGMENT_SHADER，分别选择DEPTH_STENCIL_ATTACHMENT_OPTIMAL和
DEPTH_STENCIL_READ_ONLY_OPTIMAL。Storage、未知访问、真实反馈和复杂图像仍退回GENERAL。
晚绑定读取只有在全部旧使用都在当前物理通道之前时才允许放到pending primary；
不把转场当作实际shader写入，不丢弃旧在途资源或数据。

宿主默认595355、深度GENERAL参考595355、串行555323；Mali596136/Adreno735595360
检查通过。新增真实绘制用例从深度附件切到独立颜色通道采样，再回到深度附件局部清除；
已有同图反馈继续验证GENERAL。Mali原生营地同步validation零错误、英文画面正常，
注册逻辑计数表明确实命中深度附件和只读采样；Adreno735普通运行18.238FPS。
`--general-depth-layouts-reference`只切这项，保留既有颜色Layout优化。

原厂Streamline/PA首轮同APK对照：848MHz/848MHz有效上限均固定，
RenderPass53.066/53.080每帧；深度GENERAL/专用Layout分别24.853/25.0715FPS；
外存总流量493930280→481132798bytes/frame（−2.59%），写入190496876→182065144
（−4.43%）。CPU replay wall约30.496/30.501ms，重叠区间不能与GPU相加。
这是单对窗口，天气/场景仍有漂移，不宣称稳定整帧提速或30FPS；
GPU带宽减少方向有依据，保留默认路径和独立参考。证据
`.local/logs/depth-optimal-official-pair-summary.json`及对应原厂apc/Perfetto。

选择深度只读Layout遵循[Vulkan布局规范](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageLayout.html)，
与[Arm对深度只读布局的建议](https://armkeil.blob.core.windows.net/developer/Arm%20Developer%20Community/PDF/Arm%20Mali%20GPU%20Best%20Practices.pdf)一致；
建议本身不是本项目性能收益的证据。

### Compute 的只读采样布局（947f36b2）

只读compute采样也可选择SHADER_READ_ONLY_OPTIMAL，深度图选择联合深度/模板只读布局，
同步目标阶段为真实COMPUTE_SHADER。Dispatch没有framebuffer附件，不能把之前留下的
S.targets当作同操作的反馈；真正Storage绑定仍在全部描述符建立之前把物理图像固定到
GENERAL，复杂图像、未知/顶点阶段和同图读写保守路径不变。
`--general-compute-layouts-reference`独立保留原compute布局策略。

新增GPU测试用例：前一提交GPU清除后采样、同提交重新清除后的GENERAL回退、
后续提交重新使用只读布局，三个结果均由compute写Buffer并读回确认，不仅检查调用数。
宿主默认595556、参考595556、串行555524；Mali596337、Adreno735595561检查通过。
Mali真实英文营地同步validation零错误，实际命中只读compute布局。
原厂同APK首对：25.44025/25.42475FPS，53.050/53.0475passes/frame，
总外存483376632→476834375bytes/frame（−1.35%），写入−2.02%；
单对小幅变化不作为稳定涨帧，保留已明确的资源布局改进。Adreno735普通运行18.083FPS。
报告为 `.local/logs/compute-optimal-official-pair-summary.json`。两机原游戏均新存档后退出。

### 只读深度附件（db0d9e62）

复用现有命令展开/逆扫，增加后续深度/模板写入证明。只有整段可证明没有写入、
当前实际管线不写深度且不开模板写入时，才把深度测试附件放进联合只读Layout。
颜色清除可保留只读深度兄弟附件；深度清除或后续真实写入仍升级至可写Layout，
先结束旧物理通道。已经建立的可写通道不因后来只读就再拆开。原通道覆盖/数据都保留，
未知或原专用回放变换继续使用可写路径。`--writable-depth-attachments-reference`独立对照。

GPU测试增加真实深度测试后独立采样验证未改深度，以及相同目标重绑定后出现写入的
正确升级。宿主默认/参考595557、串行555525；Mali596338、Adreno735595562检查通过；
Mali原生营地同步validation零错误，真实readonly_attachment计数有命中。
Adreno735普通运行18.302FPS，原生命周期新存档后停止。

原厂单对：可写参考25.15775、只读25.166625FPS；53.0825/53.025passes/frame，
总外存513874691→496584331bytes/frame（−3.36%）、写入−5.01%。不是受控稳定涨帧；
参考捕获排在候选之后，天气/热状态仍可变，tile写入/像素反而+2.37%，不能把所有差值
归给此优化。GPU主瓶颈未解决，保留正确的只读资源声明。

首次参考进程28064在工具开始采集前SIGSEGV，Android记EXIT_SELF/status139，
地址落在其保存maps中的PROT_NONE匿名区；没有有效参考性能数据。旧本地编排未在
启动下一组前存最后stderr，故缺该次PC/调用栈，不能擅自归因。已修正失败归档流程，
参考补采29862成功；缺最后栈的单次崩溃仍是未归因风险。证据
`readonly-depth-reference-capture-failure.json`及PID logcat/maps；性能报告只采用
`writable-reference-retry1`和成功候选，未把失败写成通过。


### 现有工具数据的等待重叠与片元颜色隔离（2026-10-18）

直接查询已采集的 db0d9e62 原厂配套 Perfetto，不加新计时器：成功候选392段
NVN replay 的平均 wall29.859ms、实际被调度运行28.861ms；主线程396段 guest fence
等待平均18.268ms，其中10.258ms与翻译重叠，余8.010ms不与replay重叠。
可写深度参考重采也有同样结构（19.235/10.756/8.479ms）。
没有记录到翻译线程的资源上限/上传冲突等待，但不能因此断言同步不是主瓶颈；
主线程等待可包含真正GPU依赖、翻译尾部和完成后的CPU发布，不是纯GPU执行时间。
队列有工作也不证明流水线没有停顿。查询输出在
`readonly-depth-*-wait-summary.csv`、`readonly-depth-*-wait-overlap.csv`。
原trace有vendor文本systrace解析失败，保留该限制；上述固定名称的完整应用区间存在，
按真实TID连接sched_slice，避免同一线程在trace中被分配多个utid而漏算。

在历史固定捕获上另做了诊断性shader隔离：53个片元模块只移除RGB输出计算，
保留模块内alpha、discard、深度/模板输出、storage副作用与捕获的几何。
官方Arm replayer + Streamline硬件计数器按rasterized fine quad归一比较：
LS L2读−88.32%、LS other写−96.06%、GPU active周期−30.45%、
execution core周期−46.84%、纹理L2读−58.63%。
这支持大量LS成本随片元颜色计算出现，不能将其全部解释为回读或搬运，
也仍不能区分Uniform读取和编译器临时数据溢出的比例。
后续依赖颜色的pass工作可变化，quad归一也不是排他时间；故这不是严格保持所有GPU工作
相同的实验。画面故意失去颜色，绝不能作为优化部署或native FPS成果。
正式游戏shader缓存未改，replayer已停止、设置已还原。报告
`.local/logs/color-isolation-official-pair-comparison.json`保留全部原计数器及边界。

对五个各有5个循环的热点模块，用标准SPIRVTools探测loop fission；
与相同规范化基线相比，五个模块的二进制完全不变，故未为它重复真机采集。
branch-to-select改变部分SPIRV，但没有一致降低MOC成本，不部署。
当前MOC为r56、手机为r32，MOC预测不等于实际驱动结果；历史编译器官方下载目录
未认证请求403，未获得旧版本。现有手机硬件计数器仍可使用，不以工具版本猜测收益。


### 依赖索引 CPU 成本复核与 Shader 精度实验

已记录主线程约18ms的guest fence等待后，继续查询原厂CPU trace中的实际调度与频率。
尝试缓存每个阶段的完整descriptor解析结果，保留每个原读取事件：v1 e40b66c9、
v2 033b6ba4均通过宿主595603/串行555571、Mali596384/Adreno735595608检查，
v1原生Mali营地同步validation零错误，英文画面已查看正常；每轮原生命周期保存后退出。
新增换绑、相同绑定、解绑及在前一Draw之后查询的用例，避免旧读取掩盖缓存失效错误。

但是该缓存没有通过净CPU成本复核：同为2353draws/frame，原db0d索引运行约1.020ms、
按CPU频率加权估算1.876Mcycles；v1为1.143ms/2.167M，v2为1.101ms/2.142M。
v2已把重建降至约1083stage/frame，descriptor查找也少约4150/frame，仍有额外解析列表
与失效维护成本。整段replay估算cycles约55.77→55.24→54.45M，不能因此把局部增加隐去，
也不能将不同采集窗口的小幅FPS变化当成稳定收益。完整缓存保留在本机构建档案，
当前实现改为仅在shader加载时预分类实际图片读取slot；动态句柄仍逐读取解析，
继续使用原descriptor代数检查与整提交索引回退，去掉阶段解析列表的重建/失效成本。

另用标准SPIRVTools规范化后，实验将整数Phi/Select/Copy中的浮点位传递改成等价浮点传递，
没有改变运算/控制/内存操作；在固定捕获的最终图像中与原精确shader逐像素一致。
在此基础上通过CFG后支配与控制依赖分析，尝试只放宽颜色算术：坐标、图像/导数执行上下文、
alpha、深度、存储写入、discard和NoContraction及其数据/控制前驱仍保护。
9个小shader分别验证颜色分支、discard、alpha/alpha-Phi、坐标Phi、条件采样、
storage写入、循环坐标与循环颜色；只放宽两个颜色用例中的门控算术。
全部62个捕获片元模块按同一策略检查，48个增加精度注解；未使用游戏shader键白名单。

单张营地候选与配套参考平均通道差0.522/255，约9460/3437280像素有任一通道差>8，
两图肉眼已检查未见大块黑/几何丢失，但这不证明角色选择/洞穴或其他材质正确。
官方Streamline配对的LS L2读/quad反而+29.11%、LS other写+10.61%、
GPU active周期+1.12%、execution core周期+2.26%、CVT指令+7.57%。
部分LS full访问减少不能抵消这些增加，不升级为正式优化。
参考10.896/candidate11.444的replayer FPS不能冒充native FPS或排他GPU成本。
精确传递规范化单独与更早原版捕获的交叉窗口比较也出现LS增加，尚无采纳依据。
正式游戏shader缓存仍为122643330366/25c59aa26fd4。

本机证据：`store-stage-cache{-v2}-deferred-{scheduler-analysis.json,store-index-cycles.csv}`、
`color-control-policy-fixture/results.json`、`color-control-replay-image-comparison.json`、
`float-transfer-normalization-replay-image-comparison.json`、
`color-control-official-pair-comparison.json`。新精度/传递实现目前只在.local中实验，
没有加入正式离线加工工具链。

官方gfxrecon的Uniform资源导出还遇到宿主lavapipe重放中的内存破坏；GDB栈已保留，
不将该工具崩溃当成手机游戏崩溃。工具在失败前写出的两个Draw资源报告只用于静态可行性
线索：固定Uniform掩码的离线替换没有显示值得新增动态特化系统的明确收益，未部署。
此外系统vkjson确认此Mali的物理subgroupSize为16；目前缓存标签32在上游翻译器所用
的<=32分支上生成相同策略，不能把标签当成实测硬件宽度。扩展Float16特性有重复别名字段
不一致，若做显式半精度，须以真正features2查询/启用与验证为准；当前设备未启用该特性，
不能直接部署带Float16 capability的shader。

### 2026-10-18：静态槽位分类与显式半精度验证

当前可复现 APK 为 `a836e3f51ba489c01675bccc0243dc1cef6ca598c8bbebbd9a0481e06e12543d`；对应 ELF、构建记录与全部 250 个源码输入副本保存在 `.local/build/float16-arithmetic-candidate/`。正式 Mali / Adreno 着色器缓存保持原样。

依赖分析最终采用加载着色器时生成不可变 Image 槽位表：保留每次实际读取位置上的描述符解析和资源顺序，不再逐次检查无关的 Uniform / Sampler / Buffer 绑定类型。每次原游戏提交约省去 16,389 次绑定分类；已有 Perfetto 调度与频率数据估算的该索引段成本从 1.876M 降至 1.757M cycles，约减少 6.37%。这是频率加权估计，并非硬件 retired-cycle 计数。之前的动态阶段缓存虽然省了哈希查找，却增加了实际 CPU 工作，已由此方案替换。

真机公开能力查询后，按条件启用 `VK_KHR_shader_float16_int8` 的 `shaderFloat16`，不启用 Int8 或 16-bit 存储接口。新增精确可表示输入的 GPU 用例验证显式 FP16 算术以及两轮 CPU 更新；Host 595,739、串行 555,707、Mali 596,520、Adreno 735 595,744 项检查通过。Mali 原游戏营地同步验证错误 0，原生输出 2772×1240、固定 3D 比例 1。能力支持不等于正式着色器已降精度。

离线捕获上另外测试了标准 SPIR-V 显式半精度转换。用官方 GFXReconstruct 解码验证：工具测试捕获只补充 CreateDevice 的 Float16 特性与扩展声明，其余 API / 资源不变；这是诊断捕获适配，不是游戏 binary 修改。官方 Streamline 单对照、按 rasterized quad 归一结果：LS L2 读取 +59.76%，执行核心活动周期 +6.51%，转换指令 +7.92%。因此这套半精度策略没有部署。该结果不能解释为所有 FP16 都无效，也不能用新版离线编译器成本替代手机 r32 驱动实测。

最新营地结果：Mali 约 25.37 FPS（0f8 槽位分类版本），Adreno 735 约 18.03 FPS（a836 普通运行）；均为屏幕原生输出、固定比例 1，不是离线重放帧率。尚未达到 30 FPS，独立短窗口不构成稳定涨帧证明。证据分别见 `.local/logs/store-slot-classification-summary.json`、`.local/logs/float16-arithmetic-realgame-validation-result.json`、`.local/logs/color-explicit-half-official-pair-comparison.json`。

### 2026-10-18：高通 Snapshot 实际编译统计与绘制状态复用

Snapdragon Profiler 2026.9 的 Snapshot 已实际跑通，不能再把手册里的“secondary command buffer 逐 Draw metrics 暂不支持”扩大成“Shader Analyzer 不可用”。当前营地取得 243 条阶段编译统计，能够查看指令数、寄存器、理论驻留率、scratch。111 个原始模块中的可关联阶段 SPIR-V 已按 SHA-256 与正式缓存匹配；部分较重片元程序为 4,000～5,101 条指令、50% 理论驻留率、scratch=0。它们是驱动编译的静态统计，不是实际执行次数或独占 Draw 时间，不能据此推断 Mali 同样不溢出。工具原始 renderer 字符串为 Adreno 726，与 Vulkan 查询的 Adreno 735 名称不同，保留两者原始记录，不擅自修正厂商标签。

完整工具会话：`.local/profiles/float16-capacity-735-snapshot.sdp`；整理结果：`.local/logs/sdp-shader-snapshot-735-all-stats.json`。此前只查询旧导出数据库、看到 ShaderData 为空，不能证明工具缺少这项能力。实际 Snapshot 和面板选择已经确认了入口。

工具明确指出重复动态状态与 Descriptor Set 绑定。后端现按每个 secondary command buffer 保存真正已发出的 Pipeline、Viewport、Scissor、Depth Bias；新 recording 不继承状态，发生变化时重新设置。连续编号的活动 Descriptor Set 合成一次绑定，缺口保持原行为，不合并描述符内容或跳过资源依赖。缓存基于最终 Vulkan 值，不以 NVN 命令是否重复作为正确性依据。

产物 `772da9dc712530cf1438ac37e407755add1d3f648f32c9d2afe2a7e9f936f9bb` 及全部 250 个构建输入副本位于 `.local/build/draw-state-reuse-candidate/`。Host 默认/参考各 595,739 检查，Mali 596,520，Adreno 735 595,744；Mali 原营地同步验证错误 0，两机原生画面已实际查看。新增参考环境变量 `DM_VULKAN_DRAW_STATE_REFERENCE` 仅首次读取，默认开启复用。

两个官方 Snapshot 的已解码 API 记录按录制 Draw 数量归一后：Viewport / Depth Bias 调用减少 97.23%，Scissor 减少 97.04%，Pipeline 绑定减少 80.80%，Descriptor Set 绑定减少 49.88%。记录也包含捕获时保留的命令缓冲，不能当作实际执行次数。公开 GFXReconstruct converter 遇到尾部高通自定义压缩 metadata 后报不支持，故这份有效 JSON 前缀仅用于 API 记录核对，不声称是完整捕获验证；原厂完整 SDP 与数据库均已保留。证据 `.local/logs/draw-state-reuse-sdp-recorded-api-counts.json`。

当前短窗口 Mali 25.77 FPS、735 18.27 FPS，仍未达到 30；Perfetto 中翻译段运行时间 27.15 ms，频率加权成本 54.72M cycles。相较此前窗口的小变化不构成稳定整帧收益证明。735 另试现有 Android Performance Hint：v2 可用、会话活跃、错误 0，但 18.08 FPS，仍保留为可选关闭，不强制频率或温控。

另一个严格隔离的诊断将两份已导出 Uniform 字冻结，估计分支特化上限。它通过 SPIR-V 校验，但在 Mali 官方重放进程首次绘制时触发驱动空指针崩溃（PID 16092，`libGLES_mali.so` PC `0x16b167c`，地址 `0x50`）。已经保存系统 backtrace、退出信息并停止工具观察任务；没有性能结果，也未改动正式游戏缓存。它与旧的原游戏参考 PID 28064 未归因崩溃是不同事件。离线 MOC 成本下降不能替代真机证据，不能据此实施有错误输入或未证明所有权的运行时常量特化。

### 2026-10-18：描述符内容、绑定状态与查询对象

`ffd6ebdc42956eaca4d335e4783d1e302bc6b59de3a9d8b4ba97d76b95055227` 在每份提交上下文中记录各着色器阶段最后一组完整描述符内容。每次仍执行原资源解析、图片布局处理、缓冲依赖、Storage Bias 和写回登记，最后才比较 Buffer/Offset/Range、ImageView/Sampler/Layout 或 BufferView；一致时复用原不可变集合。GPU 完成、上下文回收后与 Descriptor Pool 一起清空。没有跨未完成上下文共用可变集合。Mali / 735 稳态分别命中约 51.17% / 51.15%，省去对应分配与更新。Host 默认/参考均 595,739，Mali 596,520、735 595,744 检查通过，Mali 原营地同步验证错误 0。

`8ce5dda8570e28893628d0c6ffc9ec5b45f8f357626e24786274d75cfd5c5fe8` 进一步按 secondary 自己实际发出的状态，跳过相同布局下相同 Descriptor Set 的重复绑定，以及相同布局和内容的 128 字节 Storage Bias push。布局切换保守重绑，新的命令缓冲完整设置，不跳过资源依赖或内存更新。相同检查和 Mali 原游戏验证通过，两机营地画面保持正常。Mali 新窗口 25.57 FPS，翻译段调度运行 28.12 ms / 频率加权 54.17M cycles；735 普通运行约 18.54 FPS。独立短窗口不能证明稳定涨帧。

735 新 Perfetto 追踪中，翻译段运行约 36.85 ms / 63.65M 频率加权 cycles，wall 40.07 ms，主要运行于大核；即使 GPU 完全不再等待，这段实际 CPU 占用也已超出 30 FPS 的 33.3 ms 预算。这里的“CPU 占用”也包括驱动内部运行，不能当成全是兼容层有用计算。证据 `.local/profiles/descriptor-binding-state-735-scheduler.pftrace`、`.local/logs/descriptor-binding-state-735-replay-cycles.csv`。trace 中厂商 priority 字段有打包值，不把它直接解释成 Linux nice 数字。

已有 Snapdragon API Trace 10 的 CPU 时间单位为微秒：约两秒采集中 Query Pool 创建/销毁合计 33.53 ms，约每帧 1 ms；BufferView 创建 23,721 次、13.22 ms。代码核对确认这些路径仍每次创建后回收，故后续候选按真实引用归零点回收两类 Query Pool，并按完整 Buffer/Format/Offset/Range 在当前提交内复用 BufferView。查询仍在新的原命令位置 reset，计数前缀、真实 GPU 完成与 CPU 结果发布规则不变。BufferView 的 GPU 读/写依赖仍每次登记。该后续候选验收另记，不能把源码完成当成真机收益。

两项离线 shader 对照没有部署：Uniform 数组声明收缩（91 个模块、305 个数组）画面完全一致，但实际计数器基本持平；只折叠后端初始化后永不改变的 Support Buffer 标量（30 个模块、79 次静态读取）也画面完全一致，FMA 指令约减少 0.65%，LS L2 读取反增约 4.28%，没有足够净收益证据。它们没有改动任意游戏 Uniform，也没有修改正式缓存。见 `.local/logs/uniform-bounds-official-pair-comparison.json`、`.local/logs/support-uniform-official-pair-comparison.json`。

另新增默认关闭的 `DMSH_VK_PROTECTED_COLOR_HALF=1` 实验编译选项，用于 Adreno 735 的独立评估；Mali 之前该类策略增加 LS 工作，不能泛化部署。直接 SPIR-V 的 Alpha/Depth/Storage/Discard/纹理坐标、导数和相应控制依赖保守保护，RGB 算术通过标准工具转为显式 FP16，接口保持 32 位。**这是局部数据流证明，不保证附件 RGB 的所有后续跨 Pass 用途都是显示颜色**；HDR 溢出、下溢及数据附件误差仍须真机视觉和功能检查。九种控制/数据依赖合成场景与显式 FP16 输出校验通过；完整缓存 `vk-b2d4d462f1fd` 已从原入口重建、6,931/6,931 验证及完整产物检查通过，尚未部署。编译选项、变换源码和工具指纹均进入新链，旧正式缓存未改写。


### 2026-10-18：Query Pool / Texel View 复用真机结果与精度实验

`e54f1fa70d97` 已通过主机 595739、Mali 596520、Adreno 735 595744 项真实 GPU 夹具检查。Mali 原游戏角色选择与营地验证无新增 Validation 错误，原生 2772×1240；新高通原生 2750×1260。Mali 稳态 452 次 guest submit 内 Query Pool 回收命中 1356、创建 0；BufferView 复用 305150、创建 904。资源减少已经确认，整帧仍约 24.71 FPS；高通普通运行约 18.43 FPS。独立 Mali 窗口按 Fine Quads 归一化后部分总线/队列指标升高，场景混合与频率不受控，不能据此排除 GPU 成本回归；保留后续同负载对照要求。

Adreno 的保护性 RGB 半精度缓存 `b2d4d462f1fd` 完成 6931 项离线验证后，仅临时部署该设备进行真实原生测试。角色选择正常，但营地出现明显红黑地面色块，约 19.01 FPS 不能作为有效性能收益。已在原进程完成原生命周期存档后停止，使用历史完整哈希清单核验 20792 个文件并恢复 `25c59aa26fd4`。实验目录保留供分析，默认选项仍关闭。局部 Alpha/深度/Storage/Discard/坐标保护不能证明跨 Pass RGB 数据的范围与精度要求；离线 SPIR-V 验证不等于视觉正确。

Mali 的局部 Phi/Select/Composite 半精度连续区域实验只在官方重放器执行：每 Fine Quad 的 LS L2 Read +11.20%、CVT 指令 +6.83%、Core Active +2.15%。没有部署到游戏。与此前算术半精度实验相比收窄了退化，但仍没有成立的资源收益。

下一候选是宿主 c0/c2 专用常量区：每个 64 KiB 槽只初始化一次完整零区，此后每次复用只重写 0xa00 字节宿主表。区段属于 Submission Context，真实 GPU 完成后才能重用；Uniform-only VkBuffer 不允许通用 staging 或 GPU 写入。保持 64 KiB descriptor range、对齐偏移、不可变快照及全零尾部，避免依靠 Robust Buffer Access 或缩短范围改变语义。每块 8 槽，额外保留空间上限为每活跃 Context 不足 512 KiB；首次新增槽仍有一次 64 KiB 清零，稳定后减少重复清零与临时对象链表搜索。主机正反两路径均通过 595739 项检查，新增用例覆盖跨块、多帧回收、A/B/A 不可变内容及末尾 vec4 全零。真机数据待补。


常量区第一版 `3591ad8efd20`：Mali 主机/真机夹具与原游戏验证均通过；原生官方窗口平均 25.34 FPS，Perfetto 401 次 replay 平均实际运行 27.755 ms / 51.761 百万频率加权周期。前一版 e54 独立窗口为 28.523 ms / 54.114 百万周期；这不是受控 FPS 因果对照。高通 20 秒中分配 56752 份常量，仅 129 份发生首次全区清零，少清零 3,710,844,928 字节，FPS 18.379，CPU 周期基本不变。PSS/RSS 比前次高，不能忽略新缓存保留成本。

因此第二版 `dd5f74a6ee17` 将已经完成的常量块放入共享池，在不同 Submission Context 之间复用；不再按每个 Context 永久保留启动时的最高数量。空闲最多 32 个块，通常为 16 MiB，超出即回收；在途块不参与该上限，必须等待真实完成。新增 live_bytes 和 chunk_reuses 计数，区分累计创建和当前持有。两台 GPU 夹具均通过，原生对照采集中。

下一候选来自官方 SDP GFXR 的 API 检查（`.local/logs/sdp-unused-color-attachments.json`）：若干只有清屏、没有 Draw 的多附件 Pass，仍对未清除的颜色附件执行 LOAD/STORE。绑定管线的颜色写掩码没有显示普遍可省掉的绘制附件，因此不改正常绘制目标。针对单独清屏，通过最多 256 条原 NVN 命令前瞻，在同提交内遇到绘制/其他清屏则保持合并；只在证实先遇到目标切换/传输/计算或提交末尾时，创建仅含所清附件的普通 loadOp=CLEAR Render Pass。没有改用 Transfer Clear，没有丢弃其余附件，也不改上层 NVN target state；已有打开的 Pass 继续合并。减少的是未触及附件的逻辑 load/store 和伪依赖，实际总线收益须用硬件计数器验证。主机 601890 项检查通过，新增回读覆盖双颜色+Depth/Stencil、局部矩形、初始化清屏合并及关闭优化的参照路径。真机测试待完成。


清屏子集 `3072bdb87d09` 的真机/原游戏验证已完成：Mali 602671、Adreno 601895 项夹具检查；Mali 原生角色选择、营地截图正常，同步 Validation 0 错误。增加专用开关，可在同一原游戏进程执行 A/B/B/A，避免重启场景与缓存状态变化。Mali 参照两窗平均 25.172 FPS、候选 24.949 FPS；高通参照 18.394/18.542、候选 18.485/18.551。每 guest submit 省约两个附件，Render Pass 数均约 53.04；但按 Fine Quad 归一化的外部 Read/Write 仅 -0.030%/-0.022%，没有显著硬件带宽收益。保留准确的附件声明与逻辑操作削减，不把它计为涨帧或 DRAM 节省；GPU queued/active +0.934% 也不能解释成纯 Shader 时间。现场时间、频率、温度仍在变化。报告 `.local/logs/clear-only-abba-comparison.json`。

新候选：重复纹理脏页否定结果缓存。原先每个已经上传的 packaged texture 绑定都再次检查原上传 stamp；同池其他页面一旦改过，其 range.latest 不再帮这张长期不变纹理跳过逐页扫描。缓存键是该 image 的原上传 stamp 与“完成发布序列”。`g_changes` 在每次页代号发布和池登记/移除后单独递增，不使用可能先被并发写者预定的 g_now，也不推进上传 stamp。任何池写入都保守失效；检查前捕获序列，检查中发生的发布会使下次重查。仅确证 nvn_dirty_since=false 时缓存；未跟踪地址不缓存。缓存不改变 mprotect、GPU 写回或原页代号协议。

逻辑命中计数由单一录制线程累计、每次提交批量发布，避免逐绑定新增原子 RMW；没有新增计时器/采样器。主机正反路径 605480/605479 检查通过，包括真实写缺页、宿主写前准备、另一张纹理修改、移除跟踪后的连续无代号变化写入，输出均经真实 GPU 拷贝回读。Mali 初版 e65 已过 606261 检查。正加入同进程比较开关，随后采集实际 CPU 周期与扫描减少比例；尚无 FPS 收益结论。


脏页缓存最终候选 `3b3086a889c0` 的同进程 A/B/B/A 已完成。Mali 否定结果缓存命中 58.36%/60.19%，高通 54.00%/51.13%；对应重复 dirty 检查已减少，但整帧尚无可靠收益：Mali 参考 24.954 / 候选 24.819 FPS，高通参考 18.394/18.560 / 候选 18.501/18.563。Mali 硬件每 Fine Quad 总线读写约 -0.126%/-0.210%，可视为基本不变，符合该候选主要削减 CPU 检查的定位。CPU 频率加权周期存在调度核心/频率/热态差异；高通首个参考 65.0M、随后候选 60.1M/57.9M、最后参考 57.0M，不能只选首对声称减少7%以上。Mali对应51.41/51.72/51.33/50.70M，亦未证实稳定 CPU 时间收益。保留已减少的重复检查，不把逻辑命中率冒充帧率或稳定 CPU 提速。Mali 原游戏角色选择、营地视觉正常，同步验证零错误，两机分别 606261 / 605485 checks；采集完成均已执行原生命周期新存档、停进程。

现在补当前构建的像素负载敏感性诊断：原生 2772×1240 → 1610×720 → 恢复原生，均 fixed 3D=1、相同 APK/Shader、原营地、工具统计和原生命周期存档。中间降低输出仅为诊断，不能计入30FPS原生目标；旧10FPS构建的尺寸诊断不足以替代当前数据。第一个原生窗口工具均值25.749FPS。后两段待同一采集序列完成；不要据单个 present_interval 瞬时32ms称达标。


当前尺寸诊断全部完成并恢复：原生前25.749、诊断小尺寸36.836、原生后25.468FPS；对应官方 GPU 像素/帧89.97M、35.34M、90.48M，外部字节/帧477.33MB、283.50MB、468.79MB，draw2353、2286、2355。世界逻辑均约25.01ticks/s。CPU频率加权周期51.11M、50.67M、50.76M，基本相同；实际调度运行26.42、24.11、26.72ms，受频率/核心分配影响。小尺寸 draw数亦少约2.9%，所以不能做只有像素改变的严格独占时间分解。结果支持像素相关 GPU/附件工作已是重要成本，不能据此称 DRAM 已饱和或 CPU/GPU 等待已排除；后续主线转回 GPU 运算/访存及驱动成本，不再只堆 CPU 缓存。小尺寸不计目标成果。

应用内替代驱动对照准备中：官方 Mesa26.2.5源码明确包含 FD735（chip_id0x43030B00），按官方Android/NDK+KGSL路径构建；libadrenotools8fae8ce、Volk0dc3ce0已固定来源下载。仅准备进程内加载，不修改系统vendor驱动，不引用CPU模拟逻辑。APK当前仍是3b3086，无loader改动/替代驱动性能结果。来源 https://docs.mesa3d.org/android.html 与 https://github.com/bylaws/libadrenotools ；源码/构建记录位于 `.local/tools/adreno-native-research/`。

### 2026-10-18：应用内驱动加载与真实游戏对照

替代驱动加载现已实现，默认仍使用系统 Vulkan。固定来源的 Volk 负责分发；仅显式传入 `--custom-vulkan-driver` 时，才通过 libadrenotools 加载应用私有 `runtime/vulkan-driver/libvulkan_freedreno.so`。加载失败直接报错，不悄悄回退；驱动和 hook 库保持整个进程有效，Activity 重建不切换活跃设备。未使用 turbo、CPU 模拟、系统 vendor 替换或游戏 binary 补丁。

APK `1fee5bcf7ebd4eaa087cb0490f7ed419dff6bee0029d69968e6108e8b32fc5c5`、ELF、6 个 native library 及 309 个源码输入副本保存在 `.local/build/optional-vulkan-loader-candidate/`。系统驱动路径：Host 605480、Mali 606261、Adreno 735 605485 项功能检查通过；Mali 原生 2772×1240 角色选择/营地运行、营地截图正常，同步校验无错误，测试后已执行新生命周期存档并停止。

Turnip 26.2.5 按 NDK r28c、Android API34、KGSL、16KiB ELF 段对齐构建，无源码补丁。禁止 Meson fallback 下载，隔离目标 pkg-config/sysroot，避免误链接 x86_64 的 zlib/libudev/SPIRV-Tools。可复现入口为 `scripts/dev/build-turnip.py`，校验官方归档 SHA-256、解包源码、交叉配置、工具版本与驱动产物；构建与部署分离。原厂归档 HTTPS 来源固定哈希，不声称已验证 PGP 签名。实际测试驱动 SHA-256 为 `6e1b8630305d636e56e2a9c46a148b67686cdc7694b616e20acd055303c061b0`；后续干净目录复建的产物及记录在 `.local/build/turnip/driver/`，没有用新路径产物冒充已测试的二进制。

Adreno 实际查询为 `Turnip Adreno (TM) 735`、Vulkan 1.4.354；605485 项夹具检查通过，包含同步校验、纹理 Storage 升级、跨提交资源、GPU 回读与 Pipeline Cache 驱动 UUID 拒绝。原游戏使用原精确缓存 `25c59aa26fd4`，2750×1260、fixed3D=1，无外推/冻结。营地截图正常，SurfaceFlinger 测得 17.509 FPS；同 APK 系统驱动对照为 18.594 FPS。仅此首对不能证明所有驱动版本都更慢，但没有采用 Turnip 为默认的性能依据。普通游戏窗口没有开 Validation，不能由夹具通过推断真实游戏 Validation 零错误。记录见 `.local/logs/turnip-735-native-ordinary-*` 与 `.local/logs/loader-system-735-native-ordinary-*`。

同一已验证 Turnip 驱动的原生模式诊断：强制 GMEM 15.200 FPS，强制 Sysmem 18.886 FPS，均在原游戏完成新存档后停止。使用 Mesa 原有 Android 属性 `debug.mesa.tu.debug` 选择模式，逐次恢复原值；没有修改驱动源码或游戏管线。结果说明不能简单假设强制片上分块路径一定更快，也不能把 Sysmem 首窗高出系统驱动约1.6%当作稳定收益。系统驱动仍为默认。Mali 加载器普通原生回归窗口为24.718FPS，说明没有因加载层切换自动达到30FPS。

### 2026-10-18：原描边零输入规则移植

原厂 SDP 捕获中，匹配精确 SPIR-V 哈希的 `4a819818b8e489c5` 有两次 2750×1260 描边模糊记录，目标/输入为单层单 mip R11G11B10；仅凭记录不能声称实际执行或独占时间。对应上游 Metal 已有零值规则，Vulkan 保持同一事实表，单独放在 `nvn_vk_zero_draw.inc`；未修改游戏/着色器二进制，未增设游戏键白名单。资源事实由完整清屏建立，任何物理写入失效，STORE 保留、DONT_CARE 撤销。未跟踪层/mip、CPU 新上传、非零边界/比较采样、可写资源、深度/模板写和活跃查询均保守保留绘制。Uniform 系数只允许非阻塞可见且有限，不为证明而等待 GPU。

初版真实营地未命中，拒绝计数分为源/目标零值证明失败；没有放宽判定或宣称收益。有限日志实际确认 R11G11B10 经常清为 `(0,0,0,1)`。该格式不存 Alpha，因此 RGB 仍全零。最终按存储分量建立零值事实，采样时只证明 RGB=0，不声称隐含 Alpha=0；RGB 零输入只允许消去不存 Alpha 的零值目标，不能扩展到 RGBA。只检查实际绑定目标的混合位，不把其他 MRT 槽遗留混合开关当作当前绘制的状态。证据 `.local/logs/zero-outline-diagnostic-mali-native.log`、`.local/logs/sdp-zero-outline-source-images.json`。

拥有源码的 GPU 夹具覆盖完整/局部清屏、非零输入/目标、NaN 系数、非零边界采样、被报告的样本数、GPU 拷贝覆写、R11G11B10 清屏 Alpha=1、RGB→RGBA Alpha 保留及其他未绑定目标混合位。Host608062、Mali608843检查通过，原始精确 Shader 缓存不变。候选 APK `43725584cce2a3545a6e1f5a9da9f16c8dfe37fc8b038717f0da49bbc970bef8` 与311个源码输入副本在 `.local/build/zero-outline-rgb-proof-candidate/`。新高通夹具、Mali 原游戏与同进程 A/B/B/A 的最终数据待补，尚无涨帧结论。


### 零输入描边：逐 mip 证明与实际对照（10-18 清晨）

最初整图零标记未命中不是“该优化无用”。实际 LLDB 硬件观察点确认：被读取的单 mip 图像来自另一张多 mip 图像的 mip0 全量复制。改为每 mip 独立维护零分量、仅对完整同格式同尺寸单层拷贝传播证明，其他 mip 保持原事实；局部拷贝仍保守失效。GPU 图像写入只使对应子资源失效，不能从未初始化内存或历史 CPU 内容推断零。

若完整同格式拷贝的源与目标已同时证明零，直接省去拷贝和本不存在的 transfer hazard；保留上次真实生产者的依赖。范围、格式转换、缩放、自拷贝等不满足证明的路径原样执行。真实营地每次提交消去两个描边绘制和一个零到零整图拷贝；不是任意黑色贴图消去。自有 GPU 用例验证 RGB 隐含 Alpha、NaN 系数、边界采样、查询、非零其他 mip、完整与局部复制和实际 GPU 像素。

APK `dbf0791b…` 同进程、同缓存、原生分辨率 fixed3D=1 的 A/B/B/A：Mali 24.847→25.708 FPS，Adreno735 18.481→19.599。官方 PA 的算术周期/帧约10.14M→9.04M、纹理周期4.80M→4.19M，Load/Store 约8.40M不变。外部字节/帧仅−0.18%，不能以少两次全屏绘制就宣称带宽显著下降。

加入零拷贝省略的 APK `fd59b7c4…` 独立 A/B/B/A：Mali 24.997→25.879 FPS，高通18.446→19.726。该开关对比整套零绘制/复制链，不是拷贝单项隔离实验；不能用与上轮的跨窗口差值声称额外涨帧。场景/温度趋势仍存在。证据 `.local/logs/zero-mip-abba-comparison.json`、`zero-copy-abba-comparison.json` 和对应原厂 APC/PA。全部真实游戏窗口无冻结/外推，结束后原生命周期新存档并停止进程；仍未达到30FPS目标。

另一个试验是原 Metal 默认关闭的 `DM_NVN_NOFXAA`。Vulkan 对同一原有 shader key 使用中心采样模块，保留原绘制、UV、采样器、附件和 Alpha，避免未经证明的纹理 ID 转发。默认保持关闭。属于抗锯齿质量取舍，不是无损优化；真机收益/视觉结果尚待验证。APK 构建来源清单现在显式覆盖内置 GLSL、构建脚本及生成记录，补足间接输入追溯。


NoFXAA 试验已完成：APK `ef7a6246…`，源输入327项；主机613281、Mali614062、Adreno735 613286项 GPU 检查通过，覆盖同一 Program 的 off/on/off/on 与 UV/Alpha。Mali真实营地开启后同步校验未报错、截图无明显缺失。原生固定3D的同进程对照：Mali 26.182→26.134 FPS（−0.18%，视为持平），高通19.728→20.053 FPS（约+1.65%）。PA每帧算术约−4.93%、纹理−6.42%、LS约−0.26%，外部字节近似不变；再次说明只减纹理/算术未触及主要剩余成本。该方案保留为默认关闭的原有实验选项，不因 FPS 未涨删去，也不作为主要提速成果。资料 `.local/logs/nofxaa-abba-comparison.json`；两设备均新存档并停止。


### 精确逐绘制 Uniform 特化候选（实现中，未部署到原游戏）

原厂回放对62个片元模块做通用 GLSL roundtrip，并由MOC按运算/寄存器/溢出成本选择14个变体。固定帧画面未见明显缺失，但实际r32回放LS issues总量近似不变、L2读取反而增加约33%；MOC r56预测不能替代本机证据。没有替换游戏缓存。PA对此回放导出报 `No value present`，保留失败记录及可用的官方timeline/raw，不能伪造PA结果；回放FPS也不能称为原游戏FPS。

下一候选是通用的一字Uniform分支特化：由SPIRV-Tools做SSA/验证，再为只依赖一个静态寻址Uniform字的分支选择成本最高者；只支持已证明的 `Uniform {float4[4096]}` 标准布局与实际游戏Uniform槽，不含游戏shader键。额外 `.spec` 记录原模块SHA-256、Binding/槽/字节偏移与SpecId，原SPIR-V保持不变。SPIRV-Tools现有spec pass可以冻结/折叠SpecConstant，但不提供Uniform读取转成SpecConstant的接口，故只补这一表示与运行时精确取值，未自制计时工具。

每次Draw只在无GPU待写且地址属于注册Pool时读取4字节；NaN/Inf/次正规数、未绑定/未知范围保留原模块。最多16种值/Shader，之后新值走原模块；值的完整位模式进入完整PSO键和prepared上下文，不能只在每帧取一次，也不改游戏内存或等待GPU。用标准 `VkSpecializationInfo` 让驱动在创建该值的管线时删除恒定分支。理论收益是缩短活跃范围、减少无效分支/读取，新增成本是CPU每Draw一次安全证明和有限管线变体/首次编译。

主机GPU用例覆盖同提交两Draw之间的Uniform更新、GPU生产者回退、16种上限、异常浮点和未注册地址；真机验证仍在进行。首版Mali测试耗尽夹具共用命令段，已改为测试独立CommandBuffer/命令内存；高通出现驱动shader编译失败，尚在定位。不能把这些夹具结果当作原游戏已通过、或宣称已涨帧。原游戏缓存仍为Mali `122643330366`、高通 `25c59aa26fd4`，未部署实验特化缓存。


逐绘制特化V1已完成原生Mali对照：26.593→26.430 FPS，处于场景/热趋势范围，不能称提速。原因之一是浮点异常保护拒绝了多数材质整数开关：这些word在float声明的UBO里编码为1、3等原始整数位模式，按float观察是次正规数。不能直接取消保护或把它们当浮点常量冻结。

V2改为 `uint32` SpecConstant，再在原读取位置还原位模式。只有所选word的全部float读取都直接经32位整数Bitcast观察（或无损CopyObject链）时，才允许原始整数编码；真正的浮点使用仍保留NaN/Inf/次正规数回退。源码通用、没有新shader键。自有GPU用例覆盖NaN位编码作为整数标志、CPU改值、同提交更新、GPU生产者、变体上限及未注册范围；主机663492、Mali664273、高通663497检查通过，两台均没有禁用试验模块。先前高通float SpecConstant形式在ID60000和ID0都失败，不能归因于SpecId过大；V2整数表示在两ID0用例中通过。运行时另保留单Shader编译拒绝时回退原模块/原键的保护，该错误回退分支尚未在V2真机触发。

Mali缓存 `3b0e470fc91d`、高通 `d61002aa7e42` 由原完整资源链重建，各6931个原SPIR-V与各自原精确缓存逐个相同，另有3739份可选特化recipe（3451份为整数观察）。全文件手机验SHA-256后发布，保留上一缓存。Mali APK `a8ae34fd…` 原营地2772×1240 fixed3D=1验证画面正常、同步错误0、驱动拒绝0；约41442次特化选择，30种值，165条已建管线。真实游戏帧率对照仍在采集，不能提前称已到30。

之后补充当前Draw内图形阶段写入别名选择字的保守保护：只要任意图形Shader有可写资源就保留原模块，写属性在Shader装载时缓存，热路径不重扫所有binding。该保护已过主机GPU测试，尚未部署；上述A8原生采集对应其冻结源码，不把未部署改动当作已测。原厂背景与规范：[Khronos specialization constants](https://docs.vulkan.org/samples/latest/samples/performance/specialization_constants/README.html)、[pipeline specialization rules](https://docs.vulkan.org/spec/latest/chapters/pipelines.html)。


### 2026-10-18：逐绘制特化的实测与下一项 CPU 工作

补齐上述后续结果：包含当前图形写入保护的 APK `d8200fe3…` 已通过 Host 663492、Mali 664273、Adreno735 663497 项 GPU 检查。
V2 同进程 A/B/B/A 的原厂 PA 显示，每帧 Load/Store 周期 8.408M→5.627M（−33.07%），算术约−1.96%，纹理和绘制数基本不变，外部字节约−1.3%。这些不是实际总线带宽饱和的证明。
Mali 原生固定 3D 的四窗为 27.255/29.386/28.527/29.576 FPS；高通为19.670/22.301/22.484/21.663。前后基线漂移明显，不能把均值差直接当作可靠涨帧。

随后关闭 LWI/RenderDoc/validation 的普通运行，在同一 guarded APK、原生2772×1240、原FXAA开启、固定3D=1下，SurfaceFlinger游戏SurfaceView三窗29.794/29.401/29.303 FPS，另一次31.684；尚不能称稳定30。
官方 gatord CPU 调用栈使用该进程真实ELF与驱动的SHA核对；从原始APC压缩包重新导入、将匹配ELF置于APC images并显式分析，解决初次未知符号导出，未为符号化重采。结果见 `uniform-bits-writer-ordinary-cpu-*-resolved-functions/`。
候选中 `vk_shader_bindings` inclusive占全部样本11.54%，特化选择约0.41%；后者未成为主要新CPU热点。原生ARM64游戏函数映射在宿主入口下的样本不做虚假细分。
Perfetto对同窗口给出NVN回放约26.985ms、规划1.752ms，首次提交约在4.746ms；主游戏线程等待真实guest GPU fence约14.784ms，可与翻译线程重叠，不能相加或据此称等待已经不是主要瓶颈。trace存在部分systrace解析失败，报告保留该限制。

NoFXAA/ADPF组合测试前六窗29.810/29.952/29.316/29.246/31.141/31.783，逐渐上升的基线使其无法证明组合收益；NoFXAA和性能提示继续默认关闭。第七窗前无线ADB断线，旧端口拒绝连接。新发现43319亦无法建立ADB，不能写成已存档/已停机；待恢复后第一件事是存档停进程。

基于已证明的GPU工作量减少，正常Vulkan构建和Android启动现默认启用精确特化，提供`--no-uniform-specialization`参考路径；缺recipe、GPU写、未知地址、异常浮点、变体超限均保留原模块。原Metal默认值没有改变。

下一项针对实测CPU热点：不可变DescriptorSetLayout按完整排序binding/type/count/stage内容复用，PipelineLayout按完整set布局和push范围复用，避免每个PSO重建等价布局。
对于每阶段最多四个频繁变化Uniform，使用标准动态UBO偏移：资源解析、上传与依赖仍用原始范围，只有descriptor表示拆成静态高32位和每Draw低32位偏移；偏移按binding序排列，同时进入实际绑定状态比较，不能仅比较DescriptorSet句柄。
整个graphics程序所需动态UBO数超出设备上限即保留static；compute暂用原静态路径。静态/动态模式进入完整PSO键及prepared上下文，描述符缓存也包含layout。
同一提交同一buffer换两个Uniform偏移、左右不同像素、static/dynamic/static/dynamic切换GPU用例已通过，Host665547、Adreno735665552项完整检查通过，无VUID。候选APK `b587687d…`、331项源码输入、ELF及所有native库已冻结在`.local/build/dynamic-uniform-candidate/`。真实游戏对照正在采集，未提前声称收益。

实现依据：[Khronos动态偏移](https://docs.vulkan.org/guide/latest/descriptor_dynamic_offset.html)、[descriptor/buffer管理](https://docs.vulkan.org/samples/latest/samples/performance/descriptor_management/README.html)。上述缓存仅由NVN提交线程使用，沿用shader/PSO的device生命周期；没有在途修改描述符或提早释放GPU资源。


动态UBO真机结果：APK `b587687d…` 在Adreno735的同PID26380、原生2750×1260、fixed3D1、原FXAA与特化均开启的预热A/B/B/A，SurfaceFlinger为20.591/20.529/20.543/20.572 FPS，约20.582→20.536（持平），每Draw新建/更新set约0.95612→0.71080（−25.66%）。保留此项明确的资源管理节省，不能写成显著涨帧。
后加B3/A3窗口同时降至约14.96/15.01；B3包含simpleperf采样且频率/调度分布变化，A3也不回升。原始数据全部保留，不与早先基线混算收益；90Hz显示刷新率未变化，不能归因于面板改为60Hz。现有Perfetto能确认性能状态变化，尚不能确定厂商调度策略成因。
simpleperf实际采样确认static参考路径的 `vk_shader_bindings_mode` inclusive约17.25%全样本，其中驱动`vkAllocateDescriptorSets`约4.41%；动态候选该函数约14.47%。两轮约45%调用链不能完整回溯，CPU频率/场景趋势也不相同，不能直接用样本份额差当整帧CPU节省百分比。临时按官方simpleperf要求调整`security.perf_harden`后已恢复原值1；结束后新存档commits2并停止进程。
另外分析已有SDP snapshot12的完整不可变descriptor内容：6233次绑定中已有last-key可复用2861次；64项跨切换缓存只多77次潜在复用。该旧帧不是当前动态UBO的直接收益证据，不据此新建庞大的全内容缓存。

为处理simpleperf确认的分配开销，下一候选改为同一完整布局一次预分配最多16个未写descriptor set，分别写入后仍不可变，按提交上下文隔离；真实fence完成后才清空未使用句柄并reset pool。批量不足时先在同一pool试单个，避免丢弃余量；单个也不足再沿原路径续pool。最多16且不超过设备测试配置的maxSets。常规Host和刻意压到8-set pool的完整GPU测试均667595项通过，覆盖offset/分配模式切换及原有在途资源测试。真机候选尚在验证，不提前称FPS提升。

### 2026-10-18 上午：收益汇总与呈现线程候选

以下百分比来自不同对照，不能相加。原生营地不是所有游戏场景的验收；后端 GPU 用例通过也不能代替游戏视觉检查。

| 改动 | 已确认减少的工作与成本 | 整帧证据 |
| --- | --- | --- |
| 原项目零输入描边规则、逐 mip 证明、零到零复制省略 | 营地每次 guest submission 少两个绘制、一个完整复制 | 同 PID ABBA：Mali 24.997→25.879 FPS；735 18.446→19.726。对比整个链，不能独立归因复制 |
| 精确逐 Draw Uniform 特化 | Arm PA 每帧 Load/Store 周期 8.408M→5.627M（−33.07%），外部字节约−1.3% | 四窗基线漂移，不报可靠单项涨帧；当前整体 Mali 普通原生三窗 30.211/29.710/29.675，未证明稳定达到30 |
| 不可变布局复用、动态 UBO 偏移 | 每 Draw 新建/更新 descriptor sets 0.95612→0.71080（−25.66%） | 同 PID ABBA 20.582→20.536，持平 |
| 自适应 descriptor 批量分配 | 每布局从1开始，按重复需求增为2/4/8；分配调用 0.71547→0.14623/Draw（−79.56%）；实际预留 sets +7.18%，替代初版固定16个造成的约+54% | 预热窗口 15.048→15.0325，持平 |
| Descriptor Update Template | 每布局编译不可变更新模板；正常路径只填紧凑 payload，取消重复构造 VkWriteDescriptorSet；参考路径仍可选择 | 14.981→14.964，持平，无独立 CPU 时间收益结论 |
| 查询池、BufferView、常量区、纹理脏页否定缓存 | 已完成对象才复用；减少分配、重复64KiB清零和约54–60%的重复纹理脏页检查 | 前述小节有原始计数，未证明这些项各自稳定涨帧 |

自适应分配和模板证据：`.local/logs/descriptor-template-735-matrix-summary.json`，完整厂商会话 `.local/profiles/descriptor-template-735-matrix.sdp`。NoFXAA、ADPF 保持可选关闭；Turnip 系统驱动对照没有可靠提速；出现画面错误的半精度实验没有进入默认路径。

完整旧/新/回旧 APK 对照的重要限制：冻结 d820 先测预热后20.559/20.609 FPS，f255 为14.922/14.947，随后重新安装 d820 也由早窗20.530降为晚窗14.962/14.946。分辨率、角色位置、Shader缓存和输入选项相同、无抓帧层、SDP服务已退出；不能认定新版是唯一成因，也不能直接称为热降频。回旧 HAL GPU cooling 状态由8变9，但该值不是MHz，尚无可用频率读数证明因果。证据 `735-frozen-version-comparison-*`、`735-frozen-version-return-*`；每个原游戏进程结束均新存档commits2后停止。

Perfetto 真实 API 边界定位到翻译线程内阻塞：735 replay均值45.512ms，随后的 `Vulkan present call` 19.468ms；Mali分别26.239ms和5.642ms。acquire分别约0.097/0.146ms，队列锁约0.002ms。主线程 guest GPU fence等待可以与翻译重叠，不能相加。这表明呈现的主机调用仍使后续 NVN 翻译延后，不能说CPU/GPU等待已经排除。

据此实现默认关闭、启动时固定的独立呈现候选：同 family 第二个队列仅处理 present；原队列继续全部 render/compute/transfer。所有 acquire/present/feedback 由一个 `dm-present` owner 串行执行，NVN翻译只在原出图拷贝真实提交后异步排入 present；下一次 acquire 仍需真实可用图像，不预取未用图像。每图像信号量复用、真实fence和回收依赖保持，重建和Surface替换先drain请求与两队列。实际创建第二队列的 Mali GPU 用例672472项通过，不代表游戏出图路径已通过。

候选 v2 原角色选择时，成功 Present 后的 `vkGetPastPresentationTimingGOOGLE` 返回 `VK_ERROR_OUT_OF_DATE_KHR`，旧代码将其直接fatal，PID18438退出。没有把该次失败计为性能数据。v3 将该结果沿现有 out-of-date 路径交给 submission owner，drain后重建交换链；worker不自行销毁Surface。Khronos文档明确列有该错误并要求swapchain host访问外部同步。冻结v3 APK为 `562301a3fe4886cb90e1e3bc8ccc56a3fdd526d51040a04fe46ebc9a687d6645`，332项输入与ELF/所有native库存档于`.local/build/wsi-worker-v3-candidate/`；Mali GPU用例672472项再次通过，真实游戏验证另记。尚无该worker的涨帧结论。

#### Uniform 特化对 Shader 跨材质、跨场景复用的要求

不能从营地或材质名称推断选择值永远不变。离线只生成可选 recipe，不固定任何捕获值，原6931份基础SPIR-V保持不变。运行时 `vk_draw()` 每次重新从**当前绑定**的Uniform读取完整4字节，再进入管线查找；即使绑定地址、Program和raster state均未改变，也要重读。特化模块句柄和完整值同时进入普通PSO key与prepared快速复用context，值1/值3/值1必须选择各自的不可变管线，不能以Shader或材质身份替代输入值。

已知前序GPU写、当前graphics阶段存在资源写、未知/越界地址、不能保持的浮点异常、每Shader超过16种值或驱动拒绝可选变体时，保留基础Shader。数值上限只限制优化，不能丢失新值的分支。测试 `native_vulkan_specialization.inc` 覆盖同Program换值再换回、同提交原Uniform更新、GPU compute生产者、原始整数flag位模式、异常值与变体上限，回读真实像素；这些是切换机制证据，不能当作所有游戏场景均已验证。剩余正确性风险是遗漏资源写依赖、缓存键漏状态、recipe错误或驱动误编译，须继续通过多场景和参考路径验证，不能宣称无风险。

规范依据：[vkGetPastPresentationTimingGOOGLE](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPastPresentationTimingGOOGLE.html)。

v3 原游戏验证仍未通过：PID19808 在进入营地前退出。系统日志首先出现 `QueueSignalReleaseImageANDROID failed: 2`，随后 timing query 失效，重建返回 `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`。AOSP `vulkan/libvulkan/swapchain.cpp` 在 release 失败后会 orphan swapchain，timing query 因 active handle 不符返回 out-of-date；这解释后续错误链，不证明最初 release 失败是驱动缺陷。默认关闭，未计性能收益，不能将后端无Surface用例通过当作WSI通过。Mali停止失败进程后恢复此前实测f255版本，保持停机散热。下一步需分别验证同队列worker/第二队列及validation开关的受控差异，再决定队列策略，不能绕过真实完成或吞掉GPU失败。


#### 2026-10-18：呈现 worker 与翻译线程 CPU Profile 的后续证据

v4 同队列呈现 worker 在 Mali 原游戏通过校验，但原生全分辨率三窗为29.024/28.752/28.300，第三窗位置发生改变；同 APK inline 为29.655/29.395/28.527，不能认定 worker 提速。v5 第二队列先提交空桥接等待/信号，再由同呈现队列消费信号，Mali 原游戏也通过校验、出图和新存档，但普通三窗26.124/25.847/25.719，未默认采用。独立队列和共享队列 worker 均保持可选关闭。Mali 恢复旧已验证版本后停机，后续炫光候选单独冻结和验证。

高通的同 PID 对照仍出现20.175→14.960 FPS（SurfaceFlinger，冻结v4，原生2750×1260、3D比例1、无捕获layer）。低帧窗口实际完成官方 simpleperf CPU-clock 99Hz DWARF 采样，无推测调用链拼接，1001个进程样本、丢失0，其中562个属于nvn-translate；同时取得Perfetto调度/频率/系统trace。`vk_shader_bindings_mode` 子树约四成翻译线程用户CPU样本，descriptor驱动更新/分配、内存比较/复制、页同步和提交索引仍占明显工作量。这是CPU热点分布，不是独占整帧耗时，inclusive百分比不能相加；不能据此宣布GPU已不受限。

记录：`.local/profiles/cpu-phase-v2-735-late.data`、`.local/profiles/cpu-phase-v2-735-late.html`（官方simpleperf交互报告）、`.local/profiles/cpu-phase-v2-735-late.pftrace`、`.local/logs/cpu-phase-v2-735-late-{flat,children}.txt`。前一次同进程早/晚采样另有`.local/profiles/cpu-phase-735-{early,late}.data`，不能把采样窗口FPS当作无工具运行帧率。

随后原生命周期后台保存并恢复同PID，但复测窗口SurfaceFlinger没有游戏SurfaceView，因此该次phase-reset实验无有效FPS，不支持“重置相位能恢复性能”或其相反结论。进程再次fresh commits2保存后停止，`security.perf_harden`恢复1；失败边界保留在`cpu-phase-v2-735-result.json`。

按已发现的纯CPU冗余，候选增加不可变physical-device核心format properties的一次初始化缓存；扩展format仍走驱动原查询。另将单一录制owner的六个布局统计计数批量发布，实际资源代数/依赖历史不变；避免每次绑定都作原子read-modify-write。两项已随炫光v2冻结并通过Mali672666/高通671890 GPU用例，但没有独立FPS收益结论。炫光同APK开关对照会在两侧均包含它们，不能把它们的潜在收益混记为炫光收益。

炫光v2的Mali原游戏校验通过但chains=0，未进行无命中的性能对照；原厂既有捕获显示最终C实际为加法混合、D启用stencil，需补对应依赖后再测。高通v2原游戏校验出现R16_UNORM比较采样VUID06479，旧`compute-resource-order-735-realgame-validation.log`已存在同条警告，不能宣称高通全游戏校验零错误，也不能归因于本轮炫光（其未命中）。两台游戏均已确认新存档后停止。


#### CPU：完成后保留描述符集合（2026-10-18，冻结 c43de27a）

官方simpleperf证明原重置模式的`vk_descriptor_allocate`子树仍占翻译线程用户CPU样本9.81%，内部驱动反复calloc/free。新增按submission context和不可变layout保留VkDescriptorSet句柄：只在GPU完成且CPU完成回调已发布之后重置使用游标；下一次使用前完整改写binding，不缓存上个提交的descriptor内容，也不改写在途集合。保留原动态偏移和资源依赖流程，旧`descriptor_last`在完成时仍清空。layout是本后端设备生命周期的intern对象。每context最多保留配置pool的maxSets（默认2048）；累计布局需求超预算时在完成边界reset全部pool、丢弃句柄vector，避免跨场景无界累积。

运行时`descriptor_reuse_reference=true`恢复每次完成后重置的路径，用于同APK同PID对照。默认保留复用；接口`--descriptor-reuse-reference`、状态`descriptor_reuse`。Host异步+同步校验，正常与8-set小pool各696759检查通过；真机Mali697537/Adreno735696761通过。GPU用例包括32轮参考/复用切换、同buffer交换偏移、静态/动态UBO、单个/批量分配、模板/普通更新及真实in-flight多owner，另有本轮bloom清除取消用例。Mali原生2772×1240/3D1原游戏校验通过、已新存档停机。

高通同PID32112、原生2750×1260/3D1、bloom关闭、无采集layer的ABBA：参考20.409/20.657，复用20.616/20.657 FPS。均值20.533→20.6365（+0.50%），不宣称可靠涨帧。每NVN draw的驱动分配调用0.145695→0.000169（−99.88%），新分配集合0.764797→0.000810（−99.89%）；两个复用窗口pool reset为0，参考分别3326/3538。这是实测消除的工作量，不能直接换算成CPU时间节省。

随后另行用官方simpleperf+Perfetto在同PID对照：分配子树样本9.81%→0.34%，绑定子树39.59%→33.00%；剩余驱动descriptor更新子树约12.79%，提交规划约5.89%。这些inclusive百分比不能相加或当作整帧时间。Perfetto实际replay wall34.830→34.784ms、调度running31.731→32.027ms，按频率积分约54.001M→53.503M cycles（非硬件PMU）；因此也没有证明翻译线程整体显著变快。优化保留，因为分配/重置工作已明确减少，下一步应继续针对descriptor更新、资源/页访问和规划工作，而非宣称CPU瓶颈已经解决。新旧采样末尾整进程PSS约2776720→2781153KiB（+4433KiB，包含其他缓存漂移，不是本优化独占内存）。

冻结APK/ELF/339项源码副本：`.local/build/descriptor-retain-bloom-v4-candidate/`；结果`.local/logs/descriptor-retain-v4-735-summary.json`。官方交互报告`.local/profiles/descriptor-retain-v4-735-cpu-{reset-reference,retained}.html`，原始`.data/.pftrace`均保存。两台已通过原生命周期fresh commits2存档并停止，profiling property恢复1；普通FPS与采样窗口分开。

#### 2026-10-18：描述符内容复用与当前 CPU 证据

在单个 submission context 内增加有界的不可变 descriptor 完整内容缓存，允许 A/B/A 切换命中；所有资源解析、上传、动态偏移和依赖处理仍先执行。键包含 shader、layout、每项实际句柄/offset/range，哈希相同仍完整比较；真实完成后换 epoch，不跨提交沿用内容。保留句柄与保留内容是两件不同的事。256 槽按需分配，完成边界回收超过全局 16 MiB 预算的闲置元数据。碰撞、换偏移、换引用路径、in-flight 等既有 GPU 用例通过；Mali 697539、高通 696763 项检查通过。

同 PID 原生分辨率、3D 比例 1 的 ABBA 中，descriptor 更新/Draw 约 0.715→0.588（减少约 17.8%）。高通参考 20.595/20.576 FPS、缓存 20.386/20.562；Mali 参考 28.366/31.009、缓存 28.000/29.671。没有证明涨帧，Mali 基线本身有明显漂移。机制成立、额外缓存约 3.4 MB，保留该减负；不把局部工作减少写成整体 CPU 提速。

高通官方 simpleperf/Perfetto 同 PID 4588 对照：绑定处理子树用户 CPU 样本 33.62%→32.03%；回放 wall 34.654→34.470 ms，实际调度 running 31.749→31.678 ms。驱动 descriptor 更新仍约占缓存侧用户 CPU 样本 11.86%，纹理绑定、layout/依赖、脏页检查及资源锁仍有成本。inclusive 样本不能相加。`Present` 调用另外平均约 11.8 ms；不能将它归入回放的 CPU running，或宣布只剩 CPU 瓶颈。证据位于 `.local/logs/descriptor-history-*-abba-result.json`、`.local/profiles/descriptor-history-735-abba-{last-only-reference,history}-sample.{data,pftrace}`。

本轮单线程小改：当整张 image 的 layout 摘要为目标具体 layout 时，所有 mip 已一致，不再逐 mip 重复确认。混合 layout 的摘要为 MAX_ENUM，仍逐 mip 处理；先验证范围，实际访问依赖仍逐操作处理。没有省略 barrier、改变命令顺序或缓存 GPU 查询结果。宿主校验结果单独记录在 `.local/logs/layout-summary-fastpath-host-validation.log`；真机收益待测。

#### 后续想法记录：显示节拍与翻译并行，暂不实施

按用户最新决定，当前优先级是降低单个翻译线程的 CPU 工作；显示调度和翻译线程模型的改造先记录，不与本轮减负混做。

- 当前游戏并非无限提交：原 Acquire 软节拍、32 项翻译任务队列、纹理返回与 GPU 在途资源限制共同提供背压。但 Android Choreographer 当前只驱动触控；游戏还没有接官方 Frame Pacing/Swappy。帧内安全边界提前提交保留，不能把它误当作整帧无界排队。
- 候选方向是结合显示回调、目标呈现时间与完成栅栏，尽早限制过期显示工作积压。仅有回调不能完全避免 buffer stuffing。MAILBOX 只淘汰待显示请求，不能撤销已经执行的 GPU 工作；不能以画面变化小为依据丢弃任意 NVN 整帧，计算、查询和写回副作用仍须执行。记录依据：[Android 官方 Frame Pacing](https://developer.android.com/games/sdk/frame-pacing)。Swappy 2.1.3 官方 AAR 已下载到本机工具目录，但没有接入构建或运行时；默认仍为 FIFO，不新增丢帧策略。
- GPU 遮挡/时间戳结果不能直接跨帧缓存；当前结果在完成 worker 等真实 Fence 后获取。可复用查询池及不可变元数据，应与“复用上帧结果”区分。
- 翻译并行只作为单线程减负不足后的候选。现有 S/V 状态、依赖历史、内存镜像及写回顺序均由单翻译线程维护，不能逐 Draw 直接派任务。可考虑先串行确定状态和依赖，再按较大批次用独立 command/descriptor pools 记录 secondary buffers、按原顺序合并。应优先保持同一个 render pass，避免为并行拆碎 tile 渲染；必须计入状态快照、任务通信、合并等待、内存和热功耗成本，不能假定线程数等于加速倍数。当前尚未改线程模型。依据：[Khronos 多线程录制示例](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html)。


#### 2026-10-18：扩大动态 Uniform 范围的候选

官方 simpleperf 中驱动 descriptor 更新仍约占翻译线程用户 CPU 样本 11.86%。原路径只将 c0/c2/c3/c4 设为动态 UBO；按当前 d61002aa7e42 原游戏加载日志与 SPIR-V 活跃 binding 复核，110 个模块中 91 个还有其他可动态寻址的 Uniform 槽。静态 c1 保持原样，不能把“Shader 已知字段偏移”误当作“运行时已知每次 Draw 的缓冲块地址”。

增加不可变的 static/narrow/extended 三种 schema：extended 覆盖每阶段最多16个实际 Uniform，按真实 Vulkan binding 顺序提交动态偏移。所有原始 address/range 的资源解析、上传和依赖仍先执行，不扩大 descriptor range，不改 GPU 写入/查询/写回时序。整个图形 program 的动态数量超过设备限额时回退 narrow，再回退 static；extended 与 narrow 完全相同时复用原变体，避免无意义新建 PSO。模式进入完整布局和已有 PSO/prepared key，运行时可用 dynamic_uniform_narrow_reference 对照，无需改环境变量或重启。

代价包括更长的动态偏移数组、额外不可变布局/模板、驱动绑定动态偏移的成本，以及将每个 descriptor pool 的动态 Uniform 容量从 maxSets*4 提高到 maxSets*16。必须核对净 CPU、GPU 和内存成本，不能仅凭更新次数变少称已提速。机制依据：[Khronos 动态偏移](https://docs.vulkan.org/guide/latest/descriptor_dynamic_offset.html)。

自有 GPU 用例增加两个独立 Uniform（NVN 槽0与5），反向 binding 顺序与轮换偏移，覆盖 static/narrow/extended、模板/普通更新、内容缓存碰撞、保留句柄及设备总预算回退。宿主异步762436/同步722404检查、Mali763214/Adreno735762438检查通过；Mali 原游戏原生2772×1240/固定3D1同步校验零错误，实际英文营地画面已查看。冻结 APK 3b9e4175…、ELF e5bf507e…及339项源码副本位于 `.local/build/wide-uniform-candidate/`；自有夹具冻结及生成器指纹另见 `.local/logs/wide-uniform-fixture-build.json`。普通 ABBA 和官方 CPU 对照正在另行记录，不把校验帧率当作性能。

上游 Metal 对照：`nvn_mtl.m` 的 mtl_pool_add（约443行）使用 newBufferWithBytesNoCopy 包装游戏池；encode_args（约3049行）按 Uniform 当前绑定解析实际 GPU 地址，放入 slots 参数块（约3096行），经 arena_alloc/memcpy/bind_args（约3171行）绑定。g_args_dirty 与 pass 条件满足时直接复用已有参数块（约3053行）；OP_BIND_UBO（约3416行）仅在绑定结构变化时标脏。因此原 Metal 并非没有做绑定管理、只靠硬件性能掩盖问题：它已经有共享池、地址表、arena 和参数块复用，与 Vulkan descriptor 的维护成本不同。这里没有跨 API 的可比 CPU 实测，不能据此量化 Metal/苹果硬件的速度差。

编译期合并 Uniform 的进一步想法先记录：可将读取改成大缓冲加运行时偏移表，但跨池数据若要物理打包仍可能新增 CPU/GPU 拷贝；UBO 改一般 Buffer 读取也可能改变 GPU 访存路径。编译器只能统一映射规则，运行时地址、边界、资源存活和 GPU 生产者依赖仍须正确处理。暂不做此 Shader ABI 重写，先验证当前不搬数据的动态偏移路径。

上述 ABBA 已完成，两机原生命周期新存档2次后停止。高通 narrow20.451/20.624、extended20.420/20.642 FPS，均值20.5375→20.531；更新/Draw0.58719→0.12814（−78.18%）。Mali narrow28.459/30.998、extended28.083/27.737，更新/Draw0.58600→0.12782（−78.19%），基线有明显漂移且候选未证明涨帧，不能写成已达到30。正式 CPU 对照里高通绑定子树35.47%→25.78%、驱动更新11.42%→3.31%、驱动绑定2.77%→3.13%；整个 replay wall34.050→33.417ms、running31.317→30.708ms，只有约0.608ms的整段运行减少，Present 则12.265→12.862ms。比例不可相加，驱动更新减少不等于整帧同比提速。原始`.data/.pftrace`与摘要`.local/logs/wide-uniform-summary.json`已保存；Mali GPU成本和额外pool容量仍须复核，保留明确的工作量减少，不提前宣布已完成优化目标。

Mali 原厂硬件计数器对照已补齐：同一进程、同一冻结3b9e4175 APK，以 Arm LWI/gatord 采集12秒，官方 Streamline/Performance Advisor 分析2–10秒。extended 相对 narrow 每帧算术周期+0.043%、Load/Store周期+0.273%、纹理周期−0.054%、实际外部总线字节+0.055%，GPU周期+1.826%。没有证据表明显著增加 Shader 工作，也没有证明 GPU 提速；带工具28.2→27.6 FPS不能当作普通帧率。原始APC、官方报告与摘要分别见 `.local/profiles/wide-uniform-mali-arm-run{1,2}.apc`、对应report目录及 `.local/logs/wide-uniform-mali-arm-summary.json`。PA 的 fragment-bound 分类与 GPU utilization 不能用来排除全部同步停顿。采集后已新存档2次并停止游戏，恢复layer设置。

#### 2026-10-18：提交内内存池查找与页登记减负

扩大动态偏移后，官方 simpleperf 仍显示 pool_address 及资源锁的重复开销。每个物理提交增加最多8项内存池查找缓存，仅复用该提交已经持有强引用的 pool/use；池注册或删除推进 registry epoch，任何变化均使缓存失效。缓存命中仍检查真实地址/长度、初始化实际范围并登记页用途，后续脏页上传、GPU依赖和CPU写回照常执行。完成worker仅在真实Fence和CPU可见副作用完成后释放引用，缓存不越过提交回收，也不缓存游戏内存内容。initialized_bytes 统计改用原子，避免新快路径初始化与状态读取之间产生数据竞争；每次查找的逻辑计数先保留在录制线程，到提交时统一发布。

冻结bae3ef14 APK、07683fd1 ELF及339项源码副本在 `.local/build/pool-lookup-candidate/`。宿主同步722409/异步762441、自有Mali763219/Adreno735762443检查通过；Mali原游戏原生2772×1240/固定3D1同步验证零错误。两机同进程ABBA中约99.895%的查找不再进入全局资源锁和链表遍历。高通锁路径20.5465、缓存20.6075 FPS；Mali锁路径29.702、缓存28.724，基线有明显漂移，均不能宣称可靠涨帧。原始状态证明游戏约25 tick/s、未启用冻结或外推；摘要 `.local/logs/pool-lookup-summary.json`。

官方CPU对照中，高通 pool_address inclusive8.32%→3.94%，整段replay running30.313→30.539ms，频率积分估算52.175→51.079百万周期；Mali7.28%→3.21%，running32.071→37.411ms而估算47.378→47.306百万周期，后者频率明显变化。频率积分不是硬件PMU，以上既不能相加，也不能推出整帧提升。Mali Perfetto 将同一个仍存活的翻译TID拆成两个UTID，其中后一个错误归到pid=tid；查询按已确认的真实TID关联sched_slice，保留原始trace及导入错误提示，不把旧的零匹配结果当作零CPU时间。

首版重复页登记缓存按字节范围保存，真实营地只有约0.12%的查找能跳过位图重标记。后续候选将这个证明统一成实际位图的页区间，同时保留阶段和initial-read条件：同页不同偏移可复用；新增stage或从完整覆盖目的切换为读取时仍须扩充位图。实际字节范围初始化、依赖和拷贝不改。自有用例补充同页不同偏移、阶段扩展和阶段并集检查，既有用例继续验证先读后写、部分覆盖和下一提交CPU修改。候选冻结981f2f1c APK、52c33a84 ELF在 `.local/build/pool-pages-v2-candidate/`；整帧和CPU收益须以该候选对照结果为准。

另一个独立通用修改是命令池创建不再设置 RESET_COMMAND_BUFFER_BIT。后端只在真实GPU完成且CPU副作用结束后调用 vkResetCommandPool，所有主/次命令缓冲在每轮录制中只取用一次，没有逐缓冲reset或重复begin。取消多余能力允许驱动采用整池分配策略，符合 [Khronos建议](https://docs.vulkan.org/samples/latest/samples/performance/command_buffer_usage/README.html)。独立冻结cad80371 APK及8bb8d1b6 ELF已通过同样宿主及两机自有GPU检查、Mali原游戏同步验证；普通高通20.619/20.375/20.562、Mali27.911/27.384/27.226 FPS，未证明涨帧。高通 vkResetCommandPool 样本4.50%→4.10%仅为相邻采集，不能当作稳定净收益。

页区间候选981f2f1c的完整对照已结束：宿主异步762445/同步722409、自有Mali763223/Adreno735762447检查通过，Mali原游戏同步验证零错误。高通重复页登记从旧字节候选约8800次/20秒提高到649057/652390次，CPU replay running31.581→30.256ms、频率积分估算52.384→51.144百万周期；锁路径20.388/20.534、缓存20.384/20.534 FPS，均值20.461→20.459，没有可靠涨帧。Mali完整窗口27.651→21.467→15.895→15.918，回到锁路径后仍低帧，系统thermal status3、HAL skin约50.5℃；本轮不能用于净帧率归因，也不能仅据温度就断定全部下降由热造成。CPU对照running39.874→39.543ms、估算49.008→48.515百万周期。两机均已原生命周期新存档2次并确认停止，Mali待冷却后再测试；原始结果与CPU摘要在 `.local/logs/pool-pages-v2-summary.json`。

用户追问mprotect方案：当前已沿用原项目的 `nvn_dirty.c`，以系统页大小保护、首次写fault解保护并推进页代数，使用前重新保护和检查代数；宿主已知写入走prep_write。mprotect只提供CPU写入的页级线索，不提供页内变化字节，也不观察GPU写VkBuffer。现有guest/shadow精确比较用于避免旧CPU邻接数据覆盖尚未发布的GPU结果，不能直接替换成整页上传。下一步优先核对每页上传代数、GPU写回造成的无效重检查，再优化必要的变化范围提取；尚未实现SIMD扫描或替换dirty tracker，不能把它们计入当前收益。

#### 2026-10-18：脏页上传的精确向量扫描与每页代数过滤

后续已完成两项通用减负，mprotect跟踪、真实GPU依赖和完成后的写回规则保持：

- AArch64用NEON并行比较四字节单元，每64字节形成变化掩码，提取连续变化段并跨向量/块边界合并。仍只复制变化单元，不为了减少命令而把相等邻接字节一起上传；保留整页相等的快速memcmp。输入来自现有≤4096字节且四字节对齐的页分段，向量读取不越界，最坏交错变化仍最多512段。其他架构按同样掩码逻辑比较整数，原标量扫描保留为显式对照。
- 连续使用范围仍先用最旧同步代数查脏，但回调再核对较新页面的自身上传代数；已有tracker证明该页没有后续修改时，跳过整页内容比较。没有将GPU写入误当成CPU修改证明，没有跨提交复用旧内容；重新写同一页后必须再比较。跳过计数在录制线程累计，完成本次上传准备后统一原子发布。首次自有测试发现计数发布早于上传准备，已把发布移到正确位置；不是通过放宽数据检查解决。

自有GPU测试补充稀疏修改、零数据、向量/64字节边界、末字、512交错变化段，以及先部分同步、后合并读取、再修改已同步页；同时运行既有GPU写入邻接数据、异步上传和CPU发布竞争用例。冻结向量扫描26768626 APK/46aaac90 ELF通过宿主异步811635/同步771599、Mali812413/Adreno735811637检查；Mali原游戏同步验证零错误，实际营地画面已查看。源码及全部339输入副本在 `.local/build/upload-scan-candidate/`。

该扫描独立高通ABBA为标量20.472/20.652、向量20.389/20.605 FPS，无可靠涨帧。官方simpleperf上传回调inclusive6.46%→4.18%，但整段replay running31.004→30.980ms、频率积分估算52.260→51.987百万周期，不能将局部样本下降当作整帧显著提速。原始与摘要 `.local/logs/upload-scan-summary.json`，采样帧率不当作普通帧率。

合入每页代数过滤后，冻结4d296215 APK/b810382f ELF通过宿主异步869001/同步771599、Mali869779/Adreno735869003检查；Mali原游戏同步验证零错误。两机原生输出分别2772×1240/2750×1260、固定3D1、原逻辑约25 tick/s，未启用freeze/extrapolation、NoFXAA或bloom候选。

高通20秒候选窗口分别跳过55160/51336次页内容比较；完整ABBA20.595/20.203/20.581/17.866，末次参考同样降帧，thermal status仍0，不能归因于本修改或直接说是热降频。随后低帧状态官方CPU对照running37.810→38.121ms、频率积分估算59.894→59.265百万周期，Present分别23.399/23.861ms；与此前快状态的31ms/12ms不能混为同一基线。

Mali冷却后采用8秒短窗口，参考28.755/27.230、候选27.869/27.844 FPS，未证明稳定30或净涨帧；候选分别跳过26228/29019次页内容比较。官方CPU对照running26.704→25.735ms、估算47.296→43.414百万周期，Present6.564→4.777ms。后者受频率/调度/采集状态影响，频率积分不是硬件PMU。原始trace、before/after状态和摘要 `.local/logs/upload-pages-summary.json`。两机均已新存档2次并确认停止，Mali没有留在后台空跑。

硬件PMU补充：两机simpleperf确实能打开用户态cycles/instructions计数，最初自进程探针只用于核实权限，不当作游戏结果。随后高通冻结4d296215 APK、实际PID30853的唯一nvn-translate TID完成官方 `simpleperf stat -t TID --no-inherit --duration 8 -e cpu-cycles:u,instructions:u,branch-misses:u --csv` 三段参考/候选/回参考，未加自定义计时器。参考15.713/14.698十亿指令、8.154/7.794十亿周期、30.551/24.918百万分支预测失败；候选14.257十亿指令、7.470十亿周期、23.210百万分支预测失败。对应既有状态端点分别覆盖394632/394639/394632个Draw、均168次guest submission，支持本组候选确实减少CPU工作；参考本身仍有漂移，不能把3%–9%的单组指令减少写成稳定FPS收益。状态端点覆盖约8.04–8.05秒，并不严格等于8秒PMU窗口，不能冒充精确每帧指令数。记录 `.local/logs/upload-pages-735-pmu-v3-pmu.json`、三份原始PMU CSV及 `.local/logs/upload-pages-735-pmu-summary.json`。

前两次PMU编排分别因Android ps的NAME列返回进程名、以及状态日志尚未反映刚切换的参考模式而中止，均未取得硬件计数且已新存档停机；没有将它们当成驱动不支持。最终从已确认PID的task/comm定位真实TID，并等待实际模式生效后采集成功。profiling property已恢复1，最终游戏已新存档2次并确认停止。后续CPU工作量核验优先使用已跑通的PMU，历史频率积分继续仅作为估算保留。


### 采样器查找与硬件指令计数（2026-10-18）

官方 simpleperf 以实际翻译 TID 采集 instructions/cycles，显示原 `vk_sampler` 的大部分工作是资源锁；没有把 inclusive 样本比例相加或当作独占整帧时间。随后增加 recording-owner 的64槽精确id缓存，仅在同一物理录制serial和sampler登记epoch都匹配时复用句柄。首次查找仍在锁内创建/解析并登记last_use；相同参数不变epoch，不同参数/关闭设备失效。缓存不持有可realloc注册表的指针，旧sampler继续等待原快照宽限期和真实GPU完成。统计只在提交边界发布，未增加每次查找的原子RMW。

冻结APK `61fb2850d39c9f0b9c6f5411e635505cba0e7a36b0ae3cd41234abe3b9b5118c`、ELF `b47b166b…` 及339项源码副本在 `.local/build/sampler-lookup-candidate/`。host异步869107/同步771705、Adreno735869109/Mali869885 checks通过；owned用例覆盖同submit参数替换、相同登记、表扩容、hash碰撞和GPU真正完成前旧sampler保活，Mali原游戏同步校验零错误。

高通同PID2394普通8秒ABBA为20.654/20.643/20.596/20.607 FPS，未证明涨帧。开启缓存的两窗分别命中1889496/1889488次，约99.77%查找免锁。实际翻译TID2590另做官方8秒PMU：locked/cached/locked-return分别14.380/14.212/14.366十亿指令、7.726/7.587/7.531十亿周期；各状态端点约394632 Draw。候选指令比两侧参考少1.17%/1.07%，周期落在两参考之间，不能宣称稳定周期或整帧改善。状态端点覆盖8.04–8.05秒，不严格等于8秒PMU，不能冒充精确每帧指令数。原始CSV和状态在 `.local/logs/sampler-lookup-735-abba-pmu.json`，摘要 `.local/logs/sampler-lookup-summary.json`。

Mali首次普通测试PID27128在进局前线程退出时栈溢出，系统报告显示pthread_exit→jemalloc清理路径，尚不能归因于缓存；保留crash/原日志。重试PID27706已到营地，但测量窗口角色坐标改变，未作为有效性能对照；随后原生命周期新存档2次并确认停机。没有将无效窗口计入FPS收益。

### 提交规划重复工作减少（候选，2026-10-18）

同一官方instructions采样中提交规划约7.90% inclusive，提示可减少重复解码和索引工作。flatten时保存已验证opcode与samples-counter分类，利用planned_op原padding，仍为16字节；后续逆扫、索引和即时depth/query检查不再反复读原命令header。原始store_scan保留独立raw decoder供结果核对。

索引仅合并**同一命令序号、同一纹理bucket**的重复whole-image读事件；不同序号绝不合并，跨alias/full-clear的读取位置保持。新增metadata-only用例在完整clear两侧放同存储不同快照读者，核对clear前必须保留、后续完整覆盖可丢；self-copy只用于索引，不送到GPU。统计recorded/duplicates_skipped记录逻辑事件节省，不当作硬件带宽或FPS。这些候选尚未部署，整帧收益未证明。


### 炫光C物化与纹理绑定元数据候选（2026-10-18）

883b8922冻结APK的host异步872825/同步775423、Mali876675/Adreno875899通过。Mali原生营地同步验证零错误，Cblit121次/回退0。普通同PID8秒ABBA，高通20.607/20.725/20.416/20.643，OFF20.625→ON20.5705；Mali26.122/25.955/25.927/26.139，OFF26.1305→ON25.941。无可靠FPS提升；不拿不同时刻旧28–30窗口计算版本回归。原厂Arm每帧GPU周期−0.47%、算术−0.42%、LS+0.14%、纹理−1.15%、实际外部字节+0.065%。这些指标非互斥，不能相加。说明炫光节省较小，按用户要求不再精进跨帧lazy物化，保留可对照实现。详情 `.local/logs/bloom-c-blit-ordinary-summary.json` / `bloom-c-blit-mali-arm-summary.json`。

883b新官方PMU采样：实际高通PID9286/TID9389，instructions760/cycles747样本，丢失0。绑定/视图绑定/提交规划/sampler inclusive指令份额27.73%/11.83%/8.08%/0.12%；不可相加。Perfetto328个replay平均wall39.489ms、running35.980ms、Present7.546ms，包含99Hz双事件DWARF采集成本，不能直接与普通CPU时间对比。规划同序读去重每窗4–5千个、保留事件250–325万个，属于小幅减负，不能当作主要提速。

814d662d候选因此增加每提交context64槽**不可变纹理视图元数据**复用：精确键为完整descriptor id、sample/storage用途、登记epoch、物理recording serial、VkImage句柄；先检查serial再解引用owner。回调前复制小metadata防嵌套碰撞，回调后记实际last_use，登记/限制/物理promotion/提交切换回退。layout、CPU上传、alias判断、GPU阶段依赖每次照常执行，不缓存其结果。缓存64×160B=10KiB/context，context总12296B（GDB类型检查）；付出键检查和miss填充，节省资源锁/大描述符复制、image查表、范围/格式/组件构造和view链扫描。支持texture_binding_reference同PID对照；逻辑计数仅提交边界发布。

host异步872841/同步775439，Mali876691/Adreno875915 checks通过，零owned同步校验错误。新gatedGPU用例覆盖同提交STORAGE/mutable物理升级、descriptor格式替换/表扩容，早期dispatch仍采原image且校验输出，真实GPU完成前旧资源不释放。冻结APK814d662d8df48bb754c2ea054353995ab7d1ce37bd87c633596fe73900bd6a5a、ELFe8014e45…及339源码在 `.local/build/texture-binding-candidate/`。真实游戏校验、普通ABBA、轻量官方PMU+Perfetto运行时间对照正在执行，尚未计入CPU/FPS收益。两路均按最新用户要求继续：CPU优先实际running时间，GPU用原厂工具追查具体shader；不要求每项单独涨帧才保留。


814d纹理元数据缓存的高通同PID11807普通ABBA已完成：reference/cache/cache/reference为20.020/20.667/20.259/20.596 FPS；前后reference漂移，不声明FPS收益。两缓存窗实际view命中1459790/1425460次、miss459680/448894次，约76%完整绑定免重复元数据构造，动态上传及同步语义仍执行。原生2750×1260、fixed3D1、原世界25Hz，fresh save2后停止。轻量官方simpleperf同TID11913的8秒reference/cache/reference：instructions14463921355/14005706119/14507827321，候选较两侧−3.17%/−3.46%；cycles7864156463/7616632931/7841261041，较两侧−3.15%/−2.87%。应用状态发布区间与PMU窗口不完全重合，不能用它除draw数声称精确每draw收益。并行10秒官方Perfetto replay running31.617/31.496/31.454ms，候选处在两侧之间，**尚不能确认实际CPU时间降低**。保留成立的工作量节省；继续采当前814的官方调用栈定位余下CPU热点，而非把计数节省直接写成涨帧。汇总 `.local/logs/texture-binding-summary.json`。


### 翻译线程工作量候选与真实GPU资源证据（2026-10-18后续）

冻结d364b345 APK /37dd009b ELF及339源码在 `.local/build/translation-work-candidate/`。减少的两类工作：每个图像请求在构造时仅规范化/校验一次aspect/mip/layer（DS耦合与3D切片规则不变），后续依赖两遍扫描与记录复用该不可变范围；29项纯诊断计数由唯一录制owner累积，在物理commit发布原子快照，getter只读快照，不参与同步/引用/dirty代号控制。没有删除实际访问阶段/上传/读回检查。Host异步872841/同步775439、Mali876691/Adreno875915 checks；真实Mali同步验证PID6976零VUID/SYNC，fresh save2后停止。

高通d364→814→d364官方采样与Perfetto对照均原生2750×1260、fixed3D1，普通工具前FPS分别20.607/20.607、20.607/20.619、20.572/20.524；实际replay running32.470/33.187/35.257ms。第三次变慢，因此**不能宣称已稳定降低整段CPU时间或提升FPS**。同一协议样本reference的range self指令份额2.69%、候选1为0.85%；原子RMW reference0.62%、候选1为0.12%（更早独立baseline2.38%不作为本次配对基线）。样本份额并非绝对时间节省；计数发布机制和少做校验的机制成立，保留代码继续消除热点。总指令采样窗口约17.16B→16.96B/16.86B，也不精确等同每帧成本。对照汇总 `.local/logs/translation-work-735-comparison.json`，普通结果与工具采样成本分开记录。

GPU路径已使用当前实际帧1502官方GFXR及gfxrecon-extract导出135模块、按实际specialization值编译63个FS/CS，确定2351draw/11compute/52renderpass/8submit。资源回放在第1521张快照报DEVICE_LOST，恢复wrapper只用于原始API/SPIRV分析，不冒充完整成功回放。当前帧主HDR645draw确实depthWrite=false、stencil全KEEP+mask0，而规划将stencilEnable误判为写；该通用只读分类修复及动态mask/跨jump用例已实现待最终部署。两空clear pass在旧不同RT pass仍active时错过隔离，仍LOAD/STORE无关RGBA/DS；其17.19MB/frame为未压缩逻辑读写上限，非实际DRAM计数。新clear在原本不可复用且必须end的位置重新验证proof后隔离，主机含新DS用例901852 checks通过，最终真机收益待采。

现有inline_c1在真实specialized shader会减少183个静态C1 load站点，但主模型spill188→228B/thread、植被348→424，同时另变体160→140，存在资源tradeoff，本轮保留诊断不默认部署。工具缓存命中遗漏完整C1输入的正确性缺口已经修复：同控制段/code但不同C1必须miss，完整C1相同才能hit，测试含末尾字节、±0/NaN payload及legacy输入。当前已安装shader链未变。证据汇总 `.local/logs/current1502-gpu-hotspots-evidence.json`；阴影数组GENERAL根因另见 `current1502-shadow-array-layout-evidence.json`。


上述CPU三次官方Perfetto的同TID调度区间与官方cpufreq轨相交，整16秒记录的运行时间加权频率约1719.9/1585.9/1545.1MHz，核心分布也不同。它解释了测试条件存在调度/频率漂移，不能单凭最后running变长就判定候选增大CPU工作，也不能按混合核心平均GHz把时间换算成“固定频率提速”。官方simpleperf指令窗口和热点消除单独记录，后续仍需在真实CPU热点继续减负。

### 资源用途与物理Pass收尾的真机结果（2026-10-18）

最终8b86a6e4资源候选已部署验证：host异步1129835/同步1032433、Mali1133685/Adreno7351132909 checks，真实Mali营地同步验证零VUID/SYNC，完成原生命周期存档2次后停止。包含前述stencil实际写入判定、不可复用Pass末尾clear隔离、单mip深度数组2–16层使用既有整图optimal布局，以及只读DS布局转换覆盖传统STORE写入的stage+access证明。最初5ba候选在两真机新增clear用例中暴露STORE WAW，修复后才部署8b86；保留首轮失败摘要，不把早期host通过当作真机正确。

官方Arm Streamline/Performance Advisor的d364→8b86对照保持2772×1240、fixed3D1、shader链3b0e470fc91d、bloom关闭，按实际LWI帧归一化并截取2–10秒：外部读取300.41→295.40MB/frame（−1.67%），外部写入181.83→175.49MB/frame（−3.49%），合计482.24→470.89MB/frame（−2.35%，约11.35MB/frame）；GPU周期33.05→32.59M/frame（−1.39%），fragment周期−2.76%，LS/纹理周期基本不变。指标有重叠，不能相加；instrumented PA帧率27.1→27.4不是普通运行涨帧证明。原始APC及官方报告在 `.local/profiles/gpu-layout-mali-{before,after}-run1*`，汇总 `.local/logs/gpu-layout-mali-arm-summary.json`。

随后bd0ba93b冻结APK/6abfd5c0 ELF及341源码加入两项：

- 同一个物理录制owner的单64页bitmap word访问，直接检查/合并精确stage位和initial上传/完整覆盖位，一次循环替代反复扫描；跨word、未知范围或reference仍原路径。没有跳过真实脏页/上传/依赖语义。
- 仅显式支持并启用VK_EXT_load_store_op_none时，对于整个物理Pass从未写入、LOAD且始终DS_READ_ONLY布局的深度/模板附件，收尾用STORE_OP_NONE而不制造额外STORE writer。Mali当前不支持该extension，保守STORE；Adreno735支持。任何clear/真实writer仍走原保存路径，不能以DONT_CARE代替保留旧内容。

host异步1262025/同步1164609、Mali1265875/Adreno1265099 checks及真实Mali同步验证均通过。普通工具前2×8秒：Mali27.695/27.771 FPS，高通19.965/20.679 FPS，原生输出及世界速度保持。高通每8秒实际NONE命中1922/1699次；pool proof复用2609435/2357093次，说明适用性但不冒充CPU时间节省。官方simpleperf+Perfetto下，Mali429个replay平均running27.384ms，高通330个32.228ms；带双事件99Hz DWARF采集成本且频率/核心分布可变，尚不声明稳定整帧收益。两台采集后均新存档2次并停止。

Mali该Perfetto出现9217项systrace_parse_failure，已用官方trace_processor的ftrace_event/args逐项汇总：恰好9217条vendor非atrace字符串（fpsgo/fstb/thrm_aware等），无正值sched/ftrace丢失错误；并非应用replay/sched轨丢失。分类SQL和结果在 `.local/logs/pool-word-none-mali-trace-health.*`。仍保留工具开销及频率漂移的限制。

### 单次绘制的目标比较候选（2026-10-18）

官方高通simpleperf旧8b86调用地址中，include_depth和最终物理Pass复用处344字节目标memcmp分别约1.032%/2.790% instructions样本。新代码只在同一次vk_draw的栈上，实际active_job、物理Pass号及pipeline epoch均未变时，复用此前完整memcmp的精确结果；首次仍完整比较，资源解析/布局/实际依赖照常执行。不跨draw缓存：SET_RT直接改变S.targets并不改变pipeline epoch，不能据此构造全局证明。clear传NULL继续原比较，嵌套ghost或Pass变化会失效。

新GPU用例直接在不改变epoch时切换对象/mip、包含/省略DS、扩大viewport使物理Pass改变，检查全部颜色和原DS，另有已有真实ghost重入用例。首次测试误把扩大scissor当作扩大renderArea，已按实际viewport修正用例，生产代码未因此改变。冻结adf11e61 APK/716dfcf4 ELF及342源码在 `.local/build/target-match-candidate/`；host异步1268468/同步1171052、Mali1272318/Adreno1271542 checks和真实Mali同步验证已通过，零VUID/SYNC。普通高通20.607/20.643 FPS、Mali27.306/27.272 FPS；官方采样对应target memcmp指令份额高通1.722%→0.674%、Mali1.114%→0.442%。replay running高通32.228→32.022ms、Mali27.384→27.456ms，不能据此声明整段CPU时间稳定下降；保留减少重复比较的机制。两台均存档2次并停机。汇总 `.local/logs/target-match-summary.json`。


计量口径补充：上述bd0/adf的SurfaceFlinger 8秒窗口在99Hz DWARF/PMU采样之前、无GPU抓帧layer，但控制器同时采官方轻量16秒Perfetto（调度、app marker和FrameTimeline），不能称为“所有工具完全关闭”的FPS。已直接分析既有run1.pftrace，不重新采基线：无PMU/DWARF时bd0→adf的replay running高通32.464→31.335ms、Mali24.899→26.250ms；结果并未显示两机一致的整段CPU时间下降，核心/频率/阶段漂移仍需考虑。此前32.228/32.022和27.384/27.456ms属于后续带PMU/DWARF的独立窗口，不能混用。CPU热点的指令样本归因和目标比较机制仍成立。

### 规划与视图元数据再减负（2026-10-18）

3b221a0a APK/7d59ae6e ELF及343源码冻结在 `.local/build/store-recipe-candidate/`。未知shader阶段的全零证明改为在原本必需的160槽保守扫描中学习；任何同stage TEX/IMG绑定写入失效，不再为已知shader每个绑定维护populated计数。纹理view在全部上传/layout/ghost回调后，重新检查缓存槽完整身份（physical serial、登记epoch、id、storage、owner、物理image和view句柄），完全相同才省略160字节recipe重写。动态上传和hazard照常执行，不按“上次命中”推断资源状态。

新增独立命令内存用例：TEX127/IMG31的empty→nonempty→empty经CALL/JUMP展开，与独立store_scan逐操作核对且执行真实GPU像素；另外用原ghost确实重入draw并写入碰撞槽，校验post-callback连续命中和Storage/mutable升级后的GPU内容。早期用例将未声明packaged layout的普通纹理当成可CPU重上传源，gdb确认image_upload正确提前返回；修正用例输入声明后保留严格draw+1断言，未改生产上传语义。host异步1271395/同步1173979、Mali1275245 checks，真实Mali同步验证零VUID/SYNC，fresh save2后停止。Q由官方GPU工具独占，本独立包未部署Q，后续组合包覆盖。

Mali3b22两窗26.372/26.221 FPS，回装旧adf包两窗26.039/25.994 FPS，均有轻量Perfetto、无PMU采样/GPU layer；因此不能把按时序降低的FPS直接定为3b22代码回归，也不能宣称已有涨帧。官方core-frequency轨中三个连续候选的常用CPU4–6平均运行频率大致1.9→1.82→1.73GHz，3b窗口温控status为1而先前为0；CPU实际running与FPS的因果仍不能仅用混合核平均频率归一化。另确实发现新动态memcpy在store_index绑定处占0.760%指令样本，移除的Mali计数原占0.620%；下一组合候选对n==1直接复制u64，其他数量保留bulk memcpy，避免把libc小复制成本当作无代价。

### 紧凑驱动常量键（2026-10-18）

官方调用栈中的另一个重复成本是构造、比较、散列含大量固定零的0xa00 c0/c2驱动表。新实现每次从真实当前S取全部可变字段，形成精确键：VS/FS c0=192B，CS c0（SSBO+UBO）=384B，c2（128 tex+128 samp+8 image的低32位）=1056B。slot/stage独立且全字节比较，不用脏标记预测；direct S恢复同样生效。仅miss时从冻结键展开原0xa00 GPU表，完整64KiB尾部和arena生命周期不变。命中省去零填充/稀疏表构造，memcmp字节数分别减少92.5%/85%/58.75%；历史hash由320步缩至24/48/132步外加slot/stage种子。代价是miss先收集紧凑键再展开，未声称零代价或已涨帧。旧driver_reference仍独立构造完整旧表，用于对照。

独立VS/FS/CS shader把两张完整64KiB表全部写到owned输出，按原ABI坐标核对所有word，包括真实64位地址、size/handle低32位、边界槽、固定零洞、64KiB尾、直接S恢复、同submit六张A/A/B/B/A/A不可变快照，以及reference→candidate→candidate→reference。支持不足设备明确保留既有FS像素用例而非冒称三阶段通过。新增fixture在原资源live→zero宽限期断言后运行，避免增加提交改变旧断言生命周期。

冻结108b44bd APK/d058a1f1 ELF及344源码在 `.local/build/compact-driver-candidate/`；host异步3630732/同步3533316 checks，零VUID/SYNC。owned155文件素材包、生成器及输入哈希另冻在该目录fixture/，tar SHA7d6d5869…。当前真机验证进行中，禁止把host检查数替代原游戏结果。

### 当前高通官方驱动编译证据（2026-10-18）

Snapdragon Profiler当前d610链Vulkan Snapshot已完整成功（frame711，原生2750×1260/fixed3D1）。真实执行2343 draw/11 dispatch/8 submit/52 pass；工具面板96 pass包括未执行录制，不能直接采用。三主HDR .spec模块SHA和SpecId0=1逐项匹配，draw321/175/87。243组实际驱动编译统计中，83个片元管线scratch均为0；三主HDR最大occupancy50%，静态texture-read指令41/42/38，总指令3227/3188/2959。因此不能把Mali r56 MOC的spill结论外推到Adreno当前驱动。Memory-read计数0是工具的指令分类，不能解释成没有UBO/纹理读取；静态计数不等于动态实际访问量/耗时。工具未提供机器ISA。

诊断APK388c0b8d仅添加官方要求manifest权限，target36保持，逐ZIP payload证明native/Java/assets完全相同；旧权限声明不等于获得新版Android legacy存储访问。启动失败另有官方service的长参数截断，按冻结Java默认值证明后缩至145字符，保留原运行行为，未取消正确性选项。完成后原生命周期fresh save2，退出工具并恢复adf APK/layer。主证据 `.local/logs/current-q-sdp-main-hdr-exact-stats.json`，完整官方会话 `.local/profiles/current-q-d610-official-frame711.sdp`，恢复记录 `.local/logs/current-q-sdp-session-restoration.json`。

### 2026-10-18：compact driver 真机结果与下一批验证边界

108b44bd/d058a1f1 的 compact driver 缓存保持原 GPU ABI 全表，只缩短 CPU 比较键，并在 miss 时展开原表。Q 的全表 VS/FS/CS 用例通过；Mali 的后续 fb8cb058 测试版本使用不依赖图形 storage atomic 的整数附件，完整通过 3,634,587 checks。Q 的该新增点图元用例失败仍待诊断，不能把旧 atomic 路径通过当作新点图元通过。

同版本真实原游戏，原生输出、原 shader chain、bloom 关闭，Q 20.572/20.583 FPS，Mali 26.898/26.713 FPS；这是 SurfaceFlinger 8 秒窗口并行轻量 Perfetto，无 PMU/DWARF/GPU layer，不是完全无工具测量。无 PMU 的 replay running 为 Q 30.942 ms、Mali 27.180 ms；不据此宣称相对旧版本整帧稳定提速。详细证据 `.local/logs/compact-driver-summary.json`。

下一批冻结 986ee752 APK/d150f4dd ELF（347 源输入）位于 `.local/build/texel-storage-v1-candidate/`：CPU 按需解码 replay words，以及对全部保留 writer 的可见阶段取交集，仅在证明不存在依赖时跳过区间查找。GPU readonly RGBA32F texel-buffer→SSBO 为严格格式/范围/对齐/描述符预算受限的可选配对，默认关闭；原 base/spec 保留，实际绑定不满足条件便走原路径。不改原游戏 binary 或玩法。host 异步 3,635,641、同步 3,538,222 checks 零 VUID/SYNC，仅是底层用例证据。

首轮真机：Mali 已通过新的 writer 可见性交集用例；之后由于控制脚本误判 tar 根目录，53 素材解到了错误路径，未执行 GPU 新路径，需纠正解包重测。Q 在新增 sparse fill raw readback 用例失败，正在核查测试本身的 TRANSFER→HOST 可见性，尚不能归因于生产可见性优化。两个进程均已自行结束，原始失败日志保留 `texel-storage-v1-{mali,735}-first-validation.log`。不报告新 GPU 或 FPS 收益。

纠正测试部署目录后，986ee752 在 Mali 完整通过 3,639,491 checks，含新 CPU 与 53 GPU 用例，零 VUID/SYNC。真实原游戏原链、默认关闭 GPU alternative，在 2772×1240 下通过同步验证；存档两次提交后退出。随后轻量 Perfetto 的 SurfaceFlinger 窗口为 27.091/26.895 FPS；无 PMU replay running 27.076 ms，对比108b27.180 ms，不宣称整线程稳定提速。匹配 ELF 的官方指令样本中 buffer_write_start self 3.777%→2.727%，包含同函数其他 self 总计4.636%→3.872%；相对份额不能直接折算绝对毫秒。

Q 的 sparse fill 检查仍未定因：代码复核发现 commit 原本已有无条件 TRANSFER_WRITE→HOST_READ barrier，不能将失败直接归为完全缺少 HOST 依赖。80d9f580/78c77859 下一冻结 APK 增加 rawcopy 后精确 readback-buffer HOST 依赖和失败专用逐字节/source-map/guest 数组导出，保留严格断言，不改生产依赖判断。该版本也加入 54：真实 GPU typed base 与 storage alternative 对 NaN payload、Inf、正负零、subnormal 等 raw32 位模式比较；host已通过，真机待测。

54 的 Mali 真机结果否定了“仅格式匹配便可对整个 RGBA32F binding 等价替换”的假设：输入 signaling NaN 0x7f800001 时，原 typed fetch 变为0x7fc00001，而 SSBO load 保留0x7f800001；负值同理。严格 GPU 位比较失败，方案保持默认关闭、新游戏链仅离线生成，未部署。进一步官方 SPIR-V 数据流审计显示当前真实 HDR 三 shader 的16处fetch中有6/6/7处结果可直接到整数位观察（bitfield、mask、shift、比较），不能把此差异当作纯不可见 NaN payload。证据 `.local/logs/texel-storage-current-hdr-bit-observability.json` 与 `.local/logs/texel-storage-v2-mali-validation.log`。后续如实现，只考虑可证明的数值使用sites并保留packed使用的typed路径，必须重新计入描述符/Shader代价与真机验证；不加游戏键或供应商NaN猜测补丁。

高通 raw fill 导出显示 actual 与 GPU source-map 仅首个u32为0，期望0x91ab0000，其余1024个u32（含间隙）精确一致；guest仍全零符合该rawtest未注册guestwriteback的设计，不能将guest整体零误判为所有GPU写丢失。当前未找到生产common visibility改变任何对应Vulkan命令的证据，继续在严格失败前做现有工具未覆盖的最小owned GPU对照，不宣称已定位。

80d9f580 高通原游戏验证进入2750×1260营地，但同步层报VUID-vkCmdDrawIndexed-None-06479：R16_UNORM view用于depth comparison。旧1b1881a1 bloom-quarter-v2原游戏验证日志已有同一VUID（`.local/logs/bloom-quarter-v2-735-game-validation-result.json`），故当前证据支持它是既有兼容缺口，不可记作本轮CPU候选引入，也不能将该Q验证报告写成零错误。本次原游戏已正常完成两次存档提交并停止。Mali986ee752原游戏零错误结论不变。

保守mixed读取的离线评估：仅7/16fetch可证明数值用途，3个HDR的静态LS成本下降约5.2%/6.1%/8.5%，但spill bytes分别188→292、348→400、160→200，且新增descriptor和ALU成本。它仍只作为离线实验保存，未扩展生产/未部署，并非因FPS没涨而丢弃；当前存在明确资源代价和全binding语义失败。后续GPU分支改为依据官方Mali TE机制验证shader呈现候选，保持原NVN源图和原生输出，不改游戏pass或玩法。

### 2026-10-18：CPU首次初始化证明与shader呈现候选

CPU连续页证明缓存只记“已完成初始化”，每pool16B，无内容新鲜/所有权含义。miss保留原initialized_pages扫描、guest/shadow首次拷贝；相邻或相交才合并，分离区间替换，不能跨洞。后续CPUdirty、GPU同步、writeback不变。已存在官方样本中该扫描约占Mali1.43%/Q1.48%指令样本，新增CPU热路径仅包含区间比较。

GPU shader-present仅为host通用呈现助手：同format RGBA8_UNORM、原尺寸完整单层/mip/单采样才可用；新增一个全屏triangle，保留原NVN源与未来读取、真实fragment依赖及WSI acquire等待，目标view/FB按swap image复用，shader通过现有官方glslang/spirv-val离线生成并纳入APK来源记录，不改游戏shader链。每swap image只在提交成功后记PRESENT布局，重建清空，双队列idle后销毁。默认OFF；ON启动预留COLOR_ATTACHMENT用途后允许同PID OFF/ON。它不删除源读和输出逻辑量，只争取TE/实际写带宽机会，必须看原厂计数，不许宣称理论字节等于外存节省。

冻结c6e476de APK/41994fc5 ELF、358输入位于 `.local/build/present-span-v2-candidate/`。host async3,679,767/sync3,582,348全部通过，零VUID/SYNC；Mali与Q新CPUspan和两种native尺寸shader-present用例均通过，包括全部RGBA字节/alpha/行、原LINEAR blit逐字节对照、后续NVN源读取、view复用、格式/resize/crop/MSAA/usage回退。**手机全suite仍非通过**：Mali后续54保留sNaN严格失败，Q后续rawfill首u32严格失败；不把单项通过冒充全套成功。详情 `.local/logs/present-span-v2-device-fixture-summary.json`。实际AndroidWSI与原厂计数仍待下步。

前一host sync失败是新测试误用已有串行整页上传语义：rawGPU-only16B不writeback后CPU改邻接8B，serial按原策略回传整页。修正仅限fixture：所有模式先严格验证cachedlookup跨提交保留GPU值；整页模式正常writeback16B后再改CPU邻接，async精确changed-word路径仍保持无writeback的强检查。未改变生产上传策略。前一失败APK5b01e54e归档未部署。

### 2026-10-18：出图路径硬件计数器与线程退出栈

冻结 C6 的原厂 Arm OFF/ON/ON/OFF 重试完成（`present-span-v2-mali-arm-retry-assessment.json`）。按 CAM 完整帧归一后，shader-present 的外部写入均值约 166.750→166.040 MB/帧，总外部读写约 461.200→459.873 MB/帧；两次 ON 都比两次 OFF 有更多 TE killed tiles、较少写入。幅度很小，不能把理论读写的 27.50 MB/帧视为实测节省。各捕获均报告丢弃一个 CAM job，计数器包含整帧，不能作为独占出图耗时。GPU cycles 有漂移，普通 SurfaceFlinger ABBA 接近持平，尚未证明稳定涨帧。

工作区将自动启用限制在已验证的 Arm Mali-G710、原生 RGBA8 同尺寸完整出图路径；GPU 类型只在创建 swapchain 时判定一次。其他设备默认原 blit；显式开关和不满足格式/裁切等条件的回退保留。新增成本为每帧一组绘制/descriptor 录制，静态 PSO/View/Framebuffer 复用；没有新增全屏中间图或 CPU/GPU 往返等待。此默认策略仍待新 APK 真机验证。

首次原厂采集重试前曾在 libc 的 pthread_exit/TSD allocator 清理期间栈溢出，尚未取得 GPU 数据。Android HLE ExitThread 现保存并恢复原宿主线程 SP/FP/LR 后执行已有清理，避免沿游戏小栈进入 libc；不改变游戏线程入口、TLS 或游戏 binary。独立原生 HLE 测试已在 Mali/Adreno735 通过，包括 16KiB 有保护页游戏栈、96KiB TLS 析构器栈使用及主线程回退。该测试不等于实际游戏验证；新 APK 尚待部署。

采样专用颜色数组的只读布局候选已完成 host async/sync 全套验证；覆盖格式、层数边界、mip、同提交 transfer pin/recovery 和真实 storage/attachment 首次升级。仍保留逐子资源 hazard 与所有不符合条件的 GENERAL 回退。真机验证和性能收益待测。

### 2026-10-18：数组布局与提交索引计数精简验证

`sampled-array-exit-v1`（APK `50ce622a…`，360 个源码输入）已在 Mali 与 Adreno735 各通过 20 组颜色数组真实 GPU 检查，涵盖 R8/RGBA8/sRGB、2/4/256/257 层、各 mip、同提交 transfer 后的缓存 descriptor 复用以及 storage/attachment 首次升级。新数组测试前的初始化 span 与 shader-present 也通过。完整设备套件随后仍在已记录的独立问题停止：Mali signaling-NaN typed texel 与实验 SSBO 位型差异、高通 raw fill 首 word；未放宽这些断言，不能写成全套通过。

相同 APK 已执行原游戏模块：Mali 默认 G710 shader-present 的营地同步校验无 VUID/SYNC，后台原生命周期存档并重建 Surface 后在同一 PID 恢复，最终存档两次并停进程；高通默认保持 blit，普通窗口 20.69 FPS，最终同样存档两次停止。完整同步验证下的 Mali 10.341 FPS 带校验开销，不可与普通帧率比较。图像目视未见营地缺块，不代表所有游戏场景均已验收。记录见 `sampled-array-exit-v1-{device-fixture-summary,mali-game-validation-result,735-default-result}.json`。

CPU 提交索引构建改为在成功 append 且已去重后维护每桶计数，前缀和阶段再转换为绝对 end。删除整遍事件计数扫描以及 16KiB 的 counts 栈数组和零填充，无新增字段/原子，稳定 scatter、别名、epoch/容量失败回退不变。新增成本是在 append 时更新既有 bucket.end，替代后续 counts 更新。host async 16,591,198、sync 16,493,779 检查通过；各 573 次独立 raw scan 比对零差异、零 Vulkan 校验错误。`store-append-count-v1` APK `ea21b262…` 仅此生产差异，真机 CPU 对照进行中，不预先宣称时长或 FPS 收益。

CPU 单变量真机对照完成：50ce→ea21 的 source manifest 仅 `nvn_vk_store_index.inc` 不同；高通原游戏两窗口为20.772/20.747→20.714/20.690 FPS，没有稳定整帧收益。官方 simpleperf 的旧计数遍历源码与机器码消失；规划指令份额4.309→4.126%、周期份额2.431→2.126%，这些是采样占比而非绝对时长。无 PMU 的普通 Perfetto 窗口实际翻译 running 为34.520→34.495 ms，接近持平；运行期间加权频率1507.6→1530.6 MHz。保留可证工作量删除，不把它夸大成整线程提速。

颜色数组跨 APK 官方对照不支持带宽收益：C6 OFF两窗均值总外存461.200→50ce OFF的474.881 MB/帧（+2.966%），纹理 external+0.266%近持平、LS external+13.512%；两新 GPU cycles 落在两个旧端点之间。对照已匹配 native 分辨率、链、场景、blit模式和 swapchain usage，但仍有 PID/温度等差异；不能声称数组导致回归或已降低 DRAM。新候选将增加仅控制颜色数组的提交边界快照，以同 APK/PID 原厂ABBA单独核验，保持已有 mip/depth/compute 策略不变。Arm 的架构说明也不支持把只采样图像 GENERAL→READ_ONLY 简单当成创建压缩布局的开关。

### 2026-10-18：CPU/GPU 并行候选与同进程工具结论

CPU `target_fields_equal` 只比较真实有效附件、非空目标的 has_view、以及已启用的完整28B view；未启用槽/空目标的 view 数据及结构padding不再参与是否拆pass的判断。每draw现场验证与原局部 proof 失效条件不变，资源解析、上传、layout、hazard照常。原 DRAW_STATE_REFERENCE 保留344B raw比较。真实GPU draw/clear覆盖无效字段污染和 texture/mip/layer/depth 改变，host async/sync/raw-reference全通过；两Android GPU新用例也通过，Mali原游戏同步校验与Surface恢复通过。

高通 ea21→999 采样：目标比较及局部proof指令份额0.516→0.339%，新比较器self0.205%；旧正常路径344B libc memcmp来源消失，present view比较仍精确且被编为寄存器操作。普通Perfetto running34.495→32.312 ms，但频率与异构核心驻留变化，不能将全部减少因果归新比较器。SF仍20.7附近，未证实整帧增长。

通用 buffer-only 候选已按物理VkBuffer+pool唯一identity追踪producer；barrier覆盖当前ctx保活的真实VkBuffer，visible只发布到完整覆盖、producer stage全涵盖且物理身份精确匹配的历史，重新计算common交集。未知暂存/身份混合/旧generation/超过预算仍回原global。图像依赖不变。不可仅凭guest地址判断物理buffer；原删除后同guest地址注册已在真实GPU fixture核对。新增代价为producer记录24→40B（256项多4KiB）、pool8B identity、owner查找与最多16资源的核对；较严合并可能增加history/后续barrier，不能声称命令总数必降。host async16,628,180、sync16,530,761检查通过，独立源码审阅和两Android新fixture通过；Mali原游戏同步校验/同PIDSurface恢复通过。

冻结816291c3 APK、361输入，真实高通普通20.750/20.725 FPS。999→816普通running32.312→34.980 ms，频率1560.3→1519.5MHz、CPU7驻留降低，OS阻塞接近不变；样本没有新增helper热点，但不能据无样本证明无成本，也不能把相对份额下降称整体提速。

816同APK同PID原厂Arm buffer策略 OFF/ON/ON/OFF 全完成：PSO176和specialization variants32每窗稳定，完整LWI帧215/210/204/207、PA/raw归一吻合、各CAM仍警告dropped1。OFF→ON均值GPU33.377→34.017Mcycles(+1.917%)，总外存469.476→470.160MB/帧(+0.146%)，纹理external+0.198%、LSexternal+0.115%。外存ON均落OFF端点之间；L2flush次数近乎不变，且它不是texture-cache invalidation计数。温度有顺序上升，但不能将所有周期增加一概解释为热。当前没有已证净GPU收益；候选仅具备精确资源声明和有限维护潜力，额外成本必须保留记录。详见 `buffer-scope-v1-mali-arm-official-comparison.json`。游戏已原生命周期存档两次后停止、app UID无gatord遗留。

数组999同PID只有OFF/ON两份有效，不能包装成ABBA：总外存+0.376%、纹理external+0.255%、LSexternal+0.611%，未重现此前跨APK显著增量，也无已证带宽节省。首窗口variants30→32，后采更热，限制已保留。第三份gatord退出超时，不是游戏崩溃或ADB断开；原存档成功，三owned工具PID清理。采集编排现按唯一output token及app UID清理该工具子进程，再恢复perf权限，不另写计时器/采样器。

现有Android Performance Hint接口在999高通普通ABBA为OFF20.725/ON20.679/ON20.223/OFF20.690，API报告active且零失败但无稳定涨帧，原默认OFF保留。未新增绑核/时钟/温控控制，未实现帧丢弃或多线程翻译。≥30FPS原生目标仍未达成。

816最终无GPUlayer/PMU普通窗口：Mali2772×1240、固定scale1为27.176/26.851 FPS，高通2750×1260为20.750/20.725。均原生命周期新存档两次后停机、部署SHA重新核验816一致。不能把已有局部工作减少写成稳定整帧增长；≥30目标仍未达到。下一CPU候选仅为prepare/view真实MISS之间的descriptor值快照交接，旨在省重复registry锁与复制，尚未构建或验收；动态上传/布局/hazard不会被缓存跳过。完整交接见 `.local/logs/parallel-cpu-gpu-20261018-result.json`。


### 2026-10-18：描述符 MISS 快照交接与并行验证

CPU 候选在单次图形/计算 prepare→bind 作用域内交接按值复制的逻辑 descriptor，仅在原物理 recipe MISS 后尝试复用，省第二次 registry 加锁/取值；实际 VkImage/owner/handle、上传、动态 Storage 升级、layout 和 hazard 检查保留。按真实 descriptor id/storage、当前 recording serial、descriptor epoch、作用域 generation 全值校验；嵌套最多8层、每层128 direct-map槽，冲突/溢出/分配失败回原路径。consumer 先复制到栈上再允许回调，不能持有 registry/vector 指针。epoch load 是并发线性化点，不声称保留“等待已取锁但尚未发布的并发 writer”的锁次序。

新增成本为 prepare MISS 额外写184B descriptor 与32B身份、每次作用域/查找检查、最多216KiB缓存；consumer 本来的184B局部复制仍在，不能写成减少总复制字节。源码独立审查 `.local/logs/descriptor-miss-handoff-independent-review.json`。冻结6c0843ae APK/bfac6e67 ELF及361源输入于 `.local/build/descriptor-miss-handoff-v1-candidate/`。host async16,628,198 / sync16,530,779 checks 零VUID/SYNC；两台实际GPU的新handoff用例均通过，包括registration、槽冲突、嵌套/溢出、跨物理提交及独立compute内容。调用顺序将本用例提前至已知独立失败之前，原断言均保留。全Android suite仍未通过：Mali后续54位型比较失败，高通后续raw-fill首word失败；不得以新marker替代全套通过。记录 `.local/logs/descriptor-miss-handoff-v1-device-fixture-summary.json`。原游戏与官方CPU对照正在执行，尚无净CPU/FPS收益结论。

并行的GPU离线审计纠正一个语义前提：上游Metal是 `texture_buffer<float>.read()` 后 `as_type`，不是直接raw指针load；现有机器没有独立Metal/NVN硬件oracle，不能把旧Vulkan或raw路线任何一方当作已证明正确的参考。formatted storage-texel read的三个实际HDR编译结果与typed fetch完全一致，停止该无收益方向。整数quiet-bit raw-load探针仅显示静态LS减少约19–24%，同时ALU增加约19–20%，模型/alpha spill增加；不证明格式语义，不修改54、不部署游戏实验链、不加生产NaN补丁。证据 `.local/logs/texel-storage-independent-oracle-audit.json`、`.local/logs/current1502-typed-storage-texel-offline.json`、`.local/logs/current1502-raw-quiet-load-cost.json`。

CPU历史跨版本frequency比较还有工作量边界：ea21官方采样期间PSO168→175、variants30→32，属于热身增长；999与816各自稳定但实际PSO/variant集合不同。旧sched×frequency相交只能说明所采时间内的跑核/频率，不能据此证明版本代码变慢或变快，也不能用频率归一化消除工作量差异。后续结果须先审采样metadata before/after，而非只读launch ready-state。


描述符handoff的6c0843真机后续：Mali原游戏同步验证零VUID/SYNC、同PID Surface恢复、两次原生命周期存档后退出；普通无PMU/layer两窗27.493/27.464 FPS（2772×1240/fixed scale1）。Q真实原游戏20.690/20.690 FPS（2750×1260），官方CPU采样后save2退出。新旧整线程ordinary Perfetto running34.980→33.665ms/replay仅为描述：新run1内PSO169→176/variants30→32，不能将差值认作净CPU加速。随后PMU自己的窗口，两边各自175/32→175/32、176/32→176/32稳定且Samples lost0；可用于热点形态审计，仍不证明精确workload相同。新view fallback锁仍被采到，不写“锁消失”：instructions份额CAS+unlock约0.7344→0.4809%，新增scope/snapshot检查及prepare额外184B写均须计入；view self cycles份额反向增加，不能相减份额得出毫秒。证据 `.local/logs/descriptor-miss-handoff-v1-vs816-cpu-frequency-audit.json`、`.local/logs/descriptor-miss-handoff-735-lock-pc-audit.json`。当前至少30FPS目标仍未达到。


下一CPU候选为延后descriptor payload：现有精确values key、dynamic offsets及每次资源依赖工作原样保留；last/history均MISS后再按kind序列化到原24B union payload。命中避免原buffer/image24B（texel8B）无用stores，代价是MISS新增一次nb≤128的kind循环，没有新缓存。shader中buffer/object-offset-range、image/view-sampler-layout互换及texel句柄逐项独立审查通过；template/plain仍消费原union布局。不是延后真实资源准备或预测数据。真实GPUfixture扩为template/plain×texture-cache/reference，原dynamic/history碰撞像素用例继续验证；尚未构建。审查 `.local/logs/descriptor-payload-deferred-independent-review.json`。


### 2026-10-18：CPU payload 与 GPU 不同 mip LOAD 的实际验收

冻结3cc2aede APK/4efd6ce2 ELF、362源输入。payload仅driver MISS物化（全部资源准备与完整key仍执行）；mip LOAD豁免仅sampled-image kind3/6、同serial且原mem/d/L完全一致、原2D单层单采样2..16mip、明确不交view范围。保留原full-cover几何/状态证明，默认all-mips/非法或重解释view/storage/不同serial guest别名保守LOAD。descriptor epoch在prepare/bind前记录、豁免时及扫描后复核，防止已绑定旧view而cover读到新view。新增CPU成本为epoch读取、原描述比较/范围判断，无新GPU对象。

host async16,660,724 / sync16,563,305 checks零VUID/SYNC。两机真实GPU均通过template/plain四模式、marker39/47动态Uniform与history碰撞、12draw双向mip与guest别名、全部sibling mip像素；后续仍因原Mali54或Q rawfill严格断言而全suite不通过。Mali原游戏sync/resume/save2通过。Q普通两窗20.654/20.030，replay running约33.807ms仅描述；该Perfetto有20个systrace_parse_failure，不能据此报净CPU改善。源审、机器码与每项代价单独记录，不因未涨帧撤掉可证明的无用工作削减。

官方GFXReconstruct当前原游戏单帧已捕获，不重放。原始trace297,747,845B，SHA d523b3a1dd87cfa4e0bcb5571b08353352c034de6cafb9e6879d84ed13d31432；game save2/stopped、全部layer settings与properties恢复后才整体拉取，远端/本地hash相同。执行编排源码是独立冻结b642版本 `.local/build/capture-payload-mip-cover-api-b642-frozen.py`，运行中路径曾被补guard，纠正记录保留，不能把后改版本冒作执行来源。

当前官方API按物理image描述与producer/consumer匹配旧1502：同2772×1240、8mip、R11G11B10单层图，写mip1..7的7组全部LOAD→DONT_CARE；每组仍一个indexCount3/instance1三角形、两个sample view仍为同图前一级levelCount1。逐组viewport/scissor、vertex/index offsets、所有非handle管线状态一致。official extract的VS/FS字节hash与旧版完全相同，未删目标draw、未换Shader或采样资源。省4,581,500B/frame逻辑附件load，并非实测DRAM。全帧2401draw与旧2351动态场景/UI不同，不能宣称整帧完全相同。严格证据 `.local/logs/payload-mip-cover-v1-mali-api-load-cover-proof.json`。

本批Arm218完整帧raw归一与PA一致：GPU32.293Mcycles/frame，external461.022MB/frame（read291.631/write169.391），texture external216.758、LSexternal33.948MB。对旧816同bufferON样本观察到external−1.96%、LSexternal−7.33%、GPUcycles−6.42%，但不同APK/热身/CPU改动与动态全帧，不能归因或宣称净FPS。采集期间SF27.385为LWI注入值，不当普通帧率。证据 `.local/logs/payload-mip-cover-v1-mali-arm-analysis.json`。

下一CPU冻结7e6bb578 APK/69e87973 ELF、363源输入：精确covered-read proof上记录实际mip_stamp，仅stamp>本physical pass cutoff省重复mip写；global每次递增及whole image clock、write_clock、visibility/layout失效均不省。增加16B/image与一次stamp比较；request直接最终slot构造按NDK机器码少4指令/40B scratch traffic（20B normalized-range临时仍在）。RT bucket为每build局部9int、每SET_RT重置、逐slot惰性并核真实serial，不丢IX_ATT/clear/alias事件或改变首次bucket顺序。host async16,662,128/sync16,564,709零VUID/SYNC，新的逻辑stamp观测只在TEST_EMBED测试使用，非性能计时器。真机验收进行中。独立审查 `.local/logs/mip-stamp-request-slot-independent-review.json`、`.local/logs/store-target-buckets-independent-review.json`。


### 2026-10-18：当前驱动 SFLOAT 位型全域功能验证（非性能计时）

现成GPU Profile能指出LS成本，但不能证明formatted fetch与整数load的所有输入位型是否一致；因此使用最小独立owned Vulkan功能程序，不增加性能计时/采样器，不执行游戏CPU或修改54/runtime/游戏链。输入在GPU用整数生成，真实FS同时读取同一Buffer的RGBA32F typed texel与readonly uint4 SSBO，记录原typed位型、原raw、明确转换候选；R32UI签名附件检查缺片/覆盖，compute精确归约，主机仅取128B结果/块。四通道分别为32位双射轨迹，默认quick不声称full。数据与Shader/工具/binary都冻结，binary dfa7b51e…，独立审查 `.local/logs/sfloat-exhaustive-probe-v1-independent-review.json`。

真机结果：Mali-G710当前driverUUID e6b85ef2cbd7a3b8478225972ecb928f，quiet规则full4096块覆盖每通道全部2^32位型，共17,179,869,184分量比较，0 mismatch/source/coverage错误，明确PASS。Adreno735当前driver上下文，原raw规则full同样全部通过。反向quick中Mali raw失败4095分量，高通quiet失败4095分量；两机注入错误负控均明确FAIL且coverage/source均0，不把加载失败当负控成功。原始完整log、context、hash、确切计数见 `.local/logs/sfloat-probe-v1-device-full-summary.json`。

结论限定：这给出了当前编译FS探针、driver、format、buffer范围下与既有typed Vulkan行为的等价证据；不需要借Metal/NVN作为这一个较窄命题的oracle，但不能由此宣称原NVN硬件位型已被证明，也不覆盖所有RGBA128组合、VS/CS或任意Shader编译上下文。两种GPU的格式读取行为确实不同，禁止全driver统一替换。任何后续真实游戏候选须限定FS、适用driver/精度环境及确切资源范围/预算，默认关闭、保留54严格断言并额外独立检查真实HDR上下文与Profile；不得以此直接解除通用raw方案的BLOCKED状态。


7e CPU批后续原游戏验收：Mali full sync/resume零VUID/SYNC，普通无层/PMU27.227/26.855；高通20.182/20.679，均原生分辨率、原链、原save2后停机。实际Android ELF反汇编证明三条工作削减均落地，但没有稳定净CPU/FPS收益；保留代价记录，不以相对份额或暂未涨帧决定是否撤回。NEON diffmask四次横向归约变一次的数种隔离-O3写法均增加常量spill或ABI保存，未修改生产，不把源码表达更短当作提速。证据 `.local/logs/image-cpu-batch-735-independent-codegen-analysis.json`。

8fcf5729/1334073b冻结补充：physical image的6项immutable mip-tracked资格在image_allocate（含promotion）一次计算；重复读取只加载cached int。实际新ARM DWARF image仍528B，新int填原尾padding，没有增大image；singlemip恢复原一次cell写、仅多mip查询/发布proofstamp，避免不适用array/singlemip付无收益timestamp检查。host16,662,129/16,564,710及两机新mip clock功能用例通过，后续旧严格失败仍留；高通原游戏20.309/20.750+官方CPU后save2停机，Mali仅functional fixture通过，不冒称该APK已跑Mali原游戏。报告 `.local/logs/mip-predicate-v1-validated-summary.json`、`.local/logs/image-cached-predicate-independent-review.json`。

命令池reset迁移候选c16be4e6/c84e0f21（364源输入）把driver flags0 bulk reset从Nvn reclaim移到既有completion worker，普通GPU+effects completed先通知，再不持completion/resource/queue锁reset，独立reset-ready watermark/retired才授权复用；Finish/Drain另等同一次快照ready，cap16等真实retired，所有graphics/shader/texture metadata.recycle仍留Nvn。减少的是翻译临界路径工作，不减少driver reset总工作；worker可能因此拖后续effects、显式Finish现在要等reset，这些新增成本须双线程官方Profile核对。两个确定性TEST_EMBED gate分别验证pending空但reset中与16→17资源压力，真实GPU写回/护栏核对；没有生产新增线程或自计时。独立审阅 `.local/logs/command-pool-reset-worker-independent-review.json`，官方约束见 https://docs.vulkan.org/refpages/latest/refpages/source/vkResetCommandPool.html 。取消clear的未提交secondary可仍executable，不能声称所有ONE_TIME均已invalid；它们非pending、无跨resource set引用，保持唯一pool访问，相关metadata引用仍活至reset后recycle。真机验收进行中。


2026-10-18 后续验证：c16be4e6 命令池重置迁移候选在 host async/sync 分别通过 16,662,713 / 16,564,870 检查，零 VUID/SYNC 错误；两台 Android 均通过新增的 reset 交接、Finish 等待与 cap16 资源复用测试。Q 的原始 fixture tar 整套仍在旧 line220 raw fill 检查失败，Mali 原始 tar 仅验到新增 marker 后停止，不能声称两套旧 tar 全通过。原游戏 Mali 2772×1240、scale1、原链3b0e470fc91d 完整同步验证和同 PID 前后台恢复通过；普通 SurfaceFlinger 两窗口 27.146 / 27.221 FPS，和之前约27 FPS接近，尚无净涨帧结论。普通运行结束已通过原生命周期 fresh save2 后停机。证据 `.local/logs/command-reset-worker-v1-functional-summary.json`、`command-reset-worker-v1-mali-game-validation-result.json`、`command-reset-worker-v1-mali-ordinary-result.json`；双线程官方 PMU/Perfetto 待完成，不以迁出翻译线程等同减少总 CPU 工作。

RGBA32F 特殊浮点格式转换的显式实验：离线工具新增非默认 `rgba32f_quiet_nan_v1` 配对模型；SSBO 读取 uint4 后仅用整数判断 NaN 并置 quiet 位，再 bitcast float4。原 identity 默认输出保持不变，模型选项及源码进入链指纹。没有修改原 shader/base metadata/spec、runtime 默认开关或 fixture54 断言。冻结 quiet 小 fixture 在 c16 Mali 上整套原 NVN/Vulkan 测试通过 16,666,563 检查，零 VUID/SYNC/FATAL，原 fixture54 的所有 prepare 与 CPU/GPU producer 对照均 alternative_mismatches=0；这是实际 runtime 编译上下文下的 owned fixture 证据，不是原游戏画面或性能证据。`.local/logs/rgba32f-quiet-fixture-v1-mali-summary.json` 关联完整日志及 tar SHA。此前独立格式扫描的每 lane 2^32 覆盖和此 fixture 共同支持严格限定当前驱动/上下文的游戏候选验证；不能宣称所有RGBA128组合、所有 Shader 或原 NVN/Metal 等价。DTS1 本身不含模型/驱动资格，实验必须在外部核完整链与设备上下文，默认仍关闭。完整 quiet 原始加工链与实际游戏对照仍进行中。


当前 c16 双线程官方证据已完成：`.local/logs/command-reset-worker-735-independent-analysis.json`。reset实际调用PC从翻译线程reclaim移到完成线程；普通Perfetto窗口2631次，平均0.18617ms wall/0.18375ms running，资源压力/Finish等reset的正长度trace为零。翻译replay仍约33.77ms running、37.84ms wall，Present另有明显睡眠记录；不把worker正常等GPU fence当翻译线程同等等待，不将nested scope相加。旧8fc PMU窗口PSO/variant增长，与c16集合不同，跨APK事件period和running差只能描述，不能当因果净收益。Q普通窗口20.275/20.643 FPS；Mali27.146/27.221。两机采集后进程/自有工具均退出、perf_harden=1及layer设置已核实。

后续CPU候选 buffer-partition-v1（baff9995 APK/b9836414 ELF/365输入）依据c16 buffer_read内联二分查找占约2.313%指令事件的真实PC证据，新增24B私有单槽精确lower-bound地址分区。缓存仅返回索引，原size/overlap/stages/physical owner全部保留；producer任意geometry改动前失效，barrier在render_end重入后、清表前失效，visibility-only不需要失效。MISS新增至多两端点读取与四字段写，不承诺所有查询加速。host async16,679,143 / sync16,581,300检查通过，零VUID/SYNC；Q新增真实GPU边界/可见性/合并/overflow用例通过，整套旧tar仍在原line220 raw fill失败。原游戏与双线程PMU正在验证，不能将fixture当真实原模块里程碑。审阅 `.local/logs/buffer-partition-independent-review.json`。

GPU quiet原游戏候选链0b9c089f3ea5已从原163项输入经原dmsh_build入口重建并严格验6931 SPV/6931meta/3739spec与3b0相同，只新增显式FS替代模块；27520产物在真机逐件验SHA后原子切换，旧链目录完整保留。2989个被转换FS及2988个specialization输入全部官方disassembly审计，无RelaxedPrecision或FloatControls声明差异。c16 Mali真实原游戏全同步验证通过，画面同一营地视角未见明显缺失/黑块/几何变化（不是全游戏像素证明）。同PID原生分辨率ordinary ABBA OFF27.837/26.977、ON27.274/26.821，ON均值约-1.31%；第一ON有PSO194→201、variant46→48变化，OFF首尾自身漂移-3.09%，因此当前不支持FPS收益，也不作稳定净回归结论。工作路径ON实际选中且fallback0，OFF选中0；采集后原save2停机。MOC真实quiet specialization三HDR显示LS约-18～21%，ALU约+23%，两个shader有更多spill，不能把LS静态减少当DRAM减少或净GPU获益。下一步现成Arm LWI/CAM/PA逐完整帧测量；默认不启此候选。证据 `.local/logs/rgba32f-quiet-game-v1-mali-ordinary-analysis.json` 和 `rgba32f-quiet-chain-v1-hdr-cost.json`。


最新部署5e8f874a APK/a4fbc918 ELF（365输入）在partition之上增加不可变的buffer-only writer分类，原逐draw遍历kind2/8/9+vk_binding_writable改为装载最终active bindings时同predicate派生；base和转换后的texel alternative都重派生。kind4图像写不计入，后续VS动态descriptor与unknown/owner/hazard检查全部保留。原ARM64 DWARF实证has_writable_buffers用0x11c原padding、descriptors仍0x120、vk_shader仍0x268，正式汇编两stage仅加载字段，不再扫nb。增加的是装载阶段少量分类，未增加分配/对象尺寸。host async16,679,143/sync16,581,300通过；Mali qualified quiet小fixture整套16,682,993通过（strict54未改），Q旧identity tar新partition已通过但原line220 fill仍失败。真正游戏保持原3b0/d610链、scale1；Mali全sync验证/同PIDSurface恢复/新save2均通过，普通窗口27.925/27.938 FPS；Q普通20.350/20.560。读数仍未达到稳定30，不单凭Mali较上一轮略高就归因两个CPU补丁；两机末尾均已新save2停机，perf_harden=1/layers=0，无自有工具残留。`.local/logs/writable-schema-v1-functional-summary.json`、`writable-schema-v1-mali-game-validation-result.json`、`writable-schema-v1-device-cleanup-proof.json`。

quiet实验的原厂完整ABBA结论：CAM完整帧220/215/213/189，每组2边界partial。ON/OFF平均LS L2读507.56→428.29MB/frame（-15.62%），但LS external42.27→43.01MB（+1.75%）、总external486.65→488.66MB（+0.41%）；ALU cycles8.862→9.640M（+8.78%）、LSactive+1.60%、GPUactive+3.49%。这是实际工作量取舍，不把静态LS量当DRAM，也不因局部L2减少就称整帧获益；当前保留为显式实验，不默认启用，原3b0完整24533文件hash已验证恢复。单纯取消helper inline的三HDR官方MOC小实验无通用收益，不更换生产pass或重新生成链。原三HDR本就只有main/0calls，不能把代价错误归因于原函数全部展开。首次离线导出同APC并行触发官方工具内部report临时目录冲突（不同output仍共享APC/report），四份采集实际均完整；通过只读原tar核原raw、创建独立raw clones再官方导出恢复，无重采，16份原raw hash不变。证据 `.local/logs/rgba32f-quiet-game-v1-mali-arm-counter-analysis.json`、`rgba32f-quiet-game-v1-mali-arm-isolated-final-proof.json` 与 `rgba32f-noinline-probe-v1.json`。后续只读评估泛化uniform selector机会，尚未改runtime/recipe。


### 2026-10-18：两路并行候选（尚未证明整帧收益）

- CPU：当前 Mali5e 硬件 PMU 确认资源绑定、绘制、未来使用索引仍有 CPU 成本。Perfetto 的厂商标记解析存在错误和同 TID 拆分，原始结果保留；跨 utid 合并只作带条件的诊断，不用于排除同步瓶颈或宣称净提升。独立报告见 `.local/logs/writable-schema-v1-mali-independent-cpu-analysis.json`。
- CPU 新候选只向量化 c2 常量缓存键的低32位打包，完整1056B键、哈希、全字节比较、当前状态恢复均保留。正式 ARM64 ELF 确认循环指令1152→416，栈及保存/溢出指令无新增；这不是整帧提速比例。候选 APK `0614a534…` 与 GPU 候选分开冻结，真机验证未完成。
- GPU：DPR1 独立 `.pred` 保留旧原始 word SpecID0 与全部循环上界用途，只给第二个 Uniform word 的已证明整数布尔条件增加 SpecID1。原 `.spv/.meta/.spec` 不变；其他数值/浮点用途保持动态。两个当前输入都独立检查范围和非阻塞可读性，完整 raw/mask/module 入 PSO 与 prepared key，16个完整组合上限，额外输入不可读或超限回旧 `.spec`，旧输入不可读回基础模块。默认关闭。
- GPU 小规模测试：主机异步16749857、同步16652014，Mali完整测试16753707检查通过；旧 quiet170个素材逐件不变，仅新增56素材。两台真机新路径均33 hits、16 variants、0 rejects；Adreno 后续旧缓冲区/参考整数附件测试仍有已知失败，不能称整套通过。独立报告 `.local/logs/uniform-hybrid-runtime-independent-review.json`。
- DPR1 工具拒绝未展开的分组装饰及不在证明白名单内的来源，22项新旧工具测试通过。原162个着色器包及原始 ExeFS/main 通过原加工入口重新生成6931项，新增3094个独立谓词模块；链 `4b69b3fa96e7` 已完成全量验证，部署及真实游戏对照另记。离线 MOC 的分支区域缩小不等于原来会执行未取分支，不能换算 FPS。


CPU0614后续验收：Mali完整GPU oracle 16753707检查、真实原游戏fullsync及同PID Surface恢复均通过，0VUID/SYNC；Q原游戏普通20.679/20.654及官方PMU完成，不构成配对净收益。Q旧参考整数附件检查失败，不能称其完整GPU oracle通过。两机原生命周期存档后停机。

DPR1实际Arm结果采用同PSO/variant集合的ON3/OFF4：总外存+6.56%、LS外读+34.40%、L2读取−22.16%、fragment cycles+2.80%，ALU cycles仅−0.78%。LS partial writes变多，不能直接归因spill；CAM丢失、分母差异、系统级counter和热漂移均保留在 `.local/logs/uniform-hybrid-v1-mali-independent-gpu-analysis.json`。因此新hybrid保持实验关闭，旧Spec0继续保留；关闭原因是新增资源成本。普通SF同PID OFF/ON/ON/OFF为27.538/27.673/27.715/26.916，全部PSO稳定但仍有热漂移，不宣称稳定提速。原3b0链24533文件逐件哈希匹配后恢复，候选原目录保留。

外部fork逐项审计见 `DODOLA_FORK_OPTIMIZATION_AUDIT_2026-10-18.zh-CN.md`。另核实Mali此前已有实际ASTC绑定：2388个图像、HDR主组379个；原完整包与声明输入/产物哈希均匹配。“恢复普通运行”不是撤回ASTC，不能把当前textureexternal流量简单解释为BC3大量展开，也不为此重复转码。详细边界见 `.local/logs/astc-current-coverage-readonly-report.json`。


## 2026-10-18：重复顶点／索引绑定与纯颜色清屏组（3079，真机性能待核）

冻结 APK `3079b5b64bb7c2dc45a92a823f65a343fb8a781cb4522e3820f6b409bbabff51`、ELF `783f66561048741fc86c4e91be0009b82c7dc4c04ed5525c84e46df145629dce`，368 项输入已核验。相对0614只改 `nvn_vk_draw.c`、`nvn_vk_plan.c`、新增 opcode 证明 include 和两份真实 GPU 测试（及其入口）。原游戏、原加工链、Shader 和分辨率均未改；Mali 仍为2772×1240、scale1、原世界时钟。

CPU：每个实际 secondary render job 记录16槽 `{VkBuffer, offset}` 与 `{VkBuffer, offset, indexType}`。每次原资源解析、引用、脏页上传与依赖检查照常执行，只省去完整 API 状态相同的绑定。新录制清有效位；未知上下文清缓存并发原命令；原嵌套 ghost 也更新同一真实绑定状态。既有参考开关仍可强制原调用。官方原帧中572/2360次顶点和169/1616次索引绑定相同，这是适用性证据，不是本轮已测调用减少或 FPS 收益。成本为每 job **280B**，最终 ARM64 `vk_draw` 栈 **+16B**，静态栈访存计数1025→1038，不能声称零额外 spill 或零 CPU 成本。

GPU：对当前颜色 clear 后最多256条已验证 immutable plan op 做保守证明，只有纯颜色清屏组到明确边界、没有深度／模板使用，才省去该组不需要的 DS 附件。未知、draw、DS-clear、query/event、调用跳转、嵌套END或截断均回退。保持两颜色 clear 合并，未重排命令或修改 Shader；不丢弃 DS 内容。旧实际帧的8.59MB/帧仅是逻辑DS读写上界，不是已测DRAM。

独立审查见 `.local/logs/vertex-index-bind-cache-v1-independent-review.json` 和 `clear-group-depth-v1-independent-review.json`。主机异步16,777,168、同步16,679,325 checks均通过。Mali quiet 整套16,781,018 checks通过，原游戏 full synchronization validation 和同PID后台保存／Surface恢复通过，零VUID/SYNC错误，fresh save2后停机。高通两个新增 GPU 用例均通过，随后旧 compact-driver reference mode0 整数附件用例仍在line163失败（0614已有同失败），不能称整套通过。

普通运行与官方CPU/GPU Profile继续单独分析；带验证层的10.36FPS不是普通帧率，不与27–29FPS作性能比较。原生稳定30FPS目标仍未达成。


## 2026-10-18：逻辑纹理预处理与独立 mip 清屏候选（63f4）

冻结 APK `63f4ac63…`、ELF `18ca7fed…`，373 项输入已核验；原游戏模块、Shader 链、原生输出和世界时钟保持不变。普通采样的不可变 recipe 可直接证明预处理无需访问物理 owner，真实消费端仍执行原上传、布局、依赖和生命周期处理。Storage、格式重解释、多 mip 反馈及待执行清屏均保守回退；不是依赖上一帧结果预测资源状态。

实验性独立 mip 清屏默认关闭：有界命令证明两个附件的 mip1/2 完整清屏后回到原 mip0，满足无读写别名、查询等条件时，保留原物理 pass，再按原顺序兑现清屏。真实消费者、传输、事件、提交等边界先兑现；不加入游戏或 Shader 标识。新增条件判断与调用有成本，82.49MB/帧只是旧捕获推导的逻辑颜色往返上界，不是实测外存或整帧收益。首轮 ON 测试揭示无变化 image_promote 在 STORE 元数据查询中递归结束 secondary；GDB 定位后，把 drain 移到无变化返回之后，真实升级仍等待旧 pass 和清屏兑现。只接受修复后的 v3 验证结果。

主机异步16,868,619／同步16,770,776、Mali整套16,872,469检查通过，零VUID/SYNC。高通新增两份 GPU 用例通过，随后旧 compact-driver reference line163 仍失败，不称整套通过。Mali原游戏完整同步验证、同PID保存／Surface恢复通过并 fresh save2 后停机。精确 positive=true 且 pending=true 的缓存别名组合尚需补充运行边界证明，不能由最终像素测试代替。证据见 `.local/logs/deferred-clear-logical-v1-functional-summary.json`。

高通普通20.797／20.690 FPS。官方 PMU 中预处理热点份额下降，但 replay running34.623→34.557ms、wall37.655→37.661ms，未证明整体 CPU 提速；相对份额不是绝对节省。GPU清屏当时关闭，不将 CPU 采集冒称其收益。完整边界见 `.local/logs/deferred-clear-logical-v1-735-cpu-summary.json`。

Android新增可选同像素显示模式／Surface刷新率请求，默认0，不实现帧回调、丢帧或改变上游60帧节拍目标。原始显示服务曾记录用户最低120Hz与应用路径最高60Hz并存，后续新包也实测到物理120Hz；来源未独立控制。显式120请求确实到达SurfaceFlinger，但请求值不等于实际模式。首轮默认／120／默认测试最后一窗出现60与120Hz两个统计桶，禁止取第一桶FPS当整窗结果，禁止据此宣称解决原生稳定30FPS目标；该目标仍未达到。见 `.local/logs/display-api120-mali-control-v1-independent-analysis.json`。


精确别名补充验证：仅新增fixture case16，不改63f4生产代码。主机GDB实际停在cached positive=1、id4、epoch15003一致、pending issued4；自然返回prepare_binding后issued0，尚未进入调用方descriptor消费，排除了后续消费者兜底导致的假阳性。随后完整GPU oracle16,870,996检查通过。该额外case未部署Android，勿与373源冻结包内32case混同。真实原游戏打开清屏实验后fullsync/resume/save2亦通过，但admitted/materialized/continued仍全0，当前没有原游戏工作削减证据；已有官方API帧确认detour使用不同小尺寸DS，而原候选额外要求相同DS，需按纯颜色清屏语义修正并补DS独立保存测试。实验继续关闭。


## 2026-10-18：独立 mip 清屏 v3 的命中、工具结果与出图节拍边界

冻结 APK `5d8e6378…`、ELF `af432d19…`（373 项源码输入）。补齐有界规划中纯状态 `OP_STENCIL` 的识别和与原完整清屏相同的 scissor 覆盖判断；不同尺寸 detour DS 仅作为纯颜色清屏的独立附件处理，恢复时仍要求原 DS 身份。真实消费者、查询、资源代数／生命周期、别名与提交边界保护保留。50 个 OFF/ON 用例覆盖单 mip 缓存别名、独立 DS 保存、真实 DS 清屏回退及负坐标／空／溢出 scissor。主机 async/sync 分别通过 16,892,591 / 16,794,748 检查，Mali 16,896,441，零 VUID/SYNC。高通新增用例通过，整套仍在既有 compact-driver reference 检查失败，不称整套通过。

Mali 原游戏 2772×1240、scale1、原链 `3b0e470fc91d`、原世界时钟，完整同步验证及同 PID 后台保存／恢复通过；最终 fresh save2 后停止。ON 已有真实命中：验证窗 +88 admitted / +352 materialized / +262 continued，rejected=0。相同 APK 的普通 OFF/ON/OFF SurfaceFlinger 为 28.949 / 29.451 / 29.456 FPS，后两窗 PSO/variant 均179/32；初窗缓存尚在增长。因此既不能称稳定超过30，也没有可靠的净涨帧证据。ON 的额外证明扫描须计入 CPU 成本，实验仍默认关闭。完整记录 `deferred-clear-v3-functional-summary.json` 和 `deferred-clear-v3-mali-ordinary-control-independent-analysis.json`。

官方 Streamline/PA 当前 ON 捕获在2–10秒有235个完整 LWI frame，外存读／写归一化为295.061 /178.603 MB，合计473.664 MB/帧；GPU29.957M cycles、fragment19.844M、load/store5.624M、texture4.045M。旧3079捕获为476.210 MB/帧，但跨 APK、PSO和工作量并不完全匹配，不能把约0.53%的差异归因为本候选。LWI帧边界有两帧partial，分母和计数器覆盖边界已记录；这些是工具实际外存计数，不能与逻辑82.49MB上界混用，也不能把重叠阶段周期相加当整帧时间。当前捕获PSO/variant174/30→182/32，PA29.8 FPS不是普通运行成绩。见 `deferred-clear-v3-mali-arm-on-official-summary.json`。

同一 APC 的官方 leaf PC 采样已用匹配 ELF 解析：`nvn-translate#8838` 838个样本中44个（5.25%）在 `vk_texture_mip_clear_admit`，全部落于全图链表遍历，不能换算为精确周期／毫秒。初始前瞻对相同两资源重复8次全图证明，加实际4次复核共12次；下一项只合并连续前瞻8→2，保留实际4次复核，不引入跨 draw 缓存。官方分析器首轮退出异常及后续 report 成功恢复、原始 APC 未改写均有记录。见 `deferred-clear-v3-mali-arm-on-cpu-leaf-summary.json`。

出图策略另行审计：dodola 固定提交 `dd0e3c764fe2f797794eb7431ea0af9d81754c5f` 的实验 `present-60fps` 将 Acquire 前节拍固定为1e9/60，不乘 guest interval；Vulkan present 自身忽略 period，不插帧／不改游戏时钟。主线当前实际 period=16,666,665ns，已有同类取消 interval2 的行为。已有 Perfetto 完整相邻 Present→replay 仅约0.184/0.189ms，未见明显消费者饿等新帧空洞；实际 NVN fence 调用线程无33ms整段睡眠，但仍有明确 Fence/Present 等待，其他匿名线程的周期睡眠没有角色／调用栈证据，不能据此排除所有游戏内部节奏限制。见 `deferred-clear-v3-guest-pacing-independent-audit.json`。物理120Hz、目标60、实际渲染FPS应分别记录。

视觉验收补充：用户在 v3 ON 的官方单帧抓取运行期间观察到闪动／撕裂。该观察作为未解决视觉回归记录，不能由单帧结构相同或 fixture 通过排除，也不能未经对照归因于抓帧层。候选继续默认关闭；正在以同 APK、同原生分辨率、无 layer 的 OFF/ON/OFF 短视频对照核查。录屏只能观察 compositor 输出，不能证明物理面板没有撕裂。工具捕获结束后已 save2 停止并恢复 layer/属性。

普通画面对照补充：同 APK、原生2772×1240、无 layer，PID11788关闭清屏实验、PID12451开启实验，两次用户均未再看到闪动／撕裂；ON真实 admitted942/materialized3768/continued2569，非未命中假阴性。两次均 fresh save2 后停机。只能说明短窗未复现，不能认定 layer 为根因或排除跨帧问题。Android原生screenrecord无法写目标路径，stdout模式又在HAL初始化卡住且SELinux拒绝停止，因此没有视频，不伪称完整视频验收；其睡眠中的自有PID12412清理限制单独记录。见 `deferred-clear-v3-visual-observations.json`。


## 2026-10-18：按输出／3D比例拆分瓶颈（诊断，不计原生满比例目标）

两机相同冻结5d8e包、原Shader链、固定比例、原时钟、无layer、清屏延期OFF，均采用reference blit。Mali原生2772×1240为28.672/28.329FPS@120Hz，1280×720为46.706/47.845@120Hz；高通原生2750×1260为20.679/20.690、720为38.452/38.514，回原生20.725/20.725，均90Hz。720同时改变宽高比与可见绘制，不能把全部收益归因于像素。Mali720的PSO/variant还在168/30→175/32增长。Mali回原生发生120→60Hz与CPU加权频率1963→1619MHz变化，末窗22.910FPS；不能将它隐藏或当作稳定A/B/A。

原生UI、相同输出与宽高比、只固定原3D比例0.5：Mali35.495/36.063@60Hz，高通35.465/35.741@90Hz。Mali原日志实际3D目标1386×620、输出2772×1240。Mali回原生/half replay Running25.802/24.736ms，CPU加权频率1619/1686MHz；Present wall11.828→0.735ms。频率加权Running代理约同为41.7M，不是硬件PMU周期。支持3D负载和GPU／出图背压是重要部分，同时固定CPU侧工作仍明显，不能归因为纯DRAM带宽；这些普通trace没有测实际DRAM。两机均超过30，排除当前绝对30FPS上限，但不排除其它节奏／WSI开销。Mali报告 `resolution-equivalent-v1-mali-analysis.json`，所有相均保存2次后停止。

Apple口径核对：上游`front_ios.m`默认UI原生，`hle_oe.c` iOS默认3D0.75，并可因掉帧动态降0.72/0.70（流畅优先）；我们原诊断固定1.0且关闭动态比例，同输出时3D像素约为默认0.75的1.78倍。上游公开README当前A14仍无60FPS报告，A16有60FPS记录，A15mini的50–58来自自动降画质；不能把A14为最低支持硬件误当为A14原生满比例60实测。对齐Apple的原生UI固定0.75测试另记，不能冒充原生3D1.0目标达成。

## 2026-10-19：外部评估核对与下一轮优先级

用户提供的[共享评估](https://chatgpt.com/share/6ad56e9a-b0e0-83e9-8b06-8315cf2cbc60)作为方向参考，按本地原始工具数据、源码及已安装版本逐项复核。其约500 MB/帧、54 graphics pass、fragment繁忙等主要引用10月17日固定3D比例1.0的历史采集，不能直接视为当前上游iOS默认0.75、动态比例和新音频路径的性能结论。不同阶段计数存在重叠，不把各阶段cycles相加当整帧耗时，也不按TBDR单draw边界时间给Shader排名。

| 优先顺序 | 工作与依据 | 当前边界 |
| --- | --- | --- |
| 首先校正对照 | 高通此前仍是旧APK、ABI2和旧Shader链；先与当前Mali宿主版本对齐，再用官方SDP/Perfetto归因 | 已对齐同APK `892dcf51…` / ELF `32506ece…` 和ABI3；Mali与高通须分别保留兼容的Shader编译选项，不能直接共用SPIR-V |
| 同像素中间附件消费 | 原Metal已有通用融合思路；既有API帧中P34→P35对三张中间颜色附件做同像素采样 | 先做可证明的input attachment/subpass候选；不是合并所有后处理，不改变玩法，也不按游戏Shader标识硬编码 |
| 主HDR Shader成本 | 已有工具显示片元负载和大量纹理读取；匹配Shader的驱动静态输出仍有较多采样及ALU | 继续以实际采样、驱动结果和整组硬件计数定位；静态指令数量、draw数和单draw回放耗时不能替代动态热点证据 |
| 实际按需显存镜像 | 整个逻辑Pool的VkBuffer extent明显大于初始化区域，可能有容量浪费 | extent不是RSS；必须接齐生产消费者、生命周期与整池回退后，再测PSS/VMA块容量和真实6GB启动 |

系统驱动/Turnip、Present Shader/Blit、FIFO/MAILBOX及输出尺寸诊断已有对照记录，不作为此前未做的新发现。现有结果没有显示它们单独足以消除全部差距；驱动对照记录见本文件既有Turnip与出图章节。当前低帧仍须同时记录热保护的目标帧率、实际3D尺寸和显示档位，不能把20/30的热保护上限误认成新的GPU吞吐结论。

### 同像素候选：先保存语义，再验证tile收益

旧API帧中P34输出RGBA16F、RGBA8、R11G11B10三附件，P35采样这三张图及另一张history纹理，输出独立R11G11B10；history保留普通采样。旧固定1.0、2772×1240时，三源合计逻辑读取54,996,480 B。这不是硬件DRAM节省量，也不能与其他候选的逻辑上界直接相加。后续帧的读取尚未排除，源附件继续STORE。

三个源及目标的颜色合计160 bit/pixel，P35另有只读深度附件，而P34没有深度。格式容量、深度拓扑和具体驱动会影响能否合并。[Arm官方说明](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/vulkan-subpasses-the-good-the-bad-and-the-ugly)也指出tile读取吞吐和合并的其他成本；不能把旧GPU示例中的128bit当作G710通用硬上限，也不能保证减少外存就一定涨帧。该Mali驱动未提供可用的subpass merge feedback扩展，后续用现有Streamline的physical tiles/jobs与外存计数做候选对照，不自制逐draw计时器。

本轮已实现两项基础能力，尚未启用原游戏融合：

- 离线通用像素坐标证明与实验性SubpassData变体。每个实际base/spec/pred/tssbo模块分别绑定内容哈希；分支中任何未知用途、邻像素读取或非零LOD均保守拒绝。实际draw还须证明几何、插值、viewport、sampler、LOD、mip/layer与图像身份。工具选项默认关闭，原模块不改写；当前部署器仍拒绝未接入验证的`.plocal.json`，不能直接推到手机。
- 不可变RenderPass缓存支持最多两个subpass，并把input次序、第二段颜色/深度、preserve和dependency完整纳入扩展key。普通单subpass key与哈希保持原样；每个缓存对象增加一个8B指针，扩展路径另分配416B拓扑，acquire局部栈也增加拓扑对象。主机真实GPU用例验证三个混合格式input attachment的读取、输入置换、缓存复用与退休；首段使用loadOp clear，因此不能冒充已验证游戏MRT绘制、后加深度或实际tile合并。

当前游戏仍以单subpass记录。完整接入还需在首段secondary录制前规划兼容拓扑，补齐PSO/subpass及持久化配方身份、资源INPUT_ATTACHMENT用途、描述符变体、运行时坐标证明与保守回退；不能把已经按单subpass继承信息录好的secondary直接塞进新双subpass。

### 内存窗口预留：已通过隔离验证，尚不宣称节省内存

新增显式批量预留API：逻辑范围完整校验，固定对齐窗口无重叠，跨窗口单个绑定明确要求整池回退；先完成全部分配，再一次发布，OOM回滚不改变旧代数或旧异步读者。预留本身不上传CPU数据、不初始化shadow/脏页元数据、不迁移已有窗口。计划最大4096项时临时主机内存128 KiB，每个窗口还增加VkBuffer/VMA对象；最终必须把这些成本及整池迁移峰值计入。

现有生产lookup仍整池分配。不能在已借出VkBuffer后隐式迁移；下一阶段要完整预收集draw/compute/传输/texel/延迟绘制范围，在借用句柄前处理回退并重新收集。没有真实生产接入、内存A/B和6GB设备启动证据之前，不记作RSS下降。

统一主机异步21,790,147项、独立同步21,690,743项通过，保留Khronos同步验证，未发现VUID/SYNC错误；实际执行设备为Lavapipe，不使用计算显卡。这些是实现正确性检查，不是Android游戏性能里程碑。Android ARM64 APK另已编译、签名及934项源码输入哈希核验，候选 `9ef435cd…` / ELF `60a8f3a1…`仅归档未安装。证据分别为`pixel-local-proof-v1-source-ready.json`、`pool-window-reserve-v1-host-result.json`、`pixel-local-foundation-v1-android-build-verified.json`。

高通已保留subgroup64、ballot helper和必须的函数内联选项，以新规范链`9328e25ef116`重新验证并部署。6,931份SPIR-V及3,739份spec与其旧兼容链逐项一致；不是复制Mali的subgroup32链。原游戏营地短程移动、真实NVN调用和fresh save2通过，结束后已停止；尚不从该功能测试推导帧率或发热收益。完整清单见`qualcomm-canonical-53fbef1-alignment-result.json`。

随后对该已对齐高通进行官方SDP 20秒GPU计数与8秒Perfetto采集，结束fresh save2并停止游戏/本次工具。目标60、公有热API状态0；20秒末段动态比例从0.75变0.70，不能混成固定画质窗口。8.020675秒Perfetto子窗始终0.75：218次replay平均wall28.317ms，调度Running25.875ms，Running频率加权1.863GHz；Running不等于全部是有效翻译运算，仍须区分驱动忙等。Present平均wall6.663ms，其中Running0.484ms；这些范围和其他线程的guest fence等待存在并行，不能随意相加。全进程约2.256核Running，说明CPU侧持续占用仍需单独治理，不能依据旧固定1.0 GPU数据搁置CPU主线。

SDP按官方REALTIME/BOOTTIME投影到相同8秒，每项158样本：Shaders Busy77.818%、Shader ALU Capacity Utilized30.002%、Texture Pipes Busy45.940%、Stalled On System Memory0.280%、Bus Busy18.900%；读4.041GB/s（其中纹理3.436GB/s）、写1.917GB/s。GPU约500MHz；工具GPU Utilization约50%的指标明确因最大频率查询失败而使用默认值归一化，不能据此说GPU有一半空闲。指标分母不同、可重叠，且是包含合成的系统GPU计数；跨工具UTC偏差未独立校准，不做逐事件因果归因。前12秒稳定0.75独立统计与8秒值接近。当前高通数据不支持把外部DRAM带宽饱和作为主要结论；片元/纹理及局部流水线仍有成本，同像素融合优先验证带宽与能效，不预言它解决全部FPS。报告`q735-aligned-sdp-53fbef1-summary.json`与`q735-aligned-sdp-53fbef1-gpu-counters.json`；这些不是Mali的新计数，也不是候选A/B收益。

减少实际计算、复制和外存访问，在相同帧率/画质下有望降低能耗；帧率不受限时，节省可能被新增帧数消耗。持续温度及热降频还需同等工作量的真机长窗验证，当前发热与6GB验收仍未完成。

当前CPU调用栈补充：另一个同APK/链、始终0.75/目标60的7.978秒窗口，用官方simpleperf `cpu-clock:u`/DWARF采样940次、零丢失，严格匹配已安装ELF的BuildID。replay线程470样本的Self DSO份额为宿主63.404%、vendor驱动22.979%、libc12.979%；驱动混淆符号不能进一步断言为忙等。官方inclusive/self分别有`vk_draw`61.91/5.96%、`vk_shader_bindings_selected`24.89/3.40%、commit16.60/2.55%、`vk_texture_view_binding`10.21/1.91%、`sync_page`8.09/4.26%，memmove Self5.11%。这些inclusive有重叠，不能相加；部分调用链止于中途，也不能拿另一次Perfetto的毫秒直接乘成精确函数耗时。该证据支持下一步优先减少真实绘制资源处理、脏页/复制与描述符工作，同时继续GPU带宽/能效候选，而不是只按旧满比例GPU结论排序。本次公有热API日志为空，不借用上一SDP窗口的thermal0；结束fresh save2/281.4ms，游戏及本次工具已核实退出。见`q735-aligned-cpu-53fbef1/summary.json`和官方报告。

### CPU：复用纹理视图的不可变访问范围

官方NDK `annotate.py` 使用匹配ELF与冻结源码解析上述样本后，重复范围校验有明确代码证据：同一物理view在layout路径和image request路径分别调用`image_access_range`。已有recipe缓存保存view描述，却未保存深度/模板耦合、3D层映射与边界校验后的访问范围。本轮将该结果加入原recipe，命中沿用既有descriptor epoch、owner lifetime和physical handle保护；MISS仍按原规则校验一次。Vulkan view范围与依赖范围分别保存，避免把depth-only view扩成DS view，或把3D slice视图改成dependency layer0。布局、反馈、CPU上传、hazard和实际reader记录仍逐次执行；raw入口保留原检查，不跨draw预测当前资源状态。

额外成本为每个原有recipe槽一个`VkImageSubresourceRange`，无新查找表或计时器。官方功能窗约94%物理recipe命中不代表全为同一种拓扑，也不能把整个函数的采样份额视为可消除。独立冻结的两文件补丁通过异步21,790,147、同步21,690,743项原GPU检查及同步校验，包括真实DS数组采样、stencil保存、mip与格式升级、嵌套ghost和cache碰撞，零VUID/SYNC。证据`texture-range-recipe-v1-validation.json`与`q735-texture-redundancy-v1-readonly-review.json`；尚无这项修改的真机CPU/FPS/温度收益，不与并行中的内存窗口生产接入混算。

### 生产内存窗口：高通实测未见 PSS 收益，先修额外回读

后续 `69ca473`、`7910c80` 已接齐 draw/compute、传输、UBO/SSBO、typed texel、顶点/索引与延迟 ghost 的完整范围准入。借用物理句柄前完成预留；跨窗口单绑定在明确的提交完成边界迁移、重收集，旧 GPU 字节保持，未知枚举走有计数的保守整池回退。启动窗口默认仍为 0（原整池策略），不能热切换。最终冻结源的异步 21,809,890、同步 21,710,486 项检查通过，零 VUID/SYNC；这是 Lavapipe 实现验证。

同一真实高通 Adreno 735、同 APK `79607c61…` / ELF `dc2d0d43…`、原游戏 ABI3 和兼容 Shader 链 `9328e25ef116`，固定原生 UI 2750×1260、3D 0.75、目标 60，执行原输入四段营地移动后，用系统 `dumpsys meminfo` 取单次内存报告；报告在官方 Perfetto 窗口外执行。三次均正常继续原游戏、完成原生命周期两次存档并停止进程。

| 顺序 | 启动策略 | GPU 镜像 backing extent | 系统 PSS | 系统 RSS |
| --- | --- | --- | --- | --- |
| A1 | 整池 | 1,167,065,088 B | 2,612,624 KiB | 2,741,928 KiB |
| B | 16 MiB 窗口 | 707,788,800 B | 2,625,196 KiB | 2,754,616 KiB |
| A2 | 整池 | 1,167,065,088 B | 2,629,753 KiB | 2,759,252 KiB |

**分配范围缩小不等于实际常驻内存下降。** 本次 B 的 PSS 落在两次 A 的波动范围内，没有测到实际 RAM 净收益，也不能据此宣称 6GB 启动成功。窗口累计一次整池升级、22 个新 span，未发生未知枚举回退；后续走动仍可能扩大 backing。

还发现迁移后的保守正确性措施有额外带宽成本：现实现将已迁移池永久标为不能信任 CPU known-copy。Perfetto 采集前后原机制计数中，B 的 known-copy fallback 为 237,031,508 B，A2 为 21,120 B；对应实际 GPU 回读记录为 554,147,188 B 与 370,457,472 B。两个窗口工作量不完全相同，不能直接相减当等负载节省量，更不能称为 DRAM 总线测量；但它明确暴露了应修的保守路径。修复必须按范围、GPU 写入代数和实际发布证明恢复 CPU 数据有效性，不能仅凭 fence 完成就相信旧 shadow。

官方 Perfetto 的 B / A2 replay 平均 Running 为 25.677 / 24.795 ms，所选 CPU 频率也不同；这里只作描述，不宣称精确 CPU 或 FPS 因果。A1 的 Uniform variant 从 31 增长到 33，不算完整预热对照；B/A2 都稳定为 226 个 pipeline、33 个 variant。首个窗口尝试在距离目标约 3.16 个单位、允许半径 3 时移动超时，游戏仍存活且正常存档；后续 B/A2 使用半径 4，不把脚本超时报成游戏崩溃。

原报告 `pool-window-production-v1-q-comparison.json`、`pool-window-production-v1-q-frequency-comparison.json` 保留全部输入和失败尝试。待修复迁移后的额外回读，再核对 Mali 的真实内存；不能将高通的 extent/PSS 关系直接推广到不同驱动。

额外回读的后续修复已按范围接入：启动窗口池从首次 GPU buffer writer 起保存 CPU shadow 未获证明的字节区间；旧完成回写只清除其实际快照代数之前的重叠段，后写的字节和未同步邻居仍未知。CPU 上传只证明本次提交开始前的写入，迁移本身不制造新的未知数据。原 GPU-only 迁移字节、精确部分发布、同命令缓冲的晚写、旧在途发布、CPU 上传及满表拒绝拆分均有实际 GPU 负例；独立异步 21,810,491、同步 21,711,079 项通过，合入主线后的异步同样通过且零 VUID/SYNC。每个实际写入的窗口池懒分配 6,152 B、有界区间表和互斥；pool 增加 8 B、写回节点增加 8 B，并增加每个受追踪池每次 flush 的 56 B 完成回调。普通整池不分配区间表。该修复尚无真机新对照，不把它算成已降低的实际 DRAM、PSS 或 CPU 耗时；它也没有扩大 `try_read/cover_read` 的 CPU 可读证明。

### 带宽与发热的优先级

外部 DRAM 未饱和只约束吞吐瓶颈判断，不排除访存的能耗成本。继续优先减少中间附件外存往返、不必要的上传/回读与复制，并分别记录逻辑工作量、原厂硬件外存流量、整帧表现和持续热状态。当前同像素 input attachment/subpass 的实际 renderer 与模块变体仍在隔离实现，尚未装机，不能提前计入上述内存候选的收益。源附件仍有后续使用时保留 STORE，实际 tile 合并与净流量变化须由真机工具验证。

本轮内存短窗只有公有热 API 已报告 status 0，没有同步硬件能耗或持续发热对照；不据此声称过热已解决，也不通过 ADB 轮询温度。

同像素路径的后续基础提交 `3145d43`、`7077819` 已加入可部署的 DPL1 `.plocal`、实际模块内容闭包和独立描述符布局。每个实际 base/spec/pred/tssbo 模块重新证明，选一个完整的同 UV 读取组，不枚举绑定子集；当前生产 P35 的同组四个输入包含外部 history，不能把它算作三个本地源那样的免写回输入。部署器独立重生成证明/变体再逐字节校验。编译选项 `DMSH_VK_PIXEL_LOCAL_RUNTIME` 与运行时 `DM_VULKAN_PIXEL_LOCAL` 均默认关闭；关闭时保持原指纹且不探测 `.plocal` 文件。

运行时 query 使用构造期固定的 canonical owner 和精确实际 VkShaderModule，最多检查五个记录，不在每次候选 draw 遍历全局 Shader 列表。独立 loader 的损坏闭包、错误 owner、五类模块和当前高通四个真实生产模块已通过官方 Vulkan validation；合入主线后 41 项离线测试及包含生产内存窗口的原 NVN 异步 21,809,890 项检查通过。该提交只提供加载/描述符基础，尚不执行原游戏双 subpass；不能把主机 loader 成功当成实际附件融合或带宽收益。完整链仍须在最终集成源冻结后重建、核验并部署。

另新增通用启动期operation-mode诊断，默认docked不变，显式handheld重用原GetOperationMode/GetPerformanceMode/参数槽，不新写游戏规则；actual dm_opmode启动日志核验，禁止热切换。新13f0包两模式在同包720下对照；Handheld固定720高，game-res只影响手持宽高比。该模式可能影响原游戏分支，但尚不能仅由HLE断言阴影／后处理减少。源与独审见 `operation-mode-v1-source-ready.json`、`operation-mode-v1-root-review.json`。


### 同像素 renderer 接入：完成主机验证，待真机带宽验收

通用候选已接入原 NVN replay：在首段 secondary 录制前进行有界命令前瞻，使用两个 subpass 与 input attachment 变体消费可证明的同像素来源。运行期同时证明实际模块、几何/UV、viewport、sampler/LOD、mip/layer、资源身份和别名；不识别场景或固定游戏 Shader 标识。未知命令、查询、跨提交边界或无法证明的数据依赖保持普通绘制。外部 history 保留 LOAD，后续仍可能使用的来源保留 STORE；不能把逻辑采样消除算成已消除全部附件写回。

单 mip 图像复用原整图 layout/access 状态，多 mip 沿用已受支持的细分追踪，复杂场景保守回退。失败回退在重画前释放 pool operation token，geometry 检查可能触发提交时先核对 job/recording/pass 身份，避免访问已回收计划。运行期开关只初始化一次，原子读取，默认关闭。每个准入计划约 52 KiB，另有仅启用时约 49 KiB scratch 和额外 descriptor/PSO/证明成本，必须计入实际收益。

最终统一源在 Lavapipe 上通过完整异步 21,810,491 项、串行 cover-reference 17 用例 85,080 项，以及启用 16 MiB 生产窗口且故意令 subpass PSO 创建失败的 85,089 项检查，零 VUID/SYNC。失败注入验证普通路径实际 GPU 像素、STORE 与操作引用释放。首轮失败注入 harness 在惰性初始化前检查配置而失败，已调整测试初始化，保留原失败日志。这些结果只证明所测语义和回退，不证明 Android 已融合、硬件 tile 合并、DRAM 降低或涨帧。完整源指纹与日志见 `pixel-local-render-v2-main-source-ready.json`。

内存镜像的范围 authority 已在高通同包整池/窗口/整池验证：窗口模式采集区间的 known-copy fallback 约 18.6 KiB，与整池约 18.6 KiB 接近，原先约 237 MB 的额外回读退化已消除。对应 GPU 回读是逻辑记录，不是硬件 DRAM 流量。窗口 PSS 约 2,587,485 KiB，后一次整池约 2,606,748 KiB，但两次整池本身波动约 60 MiB，仍不足以宣称显著 RAM 降幅；Mali 同包对照另行记录。


### 迁移只保留实际暴露页面，避免触碰未使用内存

Mali 同包整池/16 MiB 窗口/整池的 PSS 为 4,015,898 / 4,310,244 / 4,076,973 KiB；Graphics 为 2,213,668 / 2,499,576 / 2,262,436 KiB。窗口发生两次整池升级，最终 backing extent 与整池相同；不能声称降低 RAM。最后一次整池已触发目标 30 FPS、CPU 频率降至约 1.56 GHz，不能拿它和前两次约 2.09 GHz 的 trace 做纯优化 FPS 对比。三次均原生命周期存档并停机。实际免回读复制已恢复；逻辑 fallback 与提交数近似成比例，没有原来的额外整池回读退化。

源码核实没有漏掉旧 VkBuffer 的销毁与 VMA free。旧实现迁移却复制整个已分配窗口，包括从未实际用于 GPU buffer 的页：这会给新旧存储增加无必要读写，并可能扩大实际触碰页面。新实现复用单调 initialized-page bitmap，仅复制旧 backing 内已暴露页面的连续区间，裁到物理边界和池尾，64 个 region 一批。所有实际 pool-address 路径和 writeback 在暴露 GPU buffer 前均初始化相交页，因此覆盖 GPU-only 输出；不能用 CPU dirty 或 CPU-known authority 代替这个证明。原 bitmap、上传代数、authority、同步和提交完成边界都不改变；未来首次使用冷页仍从当前 guest 初始化。

额外成本是迁移冷路径扫描原位图和最多 1,536 B 局部复制区域数组；碎片页面会产生更多 copy region，无新生产分配、持久状态、每 draw 检查或 GPU→CPU 回读。真实 GPU 用例包含 130 个 guest/shadow 已过期的 GPU-only 输出、空预留、holes、位图字/物理边界、193 B 尾页和后续 CPU 修改。特定稀疏用例从 3,145,921 B 复制降到 549,057 B（82.55%），完整暴露时仍复制全部 65,536 B。独立完整 async 21,811,194 / sync 21,711,782 检查通过，零 VUID/SYNC，且两文件冻结 SHA 与主线合入逐项一致，独立审查通过。该百分比不是实际游戏、DRAM 或 RAM 收益；真机复测待记录。报告 `pool-migration-initialized-v1-source-ready.json`。

补充内存口径：已有官方 API 帧精确清单的图片约 619.5 MiB，Pool 约 1,113 MiB；ASTC 库存 2,388 张，而该帧约 420 张被绑定，不能把库存全部称作当帧采样。跨厂商 Graphics accounting 不能直接解释为纹理解压或泄漏。单帧未引用图片约 254.6 MiB 也不构成可安全销毁证明，GPU-authored 历史内容必须保留；详细只读审计 `mali-memory-residency-candidates-v2-readonly.json`。


### 同像素产物验证：空记录只核闭包，授权记录仍完整重证明

完整包各有 6,609 份严格空 DPL 和 318 份非空 DPL。空记录不会授权任何 Shader 替换，运行期保持普通路径；部署器对其严格检查 192 B extent、头部、stage/set、五成员 SHA、实际 base/spec/pred/tssbo 模块集合与 SHA，以及反射 sidecar 闭包，不再重复 eligibility 分析来证明“未采用可选优化”。stage5 同时是原分析拒绝哨兵，按原包实际语义验证，不能误认成可用 compute 证明。非空记录仍按原 emit/build 重生成全部证明及变体并逐字节比较；原 dkey/spec/paired/metadata 与 baseline SPIR-V 检查不变，未修改编译工具或链指纹。

10 项新增离线测试检查闭包/头部/模块篡改、伪装空记录拒绝和非空重证明；两机现成包各 6,609 份空闭包实际通过，独立原 41 项证明测试保持通过。主线新增测试复验通过。原慢路径高通全量 6,931 已通过并确认基础 SPIR-V 与原已部署链完全相同；Mali 原慢路径仍在执行。新部署报告分别统计 empty_fallback/regenerated，不能把闭包验证记成重做了全部 eligibility 证明。见 `deploy-empty-dpl-v1-source-ready.json`。


### 真实游戏附件合并：先定位拒绝，再测带宽

Android 已部署完整可选证明链：高通 `9507875007c6`、Mali `38da72e80a08`，基础 SPIR-V 与各自上一完整链相同，所有 45,312 个部署文件回读哈希通过。旧包实际游戏两次三 MRT、mip0、indexed3 的规划在 SET_RT 后第六条命令因 `0x13` 被拒绝，尚未到 consumer 证明；这是官方 LLDB 的功能诊断，不是 GPU 计时。

`c16445e` 仅在已改变的 SET_RT 与首个 consumer 之间接受 primitive/fragment 排序及 texture invalidate 的已知组合。所有生产者和 consumer 必须没有 programmable writer；查询、转移、未知标志、普通 VS/FS 别名和实际资源依赖保留拒绝/回退。原 barrier 仍逐条执行，flags 不被清除；需要更广同步时结束 dual pass，仅重试尚未执行的 consumer。独立审查确认旧 GPU write→consumer 独有 VS UBO 的静态回退闭环，但该组合没有冒充新增专用 GPU 用例。25 用例包括实际 SSBO atomic 生产者/消费者、PSO 故障、窗口及 reference 路径；全套 async 21,811,196 / sync 21,711,784 检查通过，零 VUID/SYNC。

同 APK `b9de5ea6…` / ELF `1fad3575…`、原始 NSO 一次重建 ABI3 模块，在高通验证层与 Mali 普通运行均完成营地移动、原生命周期两次存档并停机。但两台 planned/draws/aborts 均为 0；本轮不能宣称融合、带宽或 FPS 收益。高通仍有此前已见的 R16_UNORM Dref VUID 06479，不能称整轮零验证错误。继续使用 LLDB 找下一处拒绝，候选未命中时不重复做性能 A/B。见 `pixel-local-boundary-order-v1-{q,mali}-assessment.json` 和独立审查报告。

### CPU 上传：一次源读取写入两个目的

直接 changed-run 上传原来分别把 guest 数据复制到 mapped buffer 和 shadow。新可选路径在没有 pending publication / GPU access 时，一次源读取供两份目的写入；原四字节差异范围、脏页代数、物理 span、publication lock 与 submission-begin authority cutoff 不变。存在在途 GPU 使用时仍走原 staging，serial/global/reference 路径不变。它减少复制阶段的逻辑源读取及独立复制调用，不代表同等数量 DRAM 流量被消除；比较扫描和两份目的写入仍保留。

NDK O3 中 64B 循环为两次 LDPq 和四次 STPq，sync_page 文本增加 640B，并增加一个缓存启动布尔与选择分支。简单循环与 bionic 流水复制的实际吞吐仍须用官方 CPU Profile 验证。默认实验关闭，Android 冷启动参数 `--upload-dual-store` / `--no-upload-dual-store` 选定；已启动进程拒绝修改，未增加热路径 getenv。

Mali ARM64 CLI 的真实 Vulkan memory-pool oracle 400,453 项通过，含非对齐/guard tails、两个物理 backing、GPU-only 邻字和录制后 CPU 修改。官方 LLDB 另确认调用来自 commit→sync_page→pool_spans_copy_dual→upload_copy_dual，并命中 128B 向量循环入口；调试轮在首 LDP 前主动结束，功能完成证据来自前述完整 CLI。Android standalone 无法发现验证层，不能称其 VVL 通过；Lavapipe 独立完整 async 21,813,081 / sync 21,713,669 项零 VUID/SYNC，合入主线 async 21,813,081 及 memory 400,453 项也通过。没有实际游戏 CPU/FPS/温度收益结论。见 `direct-upload-dual-store-v1-mali-production-proof.json`。


### 未执行 draw 的重试不发布可见性

附件合并的四处消费者重试原先使用正常 record/writeback 结束 descriptor batch；如果后面的 Y 上传结束 pass，之前已收集但尚未消费的 X 会被错误记录为已满足 shader-read visibility。新代码取消 prospective image/shader operation，不记录未执行的读写、不复制回尚未写入的 storage；实际执行的 upload/copy/producer/clear 历史保留。PSO 故障等晚回退也保守取消，允许重试重新同步。

实际 GPU 负例在 compute imageStore X 后，通过 Y 首次上传触发 dual abort；旧路径在最终消费者前缺少 COMPUTE→FS write/read 依赖，契约断言失败，而普通 OFF 路径和修复路径有正确依赖、正确像素及后续 STORE/history。Khronos SYNC 验证在旧路径上未报告该缺失，不能把自有 API 契约检查说成原厂错误报告。修复独立 async/serial/window/PSO-fault 用例及完整 async 21,811,196 / sync 21,711,784 检查通过，最终零 VUID/SYNC。没有真机融合或 FPS 收益结论；报告 `pixel-local-batch-abandon-v1-source-ready.json`。


### 生产者读取 sibling mip 的范围证明

高通官方 LLDB 的下一处拒绝已确定：真实生产者 FS `8e80084d3c30340a` 的 sampled binding2/reg12/descriptor786 读取同 serial2146 的显式 mip1 单级 view，当前 color0 则是 mip0 单级 view。旧 producer rewalk 丢弃 descriptor view，按整个图像判别名，导致合法 sibling read 被拒绝；这不证明 GPU 上有实际反馈环。

新精化只作用于 producer sampled kind3/6 的检查，要求完整 physical texture 身份/描述/布局相同、受限 2D 单层和双方显式有效不相交 mip 范围。storage/texel、默认/未知/零计数 view、同 mip、不同 serial 的 guest 内存别名仍保守。消费者普通资源别名、实际 token、epoch、geometry、layout 和 hazard 保护不改变。新 9 个实际 GPU 用例覆盖本地来源和 external history 的 sibling mip、重叠与未知负例、后续 STORE/history 和故障回退；原 26 例保留。新正例为 1 pass；用原 a7953b3 planner 加同一新 fixture 的负控则严格失败于 2 pass。独立完整 async 21,813,081 / sync 21,713,669 及窗口、PSO 故障、cover reference 全部通过、零 VUID/SYNC。真实 Android 游戏命中/硬件 tile 合并及收益仍待测，不能把夹具融合当作游戏收益。

CPU 对照已补两厂真机 memory-pool oracle 各 400,453 项；Mali 真实游戏启用 dual upload 完成营地移动和原存档。官方 Perfetto 的 OFF/ON replay 平均 Running 为 20.091/20.104 ms，wall 为 21.128/21.280 ms，运行期加权 CPU 频率 2092.42/2088.98 MHz，未建立净 CPU 改善。OFF 样本解析器最初不接受千位逗号，既有录制已恢复，原失败元数据保留且缺 sample-after；ON scheduler 目标60→30，而其后独立 simpleperf 窗口30→30，不能据此归因 FPS。ON 的新复制循环/短尾仅有5/636个翻译线程 SELF 样本（0.7862%），不能把含旧 iterator/authority 的粗集合当成循环开销。暂保留可选实验，继续核总工作量成本；不将减少逻辑源读取宣称为 DRAM 或温度改善。见 `upload-dual-store-v1-mali-cpu-comparison.json`。


### 顶点属性的相邻相同范围不重复查询历史

实际 LLVM 样本落到 vertex attribute require 调用点；已有完整 API 帧的 7,201 次按 attribute 要求可在稳定条件下合并成 2,360 个相邻 exact range，这是机会计数而非当前时间节省。新代码仅在同一 draw 内省略紧邻的完全相同 address/size；不同范围替换 key，A,A,B,A 仍重查最后 A，UBO/IB、实际 buffer resolve/上传/bind 不变。首次 require 前后 command/recording serial/pass/buffer epoch/state epoch 必须不变，任何 pass-end/ghost 新 writer 使下一相同范围保守再查。无跨 draw 缓存、stream bitmask、新原子/锁/分配或运行期开关。

真实 Vulkan GPU 用例覆盖两个 stream 同范围、部分别名、跨 draw retarget、transfer/compute VBO writer、原 UI ghost 导致新 writer、首 A 回调、溢出/全局回退，正确像素及依赖均通过。独立不安全 seen-bitmask 变体在实际 GPU 执行后由官方 SYNC 验证捕获 READ_AFTER_WRITE，不能用仅减少调用数代替正确性。完整 async 21,823,451 / sync 21,724,039 和专用10例、原 abandon+PSO 故障通过，最终零 VUID/SYNC；marker57/58冲突的首次失败已保留，最终换65/66并拒绝重复marker。额外成本是未合并调用前后的四个小 getter、比较与可能的 ARM spill；没有净 CPU cycles、帧率或发热收益结论。见 `vertex-require-dedup-v1-source-ready.json` 及独立审查报告。

### 新增只读深度附件在 union pass 入场前同步

同一 9f6a0ae 包在高通实际完成112次转换、0回退；Mali 则325次计划全部回退。官方 LLDB 已将 Mali 阻断定位为旧 D24S8 STORE → 新只读 DS attachment 的真实 RAW：旧 writer 早于 pass begin，两 aspect 都有 DS_WRITE 历史。不能移除 attachment 保护或清掉 writer 来强行命中。

当首段没有 DS、次段新增受限单层单 mip 2D 只读 DS 时，创建合并 pass 前通过现有资源依赖机制建立 LOAD／实际 STORE 的 scope。支持 STORE_NONE 时只需 DS_READ，否则还含 DS_WRITE，覆盖旧 Shader 读取到 STORE 的 WAR。仅发布已经建立的 barrier destination scope；不伪记未来 draw/STORE，不清除 readers/writers 或推进写入时钟。回调前排空 deferred clear，前后核对 image lifetime、recording、pass、view/layout，最后重验所有输入 token；复杂/shared DS 保留原保守路径。

新增18项真实 GPU 用例覆盖 NONE0/1、旧 STORE、FS reader WAR、前后 clear、shared DS、guest alias、PSO 故障及两 aspect/history 像素；原26+9项保留。旧9f生产代码配同一新增用例严格失败：plans1/draws0/aborts1/passes3；修复为 plans1/draws1/aborts0/passes2，且实际观察到 DS_WRITE scope。独立完整 async 21,813,081／sync 21,713,669 零 VUID/SYNC；专用新增用例由单独 selector 验证，不能把完整套件数量冒称包含这些新增用例。增加一次受限 DS 查询和必要的真实屏障，无新 Shader、游戏补丁或 GPU 回读。真机转换命中及物理 tile／带宽收益待测，报告 `pixel-local-union-ds-preflight-v1-source-ready.json`。

高通已有同包官方 SDP OFF/ON 数据实际转换319次、0回退，但按完成帧归一的读/写/纹理流量变化均不足1%，边界、PSO增长及场景差异仍存在，不能宣称硬件 GMEM 合并或净带宽收益。后续局部转换将对齐原 Metal 的 producer 交集，避免把外部 history 无条件纳入 attachment；这需要离线证明的独立模块，不能在运行时删改已编译输入。

### 局部 input 转换：外部 history 保留普通采样

原 Metal 只把 producer MRT 交集采样转成 fetch。新 DPL2 对完整同 UV 组及每个非空 single-omission 子集离线生成独立、源 SHA 绑定的模块；4个成员为5种，非15种幂集。mask 位表示规范化完整组的排序位置，非 Vulkan binding 编号。loader 检查 full-first、完整组数量、唯一 mask、严格绑定/LOD 子序列和各实际模块身份；DPL1 继续读取，缺少精确子集则普通绘制回退。运行时在 producer 与 consumer 均重新证明交集，只将对应成员转为 input，history 描述符与真实依赖继续走普通采样；无运行时 SPIR-V 改写。

新 GPU 用例的4种 history 槽位置均从4 inputs/5 attachments 降为3 inputs/4 attachments；多于一个外部成员缺精确变体、无本地成员等安全回退，全部本地仍使用 full。原 local feedback、writer、query、PSO 故障、abandon、STORE/history 像素保护保留。原 mip case8现可合并：producer/consumer 都只读外部 history 的不同 mip，history 已不作为 attachment；真正本地反馈负例不放宽。独立42项 GPU 用例、14项 loader 损坏控制及完整 async21,823,451/sync21,724,039 通过零 VUID/SYNC；主线与 DS 修复合并后的专用263,748项及完整async21,823,449项也通过。首轮主线夹具漏通用 FS9 的失败保留，补用已有完整夹具材料后通过，未改生产代码。

新增成本：318个非空 sidecar 真实生成后580→2886 records、56,042,480→324,755,824字节，增加约269MB磁盘/工具缓存，不能当作 RSS；最大单文件2,508,680B。Q已见30个 eligible FS 若全部 query，其 retained blob 上界3,325,360→13,911,632B，非实际已加载量。P35仅3696→18656B。模块/schema 按需建立，但被读 sidecar 的完整 blob 会保留。consumer 为跨 geometry 回调重验，P35 descriptor/sampler 检查从4增至4+3；planner 不重复解析 selected。逻辑附件减少不代表物理 tile 合并、DRAM 或 FPS 改善，仍为默认关闭的候选，需重新生成/验证完整 shader chain 后真机验证。见 `pixel-local-subsets-v1-implementation-report.json`。

DS 入场修复已实际部署：Mali 普通游戏3秒窗口+236 plans/+236 draws/0 abort，高通营地移动211 plans/211 draws/0 abort，画面/HUD人工静态检查正常并原生命周期两次存档停机；静态截图不能排除动态闪烁，且尚未证明帧率、发热改善。Mali 原厂单次OFF/ON对照另行记录边界，不能把开启命中当作性能收益。

### 有限范围 pool 维护，避免一次跨窗读取提升整池

原厂 LLDB 在真实 Mali 选角阶段捕获：400MiB池已有[0,16)、[128,144)、[144,160)MiB三个段；唯一缺失需求是VS只读SSBO范围[28,373,808,47,947,216)，长19,573,408B，横跨两个尚未backed窗口。它不是VBO、unknown/reference或节点预算回退。按16MiB对齐只需新增[16,48)MiB，累计backing可为80MiB；旧策略提升400MiB。320MiB是本次规划上避免的扩张，不是已测RSS、DRAM或帧率收益。

可选window策略现在把完整触发require带到原有全quiescent维护边界，合并其窗口对齐范围与真正相交的旧段闭包。新VkBuffer使用非零逻辑origin，初始化页面保留拷贝按该origin重定位；不缩Shader声明的范围、不上传guest旧内容、不逐出GPU数据。锁内固定selected旧节点，保留lifetime pin并在copy完成/command reset/recycle后验证原pool identity、generation和splice anchor。仅退役selected，外段exact handles/generations保持；异常、OOM或同地址pool替换不得向新pool错误发布。脏页、shadow、authority与initialized bitmap不变。

默认whole策略不变；fresh pool首次宽绑定、未知/零sizeVBO、完整pool/reference、预算等仍用原whole回退。本轮不做异步迁移：即使零旧段的新建也仍全finish/recycle、独立保守barrier/submit/wait；增加cold-path指针快照及heap节点，旧选中buffer会与replacement暂时共存。后续范围可继续扩张，初始80MiB不等于长游玩仍80MiB。

真实GPU夹具包含完整大SSBO图形绑定的256像素oracle、GPU-only旧页/外邻字、CPU脏页/冷洞、非零origin/prefix、尾193B、外邻handle保留、OOM回滚、实际同址重建和copy前追加外段。刻意省略GPU保留copy的负控由实际GPU回读memcmp失败，而非只看计数。独立完整async21,823,961/sync21,724,549零VUID/SYNC；合入subset+DS后的owned522项、pixel-local/window16/PSO故障263,762项和完整async21,823,959项也通过零VUID/SYNC。旧257-writer夹具补齐原operation admission，未放宽生产missing-admission保护；早期错误夹具目录的缺shader失败保留，不能算通过。Android真机实际backing/PSS和稳定性待测。见 `pool-finite-consolidation-v1-source-ready.json`。

补正Mali原厂计数解释：DS修复对照下降约3.4%的是 `ITER_FRAG_TASK_COMPLETED`，PA pixels由此派生；真正 `FRAG_PTILES` 上升约0.142%，外部流量+0.804%、GPU周期+3.10%，没有物理pass合并或净提速证据。旧capture未选择可用的 `ITER_FRAG_JOB_COMPLETED`，其APC不能补出job拓扑；后续必要的新候选采集将选择该官方counter。见 `pixel-local-ds-preflight-v1-hardware-merge-evidence.json`。
