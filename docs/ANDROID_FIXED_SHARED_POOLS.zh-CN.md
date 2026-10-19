# 固定模式、整池延迟导入实验

基线 ff2363a2cbbc327afca3002e6a2a0e0c1d1707cd。此树不包含旧只读窗口、排除证书、每 Pool pin 预算或镜像退窗。构建选项 `DM_VULKAN_SHARED_POOLS` 默认 OFF；仅独立底层功能实验，不是可启用的游戏版本。

## 已实现的 Buffer 路径

编译 ON 后，启动时只读一次同名环境变量（0/1）。若要求 shared，在原 NVN Pool 注册前确认唯一翻译 worker、外部 host 内存扩展与完整 Buffer 用途，用自有对齐小页实际做 CPU→Flush→GPU Copy 和 GPU Fill→HOST barrier/fence→Invalidate→CPU 字节检查。探测不成立时整次运行固定 legacy；成功则整次运行固定 shared，不再按分配动态切换。后续非法地址、OOM 或导入失败明确报错，不在线搬回镜像。应用层“失败后下次启动 legacy”的持久化入口尚未接入。

PoolInitialize 只登记原 Pool 借用生命周期与边界，不调用 MEM_IMPORT、不分配镜像或 shadow，也不登记旧 dirty range。首个原线性 Buffer 取址才以完整 Pool 创建外部 VkBuffer、导入、map 一次；用途包含原后端的 transfer/UBO/VBO/IBO/SSBO/texel/indirect。保留原 GPU 使用位图与引用。读写都落在同一宿主分配，不用旧 dirty visitor、memcmp 或 pool 上传。非 coherent 提交前仍按实际 GPU 访问页范围执行 Flush（包括写目的页，避免残留 CPU 脏 cacheline）；写回范围记录原生产者→HOST 依赖，真实 fence 完成后先 Invalidate，然后原 CPU 事件/回调/等待者才可观察完成。原 pending CPU-write 元数据同时涵盖共享 GPU 写，CPU demand/常量证明不得忽略尚未发布的写入。coherent 跳过 cache API。

MemoryPoolFlushMappedRange / InvalidateMappedRange 在原 FIFO owner 上执行，校验范围，拒绝和在途 GPU 访问页重叠的显式缓存操作，不隐藏新的 GPU 等待。这项拒绝使用原页粒度访问位图，比最小 cacheline 更保守；不能说支持任意原子域并发。Buffer 对象 Flush/Invalidate 仍不是本项目已实现接口，不能根据名字表推断支持。

Finalize 在原 gfx gate 中向同一 owner 排队，已有提交先执行，再沿原 finish/fence/引用释放导入对象；调用完成前不得复用原 storage。GPU/CPU effect 完成前不 unmap/unpin，不改系统驱动或全部 malloc。首次取址会使 kbase import 全输入 range 先 fault、MAP_IMPORT 再 pin/map；不能把登记阶段说成已经长期 pin，也不能把地址容量说成实测驻留。尚无大 Pool Mali 实际资源成本结论。

## packaged image 是当前硬阻断

Metal 同样依赖 nvn_dirty 判断 packaged image 需要重传。共享 Pool 禁用 mprotect 后，既有负证明不能直接沿用；仅凭当前 no-op Flush 或入口名表，也不能证明游戏会为每次 CPU image 改写显式发布。未给这个未知点编造“永远干净”，也没有把每 draw 全纹理重转当解决方案。当前 shared 第一次真正上传/使用 packaged source 时明确终止，原 legacy 图像缓存保持原实现。由此，本树 **不是完整 global shared 游戏后端**，不能默认启用或部署原游戏。

继续工作须先证明一个可执行的新鲜度合同：原 NVN explicit Flush/texture invalidation 足以覆盖真实 CPU 改写，或可保留原 dirty 保护且 driver 确保其映射别名长期正确。此前真实 Mali stale 反例保留；没有 PFN/确切 COW 因果结论，不重做被拒绝的 guest-AS 诊断、不解除保护重试 GUP。

## 自有功能验收

`DM_FIXED_SHARED_BUFFER_FIXTURE=1` 仅宿主/原生功能 runner 入口。检查 metadata-only、无 shadow、启动后改环境不改变模式；执行原 NVN 不对齐 Copy、录制时 Update 快照、CPU 改写后提交、四轮 SSBO GPU 写回；gated GPU 尚未完成时并发 Finalize 必须等到原 fence/发布后才返回，再验证同地址新 Pool 未继承旧 handle。最后复用原后端 CPU-demand/query/event byte oracle，不篡改提交计数。

两个独立负例保留原 fatal：`DM_FIXED_SHARED_CACHELINE_NEGATIVE=1` 在原 timeline gate 阻塞的 GPU 访问上调用邻近 word 的显式 Flush；`DM_FIXED_SHARED_IMAGE_NEGATIVE=1` 真正获取 packaged image，确认未走旧 clean proof 或每 draw 重传。严格 capability-deny 注入只缩小能力，验证启动 legacy，不能当真实设备“不支持”的证据。

宿主实际 memory type 是 coherent 0xf；这不验证新的非 coherent Pool 路径。旧定制驱动 bdd9 的 Mali 8KiB 标准双向与64KiB只读 NVN 成功仍保留，不代替此模式的 Android 结果。尚无 GPU 带宽、RSS、帧率或游戏零拷贝收益声明。此树未集成原主线没有的 PanVK app-local loader，也没有 APK 部署入口；后续 Android 验证需单独组合已审 loader 与独立 appUID fixture，不能冒充现成可玩包。
