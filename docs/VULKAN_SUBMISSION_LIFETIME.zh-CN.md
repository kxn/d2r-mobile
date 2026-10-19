# Vulkan 提交与资源寿命审查

## 结论与依据

NVN 的命令录制/队列提交并不要求 CPU 每次等 GPU 完成。本项目原接口层把提交与出图
排进翻译队列；显式栅栏、CPU 读回、交换链纹理再获取才形成必要等待。
原 Metal 也是异步提交，并非依靠同步 NVN 逻辑运行。

Vulkan 为初期正确性引入了更强的串行条件，默认对照仍保留：`nvn_vk.c:mtl_sub_begin` 每次先
`mtl_finish`，等此前提交和 CPU 可见副作用全部完成。`nvn_vk_window.c:vk_window_present`
又在出图拷贝提交后调用 `vk_internal_wait`，再请求显示。这些等待不能解释为 NVN API 的固有要求。
以上是默认串行对照。第六步已让调试开关同时覆盖 begin 与出图的设备端依赖，见文末；
尚不能视为所有 CPU 消费路径均已异步兼容。

原 `nvn_mtl.m:mtl_sub_begin` 按已完成栅栏选空闲命令分配器，没有空闲时增加分配器，
仅达到 `ALLOC_MAX=8` 后等最老的一份；`mtl_sub_commit` 提交后返回，在完成回调中
发布查询/事件副作用与 `g_done`。`arena_commit` 与延迟释放按栅栏保活资源。
原实现也会在显式完成、读回、资源上限或取图时等待，不能概括成“Metal 从不等”。

## 真机证据

新 APK、2772×1240 输出、3D 1.0 固定、无 RenderDoc/校验/自定义 GPU 计时：

- 四个实际上屏时间窗共 256 个间隔，平均 110.312 ms / 9.065 fps，空闲 HWUI 重画 0。
  记录：`.local/logs/android-native-resolution-normal-present.json`。此轮 `control=none`，
  阶段标签不代表渲染开关变更；不是与旧 720p/掌机配置的受控提速对照。
- Perfetto 约 7.989 秒记录中，翻译线程 TID 12751 执行 2.460 秒、睡眠 5.364 秒。
  142 段完整睡眠后均由 GPU 完成线程 TID 12733 唤醒；执行队列等待约 68 ms。
  记录：`.local/profiles/android-native-resolution-scheduling-v2.*`，含查询 SQL/CSV。
- 跟踪里有厂商 FPSGO `C|线程号|...` 元数据及 1454 个 systrace 解析错误，部分线程的
  推断进程归属冲突。分析改按采集前后实际 Linux 线程 ID 筛选，不能使用旧 PID-only 报告。
  调度事件不等于 GPU 执行轨迹；睡眠也可能含驱动等待、CPU 回写和完成回调成本。
- 当前 GLES 驱动字符串 r32p1，Vulkan driverVersion 为 32.1.0；Perfetto 未取得 GPU
  render-stage 轨迹。Arm Timeline 文档要求至少 r47p0 的基础轨迹支持，不能靠加载层
  假设当前驱动具备此能力。Arm gatord 硬件计数器仍可采集。

此证据支持优先审查过强同步，不证明删等待后的提速幅度，也不证明 GPU 没有其他瓶颈。

## 等待链与优先级

NVN 是图形 API，不是一种“同步绘制原语”。CPU 函数返回、GPU 完成命令、显示器实际
呈现是三个不同的时点。适配层须保留原 NVN 可观察的顺序，但不必把所有 GPU 依赖变成
CPU 栅栏等待。一次提交也不等于一帧；原 Metal 的 8 份 allocator 上限不能解释成预渲染 8 帧。

1. **已确认的主机串行链。** 当前 begin 先等完成线程，CPU 才能准备下一提交；GPU 工作
   提交后，下一 begin 又等 GPU 和 CPU 副作用。准备与执行无法充分重叠，可能形成交替空档。
   等待还会经过线程唤醒和重新调度；原 Metal 的注释也明确记录固定少量 allocator 导致热态
   等待和 CPU 降频的问题。本机是否发生同样降频仍须独立核对，不能把上游结论当本机证据。
2. **Android 镜像引入的真数据依赖。** 本机没有可用的 external-host-memory 导入支持，
   `sync_pools` 会把客体脏页写进一致映射的 Vulkan 缓冲；GPU 写入又经完成线程拷回客体。
   Vulkan 的 HOST_COHERENT 只免去显式 cache flush/invalidate，不保证 CPU/GPU 同时访问
   同一范围安全。因此即使分开 descriptor/command pool，仍不能直接移除 begin 等待。
   应按实际在途读写范围管理冲突，临时只读数据可分配新版本，真正相交的 CPU 消费保留等待。
3. **GPU 内部过强依赖是下一层问题。** `vk_barrier_at` 当前使用 ALL_COMMANDS 与广泛
   读写访问掩码，并结束 render pass；这没有直接阻塞 CPU，但可能限制 GPU 通道重叠并在
   Mali 上增加附件存取。须结合实际资源用途细化 stage/access 和 pass 边界；不能先把所有
   barrier 删除，也不能从 CPU 睡眠数据推算这种成本。

理想重叠时稳定帧间隔更接近 CPU 准备和 GPU 执行两者的较大者，而不是二者相加；仍受
真正跨帧依赖、显示节拍和资源上限约束。目前没有足够的 GPU 时间线证据承诺目标帧率。

## 移除全局等待前必须拆开的资源

下表记录本轮改造前的依赖；已完成的第一步列于后文，未完成项继续保留安全等待。

| 资源/行为 | 当前串行依赖 | 异步提交需要的对应 |
| --- | --- | --- |
| 主/次级命令缓冲与命令池 | 下一提交回收 `vk_retired` 与旧绘制任务 | 按提交持有，GPU 完成后在所有者线程回收；命令池访问仍须宿主同步 |
| 描述符池 | `vk_shader_begin` 重置所有池 | 每个在途上下文独立池；完成后才重置，不只增加 descriptor 数量 |
| 常量、只读存储与驱动常量缓存 | begin 清表，临时缓冲全局复用 | 录制期缓存与其实际 GPU 数据按上下文持有；不能仅保留 CPU 键 |
| 临时上传/拷贝缓冲 | begin 全部加入可复用表 | 对提交绑定的暂存对象等实际完成后再循环使用 |
| render pass/framebuffer | `vk_graphics_begin` 销毁旧任务 | 持有到最后引用的提交完成；次级命令缓冲同时保活 |
| 时间戳、遮挡与分段查询 | 共享池/索引，完成线程读取全局状态 | 查询槽和读取描述随提交快照；上次结果消费前不能重置 |
| CPU 池与 Vulkan 一致映射镜像 | `sync_pools` 在全局完成之后上传脏页 | 跟踪在途读写区间；与 GPU 相交的 CPU 修改须等待或使用新数据版本 |
| GPU→CPU 回写、事件、查询结果 | 完成线程先回写再发布 guest fence | 保持“GPU 完成→回写/副作用→通知游戏”的顺序，不能提前报告完成 |
| 图像/视图/程序/采样器退役 | 依赖当前完成顺序与快照宽限 | 覆盖所有在途引用后才释放，不能仅按提交次数猜测完成 |
| Android acquire/present | acquire 用 CPU fence 等图，拷贝完成也由 CPU 等 | acquire/render-finished semaphore 连接 GPU 与显示；每图/每上下文的信号量复用要分别证明 |

GPU 内部读写顺序仍由 barrier/队列依赖保证；同队列不能代替正确内存可见性。
CPU 等待只留给实际 CPU 消费结果、游戏显式等待、复用未完成资源、后台保存与销毁等边界。
设备的 `VK_KHR_timeline_semaphore` 现已在查询 feature 后按需启用；无扩展/feature 的设备
保留 fence 路径。二进制 semaphore 与 fence 也可表达同一架构，不应把 Vulkan 1.3 当成本移植前提。

## 实施与验收顺序

1. 对应原 Metal 建立按提交分配、按完成编号退役的上下文，保持现有串行路径作参考。
2. 将表中各资源、查询结果描述和副作用移入其正确寿命；先保留全局等待通过正确性回归。
3. 用原创 GPU 命令构造两份以上同时在途、GPU 仍读旧数据时 CPU 产生新数据、查询复用、
   资源提前 finalize、CPU 等待时副作用可见等用例，校验真实像素与宿主可见时序。
4. 分别放宽提交入口和出图路径的 CPU 等待；每次通过 host/手机同步校验、真实营地/移动/
   面板/后台原存档回归，最后在同分辨率与相同 NVN 配置下对照净收益。

不修改游戏绘制命令、相机、玩法或原游戏 binary；不以删除绘制、少做回写或降低画质换性能。

## 已实施：暂存与诊断查询归属

`nvn_vk.c` 已引入每提交的 `submit_resources`，持有临时上传/回读缓冲、整提交时间查询、
分段查询和其元数据。commit 将整份归属交给在途 submission；完成线程使用该份查询与
元数据，先完成 CPU 回写/回调，再把整份交回。提交线程分离已完成列表后回收命令缓冲、
有界复用暂存缓冲和查询资源；完成线程不访问 Vulkan 命令池。

诊断开关在 begin 锁存，停用时不会因为旧查询池存在就继续写入。新增真实 GPU 用例交替
启停，并在录制中反向修改环境；核对 GPU fill 数据、CPU 拷贝、回调和 guest fence 顺序。
主机 lavapipe 正常/同步校验 489446 项、外部采集模式 489443 项通过；手机 APK 的 Mali
同步校验夹具通过 489446 项，无 VUID/SYNC 错误。记录为
`.local/logs/vulkan-submission-resources-host-{sync,external}.log` 与
`.local/logs/android-submission-resources-vulkan-test.log`。

同一 APK 在真实游戏 2772×1240、固定 3D 1.0 下完成营地/英文物品栏/实际移动往返/
原后台存档回归，完整管线键核对 1152079 次，107 条分段 GPU 结果，未见同步校验错误。
源代码和 APK 哈希、真实玩家/输出状态及新鲜原存档两次提交证据关联于
`.local/logs/android-submission-resources-verification.json`。该运行开启完整校验与计时，
不能用它的帧率作为正常性能；之后恢复无注入外部采集模式。
恢复正常模式后四窗共 256 个上屏间隔为 9.044 fps、110.569 ms，HWUI 重画 0，
仍是约 9 帧；不宣称此次归属改造有净提速。手机 main/sdk/subsdk0 镜像哈希仍与原构建清单一致。

这一步尚未让完整提交并发：descriptor pool、绘制任务与镜像冲突等仍依赖 begin 的全局等待。
不能用串行夹具证明多份同时在途已经安全；下一步继续拆这些归属，再加入实际多在途验收。

## 已实施：描述符、常量缓存与绘制任务归属

第二步将 `vk_shader_submission` 和 `vk_graphics_submission` 纳入 `submit_resources`。
描述符池及其分配游标、常量/只读存储/驱动常量副本缓存、存储回写记录都归当前提交；
render pass、framebuffer 和次级命令缓冲也归当前提交。commit 断开 CPU 录制游标，完成线程
仍不访问命令池；提交线程只对已完成的一份重置描述符、清缓存、销毁绘制任务，再复用暂存。
着色器、已编译管线及不可变常量仍可共享；不改变 NVN 状态键、绘制命令或图像质量。

新增 `native_vulkan_submit.inc` 使用两份独立描述符资源、三份真实 Vulkan 提交：
A/B 分别等待主机可发信号的 timeline semaphore；只解除 A 并等待其 fence，在 B 确实未完成时
回收 A，再复用 A 编 C。18 个描述符集合跨越夹具每池 4 集合上限，逐字验证 1152 个 GPU
计算结果，并核对没有推进 guest fence。主机正常同步校验 490606 项、外部采集模式 490603 项、
手机 Mali APK 同步校验 490606 项全部通过；用例没有跳过，手机实际启用了该 feature。
记录：`.local/logs/vulkan-submit-bindings-host-{sync,external}.log`、
`.local/logs/android-submit-bindings-vulkan-test.log`。

这是描述符资源隔离的真实在途验证，尚不证明整套 NVN 提交并发安全。完整 begin 全局等待仍保留，
镜像页在途读写、GPU→CPU 回写、临时 image/buffer view、图像退役和出图同步等仍需继续处理。
特别是 `vk_texture_end_submit` 仍统一销毁上次创建的临时 view；`vk_buffer_writeback` 的回写源
仍指向共享镜像，后续 GPU 写入可能早于完成线程拷回。两者都必须先拆开，不能只处理描述符池
后就删除 begin 等待。
timeline semaphore 是同步机制，不代表 r32 驱动已经支持 Arm GPU Timeline 的性能轨迹。

同一 APK 的真实游戏原生分辨率、固定 3D 1.0 回归已完成英文物品栏开关、实际移动往返和
两次原后台存档提交；完整管线键核对 276693 次、52 条 GPU 分段结果，未见同步校验错误。
源码/APK/未变游戏镜像哈希及实际状态见 `.local/logs/android-submit-bindings-verification.json`。
恢复无校验/无注入正常运行后，四窗共 251 个实际呈现间隔为 8.876 fps、112.668 ms，
HWUI 重画 0；仍约 9 帧，不能作为取消等待的性能结果。此轮与上一 APK 是顺序冷启动，
不是受控提速/退化对照；手机最终保持原生输出和固定 3D 1.0，USB/Wi-Fi ADB 均正常。

## 已实施：临时 texel view 与写回快照

第三步把临时 `VkBufferView` 归入 `vk_texture_submission`，随 `submit_resources` 保活；
只在真实 GPU 完成及 CPU 回调结束、描述符池回收之后销毁。原先 transient view 结构中的
image-view 字段没有创建者，已移除；真正的 image view 仍由 image 缓存持有，图像退役还需继续改。

`vk_buffer_writeback` 改为登记每个池的有序字节范围，合并相交和相邻范围但保留空隙。
提交尾部 `vk_buffer_writebacks_flush` 把最终 GPU 内容拷到本提交独占的暂存，再登记客体/
shadow 回写；随后 GPU 覆盖共享镜像不会改变此快照。完成线程仍先发布所有复制，再执行回调/
事件，最后发布 guest fence。`vk_pool_read` 同时检查未封口范围与已登记副作用，不能因为
延迟分配快照而读到旧值。显式 flush 后新增写入须重新登记。

新增 GPU 用例核对提交尾最终值、flush 后原缓冲被覆盖、未对齐/重复/相交/相邻范围及空隙。
原 NVN texel 纹理注册/绑定/dispatch 路径也执行了原创 R32UI 计算着色器；另让两份 view 资源
真实同时在途，只回收并复用已完成的一份，逐字核对结果。主机同步校验 492220 项、外部
采集模式 492217 项、手机 Mali APK 同步校验 492220 项通过，未见 VUID/SYNC 错误。
证据：`.local/logs/vulkan-submit-readback-host-{sync,external}.log`、
`.local/logs/android-submit-readback-vulkan-test.log`。

这不是完整并发 NVN 验收。begin 全局等待仍保留；CPU 上传镜像与 GPU 在途读写的冲突、
CPU 读取前一提交尚未发布结果、image/采样器退役及 acquire/present 同步仍待处理。
快照与范围合并的性能须另测，不能用此夹具数量推断帧率。

同一 APK 已通过原生 2772×1240、固定 3D 1.0 的真实英文营地/物品栏、实际移动往返和
两次新鲜原后台存档提交；完整管线键核对 273049 次、GPU 分段结果 51 条，未见同步校验错误。
代码/APK/未变三份游戏镜像哈希及实际状态记录在
`.local/logs/android-submit-readback-verification.json`；校验运行不能当正常帧率。
恢复正常模式后四窗共 246 个实际上屏间隔为 8.783 fps、113.861 ms，HWUI 重画 0。
未宣称净提速；最终仍是原生输出/固定 3D 1.0，完整提交等待尚未放宽。

## 已实施：实际完成序号与待发布 CPU 范围

第四步给每份录制资源保留内部 queue serial，commit 核对实际编号；completed serial
只在 GPU、查询读回与全部 CPU 副作用结束后发布。image 和已替换 sampler 记录最后引用它的
录制序号，退役必须同时满足原翻译快照宽限期与该次真实完成；内部提交也计入完成域，不能把
guest fence/已提交数量当作 GPU 已完成。纯 CPU 的 sampler 查询不会把 last-use 清零。

每提交还快照一份不可变 CPU 写入范围表，供在途查询使用；完成线程释放副作用节点时，范围表
仍保留到提交线程回收。`vk_pool_read` 区分当前未封口/已封口写入和前序在途写入，先放锁再等待
真正相交的前序结果。原拖动物品淡化路径改用同一查询，避免延迟生成快照后漏掉尚未封口的写回。
这没有新增游戏判断或改矩阵数据。

新增原创 GPU 用例用真实底层提交满足宽限条件，再用 timeline semaphore 挡住实际采样提交：
此时 finalize 图像、替换 sampler 并尝试退役，核对对象仍在；解除 GPU 等待、核对 64 个采样值
及 CPU 范围发布后，再核对对象已释放。未修改计数器/游戏状态来制造宽限条件。
未封口、已封口、在途、完成后的写入范围及空隙/端点均有断言。
主机正常/外部采集同步校验为 492322/492319 项，手机 Mali APK 为 492322 项；未见 VUID/SYNC 错误。
记录：`.local/logs/vulkan-submit-retirement-host-{sync,external}.log`、
`.local/logs/android-submit-retirement-vulkan-test.log`。

完整 begin 等待尚未取消。CPU→GPU 上传还须按在途 GPU 访问范围处理冲突，打包纹理/其他 CPU
镜像读取与出图依赖也需覆盖；上述隔离用例不等于整套并发 NVN 或完整游戏验收。

同一新 APK 已通过真实英文营地/物品栏、移动往返和两次新鲜原后台存档提交，输出保持
2772×1240/固定 3D 1.0；完整管线键核对 424715 次、59 条 GPU 分段结果，未见同步校验错误。
源码、APK、未变游戏镜像与实际状态关联在 `.local/logs/android-submit-retirement-verification.json`。
物品淡化分支本轮验证的是写入依赖查询，并未以面板开关冒充拖动交互全项验收。
恢复无校验/无注入正常模式后四窗共 253 个实际上屏间隔为 8.965 fps、111.540 ms，
HWUI 重画 0；不宣称净提速。手机最终保持原生输出/固定 3D 1.0，完整 begin 等待仍保留。

## 已实施：池访问冲突与有界并发入口

第五步给每份提交记录池的 GPU 访问范围，合并相交/相邻区间；池在真实 GPU 访问和 CPU
副作用全部结束之前保持引用。池同步先保活池列表，再放开资源锁扫描脏页；先等该范围前序
CPU 回写，只有 guest/shadow 实际不同才等覆盖实际上传范围的 GPU 访问，然后更新镜像。
不相交页可直接上传，不持 `V.resources` 等待完成线程；池 finalize 等引用释放再销毁缓冲。

新增 `DM_VULKAN_ASYNC=1` / 启动脚本 `--async-submit` 调试开关。开启后 begin 不再每次
全局 finish，而是回收已完成资源；最多 8 份，没有空闲份才等最老提交。默认仍保留串行对照。
fixture 用真实 timeline semaphore 挡住 GPU：一项证明不相交上传继续、相交上传等待，并
逐字保留旧 GPU/新 CPU 内容；另一项经两次真实 `mtl_sub_begin/commit` 让两份 backend
提交同时在途，检查独立暂存和待发布 CPU 范围，解除等待后核对输出与发布完成。

主机并发正常/外部采集为 492666/492663 项，Mali 同 APK 并发/串行为 492666/492527 项；
串行模式明确跳过“同时在途”用例，未见 VUID/SYNC 错误。串行测试曾因手机熄屏被 OEM
Hans 冻结，主机观察超时；唤醒后同一测试完成，不能把这次停顿当作 Vulkan 死锁。
记录：`.local/logs/vulkan-pool-hazards-host-{async,async-external}.log`、
`.local/logs/android-pool-hazards-vulkan-{async,serial}-test.log`。

同 APK 的并发调试模式已通过真实英文营地/物品栏、实际移动往返及两次新鲜原后台存档
提交，2772×1240、固定 3D 1.0；45 条 GPU 分段记录，未见同步校验错误。
源码、APK、未变三份游戏镜像和实际状态关联见
`.local/logs/android-pool-hazards-verification.json`。这不是完整异步兼容或帧率验收：
出图仍 CPU 等待；打包纹理的 CPU 解码/去 swizzle、压缩拷贝回退和其他 CPU 消费路径须
继续审查，不能在持有描述符/图像裸指针的深层函数里随意拆提交并回收资源。

首版范围记录同 APK、正常模式的串行→并发→串行实际上屏为 6.699→6.636→6.780 fps，
每组四窗，HWUI 均 0；翻译线程约 0.49→0.50→0.49 核秒/秒，单独放宽 begin 未显示收益。
记录：`.local/logs/android-pool-hazards-comparison.json`。这也暴露出范围记录的线性链表
热路径：大量小范围绑定会反复从表头扫描，不能把这个额外 CPU 成本当作 Vulkan 的固有代价。

现已改为每池直接索引当前录制份、每份用 4 KiB 页位图记录 GPU 使用；小范围更新直接定位
位，跨字范围批量填充，不按绑定数量反复扫描。查询按实际复制段覆盖的页检查，允许保守多等，
不能漏掉字节访问；精确 GPU→CPU 写回字节区间不变。完成线程仅在序号匹配时清除池索引，
不会清掉较新提交的引用。池保活及先发布 CPU 副作用后通知完成的规则不变。

补充 GPU 用例覆盖首位、跨 64 位字边界、完整中间字、池尾及同一池跨提交复用。
主机串行/并发/并发外部采集为 492648/492787/492784 项，Mali 同 APK 并发/串行为
492787/492648 项，均未见同步校验错误。证据为
`.local/logs/vulkan-pool-index-host-{serial,async,async-external}.log`、
`.local/logs/android-pool-index-vulkan-{async,serial}-test.log`。
新 APK 并发模式的原生营地/物品栏、移动往返及两次原后台存档提交亦通过；51 条 GPU
分段结果和无同步错误记录、源码/APK/未变游戏镜像指纹关联在
`.local/logs/android-pool-index-verification.json`。整套异步兼容仍未验收，出图等待仍保留。

页位图版同 APK、正常模式串行→并发→串行为 8.636→8.588→8.676 fps，翻译线程
0.327→0.330→0.328 核秒/秒，三组各四窗且 HWUI 均 0。记录：
`.local/logs/android-pool-index-comparison.json`。这消除了首版链表的明显 CPU 回退，
但 begin 并发本身仍未显示净收益；不能把恢复到约 9 帧作为低帧率问题已解决。

## 更根本的性能工作

目标按 30 fps 的 33.3 ms 整帧预算检查。目前约 115 ms，翻译线程约 38 ms CPU/实际上屏帧；
仅取消等待不足以满足预算。此前同 APK 的原生→720p→原生对照中，输出像素差 3.73 倍而
帧间隔仅差约 1.30 倍，说明不能只优化像素数量。后续须同时降低 CPU 工作和 GPU 执行依赖。

- 宿主 CMake 仍把整个 Vulkan/HLE 与原功能模块固定为 `-O1`；现新增可复现的
  `DM_ANDROID_RUNTIME_OPT_LEVEL` / APK 构建参数 `--opt-level`，默认 `3`，保留符号和栈帧，
  无 fast-math。须通过同着色器、原生输出的真机回归和实际帧时验收，不能用编译通过推断收益。
- 资源绑定目前每次绘制/dispatch 分配和更新 descriptor set；须按实际资源身份/版本复用，
  保留写回与视图升级等副作用。CPU 来源常量的对齐副本还可用映射上传环避免 GPU transfer
  和通道切断；GPU 来源仍按依赖用 GPU 拷贝。先量化每帧操作量，再做对应通用实现。
- GPU 路径须从全局屏障改为资源用途和状态的依赖管理，并处理 acquire/copy/present 的
  设备端衔接。全局裁剪图像、源交换链重用和后台存档均需一起处理，不能只删除 CPU wait。
- 原 `set_time_constraint_comp` 的 Android 分支为空；真机 ADPF 支持会话，图形线程上限 5，
  当前游戏没有自己的会话，Manifest 也未声明游戏类别。需补 Android 的平台调度适配并实测，
  不能直接移植 Apple 实时策略、锁频或把 GPU 等待误报成 CPU 工作量。

官方计数器新记录为 `.local/profiles/android-pool-index-structural.apc` 及导出目录；未启用
CPU 调用栈/截图/GPU layer，临时权限已恢复。它是系统 GPU 计数器，不是每通道独占时间。
对实际加载的 111 个 shader 作 G710 静态分析，16 个片元 shader 有栈溢出，热点估计
396–606 字节的溢出读写；见 `.local/logs/android-mali-structural-shaders.json`。
编译器估计使用 r56p1，手机是 r32p1，不能把这些数直接换算成真实帧率。

尝试打开原翻译器 AST 表达式优化时，先复现了调用实参局部存储丢失的编译失败。候选修复
通过 AST 回归、全量 6931 份 SPIR-V 校验及五份 debug 参考产物逐字节不变检查，但优化链
`11d81bd4309a` 在真机使人物/NPC 出现黑块，故**拒绝验收、未接受帧率结果**。
确认新鲜原后台存档后，完整核对旧缓存清单哈希并恢复 `3a0ffe903acf`，画面恢复正常；
实验性的 vendor/compiler 改动亦从主线撤回。补丁、静态报告和测试保留在
`.local/build/mali-optimized-ast/`；拒绝及恢复记录见 `.local/logs/android-optimized-ast-rejected.json`。
C1 逐位常量折叠与 GLSL 往返仅为本地编译实验，未部署，不以其估计改善冒充真机提速。
后续编译优化必须有足够的数值/像素差分依据，不能把全库编译通过当作语义正确。

## 下一步的依赖顺序

1. 先补 CPU 消费依赖。`image_upload` 在 CPU 上从 `t->mem` 去 swizzle/解码，须在读取及
   脏代号判断前处理 GPU 尚未发布的结果；decoded BC 的 B2T 回退仍只扫描 `V.effects.e.src`，
   会漏掉延迟快照的未封口写入，必须改用统一待写查询并在安全入口同步。不能在已分配
   descriptor/render-job 或持有可能退役图像指针的深处任意 commit/begin。相反，当前常量与
   只读 storage 的对齐副本实际是 `vkCmdCopyBuffer`，其 GPU→GPU 依赖不应再误加 CPU 等待。
   覆盖证明的顶点/索引 CPU 读取已使用 `vk_pool_read`，无需重新做一套等待。
2. 出图把 acquire semaphore 接到拷贝提交，按交换链图像持有 render-finished semaphore，
   present 等后者；不能拿普通提交 fence 完成当作 present 已消费信号量。裁剪中间图像当前
   是全局对象，异步后须按在途寿命保活/复用，尺寸变化时不能直接销毁旧引用。
   返回内部队列完成票号，让原 NVN AcquireTexture 在重用对应源纹理时等待；`mtl_present_wait`
   须使用同一内部序号域，不能把内部 serial 传给 guest-fence 等待。Surface 更换和后台存档
   的排空/丢帧机制须同时保留。
3. 再比较完整串行/并发的实际上屏与调度空档。若 GPU 执行仍主导，继续按真实资源用途细化
   `ALL_COMMANDS` 屏障及 render-pass 边界，检查 Mali 附件存取；CPU 睡眠时间不能充当
   可节省 GPU 时间，GPU 时间戳跨度也不能当作独占忙碌时间。全程不改游戏场景/相机/绘制逻辑。

参考：[Vulkan 同步规范](https://docs.vulkan.org/spec/latest/chapters/synchronization.html)、
[交换链信号量复用](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)、
[Arm Timeline 驱动要求](https://github.com/ARM-software/libGPULayers/blob/main/layer_gpu_timeline/README_LAYER.md)。


## 第六步：设备端 acquire/copy/present 衔接

`DM_VULKAN_ASYNC=1` 现也启用出图异步路径，默认串行对照仍保留。acquire 使用二进制
信号量，由拷贝提交在 TRANSFER 阶段等待；每个交换链图像独有 render-finished 信号量供
present 等待。acquire 槽按内部提交完成号回收，render-finished 只在相应交换链图像重新
取得后复用，不能按普通 submit fence 推断显示端已消费。出图返回内部拷贝完成票号，原 NVN
在重用该源纹理时等待，`mtl_present_wait` 使用内部序号域；不再混用客体栅栏。裁剪中间图像
被替换后的旧资源延迟到引用提交完成才释放，Surface 重建保留排空边界。

新增二进制 GPU 信号链、内部票号和 CPU 副作用可见性用例；既有 core 用例另外验证真实
同时在途。主机串行/异步/外部采集为 492648/492929/492926 项，Mali 同 APK 异步/串行
为 492929/492648 项。真实游戏原生输出的营地、物品栏、移动、HOME 原后台保存和同进程
Surface 重建恢复通过，109 条 GPU 分段记录未观察到同步校验错误。
`.local/logs/android-present-chain-verification.json` 关联源码、APK、未变游戏模块与运行证据。

**无明确吞吐收益。** 同 APK、原生 2772×1240、固定 3D 1.0、相同着色器、无校验/注入/
自定义计时，串行→异步→串行为 8.664/8.704/8.611 fps，各四个实际呈现时间窗，HWUI 重画
为零。原始报告为 `.local/logs/android-present-chain-{serial-a,async-b,serial-c}-present.json`。
`-O3` 也已通过真机正确性，但先前 O1/O3 正常运行约 8.632/8.591 fps，没有可认定的提速。
不能把异步架构或编译优化开关的完成当成 30 fps 目标已接近。

## 取图同步范围修正

原 `nvnWindowAcquireTexture` 在 CPU 上等完所取纹理的出图票号后，又把 acquire sync
绑定为整个队列最新 `x_fence()`。这会让本来空闲的纹理无故继承无关提交的等待。
现只在既有该纹理等待完成后返回已满足的 acquire sync；显式 `nvnQueueFenceSync` 语义不变。
专用新进程模式 `DM_VULKAN_ACQUIRE_TEST=1 DM_VULKAN_ASYNC=1` 经原 NVN 录制/提交，
真实 GPU timeline gate 阻塞无关队列任务：修正前两张未使用纹理的同步均超时，修正后立即
满足，而显式队列栅栏阻塞至 gate 释放。先释放 gate 再断言，避免失败用例留下挂起 GPU。
主机前后日志为 `.local/logs/vulkan-acquire-scope-{before,after}.log`；Mali APK 中可用
`run_vulkan_tests=true async_submit=true acquire_scope_test=true` 单独复现，同步校验通过。
此用例证明等待范围，不能证明游戏使用这一 CPU 等待或帧率收益；真实游戏仍须独立对照。


## CPU 已知拷贝来源的发布实验（默认关闭）

`DM_VULKAN_CPU_COPY_REFERENCE=0` / `--cpu-known-copies` 保留原 GPU copy、屏障和完成栅栏，
只在能证明来源字节时，以宿主自有不可变快照代替提交尾 GPU→CPU copy。
内联 UpdateUniformBuffer 从录制载荷复制；池间 copy 要求来源与本次未封口、已封口及在途
待发布范围均不相交，否则非阻塞回退到原回读。池来源快照在 commit 的 lazy upload 之后、
vkQueueSubmit 之前，复制实际上传的 shadow；不能在录制时读旧 shadow，也不能完成后读新 guest。
部分/相邻目的覆盖会使合并区间的已知证明失效，只有完全覆盖合并范围的新已知写才能替代。
完成顺序仍为 GPU 完成→所有 guest/shadow 字节副作用→回调/事件→通知完成；快照随后释放。
已通过来源 GPU 先/后写、字节护栏、目的覆盖、显式封口、晚 CPU 编辑和 timeline 阻塞的在途来源回退。
真机回读字节下降约42.77%，但两次吞吐对照一正一负，因此仍默认关闭；不能把少拷字节等同于提速。

## 写入者与消费者的区分

| 数据 | 写入者→后续消费者 | 当前语义与边界 |
| --- | --- | --- |
| 普通 VS 阶段输出 | VS→后续图形阶段 | 属于 GPU 管线内部数据，不登记为 guest buffer 回写，不能作为 CPU 回读理由 |
| 显式 storage/texel buffer | 声明可写的 shader→后续 GPU 或 CPU | 依据离线反射的写标记登记范围；可能来自 compute，也可能来自其他支持 storage 写入的阶段，不能只按阶段猜测 |
| buffer copy/内联更新 | GPU transfer→后续 GPU 或原池的 CPU 观察者 | 当前镜像为原生 guest 指针保留完成后的可观察内容；已知 CPU 来源实验只替换回读来源，不提前通知完成 |
| 图像→buffer | GPU transfer→GPU 或显式 CPU 读取 | 当前登记目的池写回；原样保持 CPU 可见语义，真正延迟到消费时回读需要完整跨提交版本追踪 |
| 常量/只读 storage 对齐副本 | GPU transfer→shader | 属于 GPU→GPU 依赖，不应加 CPU 等待；可写对齐副本另有 copyback 与最终发布 |
| c0/c2 地址/句柄表 | 宿主生成→shader | 不是 GPU 结果；查重无需读取 GPU 映射内存，CPU 自有键候选按提交保活且默认关闭 |
| timestamp/遮挡查询 | GPU query→原 NVN report/条件判断 | 保留结果与事件发布顺序；查询池及 report 按提交保活 |
| 纹理解码、去 swizzle、覆盖证明 | 池数据→宿主 CPU 算法 | 是实际 CPU 消费；可选覆盖证明遇待写已非阻塞放弃，纹理路径仍有前文所列未完成审查 |
| 截图/逐绘制 trace | GPU image→宿主文件写入 | 显式诊断路径，正常外部 profile 不开启，不能把诊断回读量当作正常游戏负载 |

登记“GPU 写过”与证明“游戏当时需要 CPU 回读”是不同问题。当前提交尾镜像发布仍保守，
不能从大量 transfer/storage 写入推导出游戏设计了大量同步回读，更不能把普通 VS varying 当作顶点池写入。
