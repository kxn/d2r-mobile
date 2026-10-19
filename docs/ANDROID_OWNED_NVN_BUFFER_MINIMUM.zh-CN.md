# Owned NVN Buffer 最小接入（独立、编译默认关闭）

依赖 scoped API `1358455` 及 renderer 接入 `79721ea`。本阶段仅 `DM_VULKAN_OWNED_NVN_FIXTURE=ON` 编译的测试程序可以登记自身 fresh 64KiB Pool；普通构建没有 attach/retire/enable 符号或对应运行时字段。没有游戏启动选项、JNI 任意地址入口、malloc 拦截、固定 guest 地址操作或任何真实游戏导入。不能把这个自有 Pool 当成任意 NVN Pool 的 provenance 证明。

本实现明确小于之前设计的大 Pool 多节点方案：自有 UBO 使用独立完整 64KiB Pool，命令内存、RT 和读回使用其他 Pool。通过原 marker39 Shader、NVN 命令录制/翻译、BindUniformBuffer、DrawArrays 和像素检查验证。当前没有把同一个大 Pool 的普通纹理与一个 imported 子节点组合起来；普通纹理 scoped 负检查回归在上一层单独验证。

## 所有权与同步

1. attach 只接受完整、对齐、有明确自有来源的 fresh Pool，且没有 backing/shadow/初始化历史/在途引用；调用方须保证在登记前没有图像/别名/CPUeffect 暴露，不能按地址猜测来源。先以原 dirty API 预约可写 PG_NONE 页，再调用标准 `VK_EXT_external_memory_host`。PG_PROT/PG_BUSY 不解保护、不触发 GUP（此预约拒绝不存 certificate；fixture 不在后续 fault 后再次尝试，不能广称所有预约拒绝都永久 sticky）；真正 import 失败先销毁/unpin，再 release，逻辑池保持 sticky 拒绝。
2. 创建 Buffer 仅声明 Uniform、Vertex 和 TransferSrc（原 UBO 对齐只读复制需要 Src）。HOST_VISIBLE|HOST_CACHED 类型优先 coherent，非 coherent 保持全生命周期 map。map/bind/import 任一步失败不发布物理 backing。
3. 原 operation 现有 pool 解析中检查访问类型：只读 UBO/VBO可借；writer、SSBO/texel/index/indirect、显式 transfer 与 unknown 在任何物理 borrow 前产生维护请求。精确重复项合并 writable/stages，类型冲突变成 unknown。没有新增每 draw 注册表扫描。
4. 只读导入不建立 shadow 内容证明，也不运行原比较/上传；原 `sync_used_run` 对本次实际 GPU 读页逐次 atom 对齐 Flush，不用 dirty 代数省掉 Flush。coherent 类型依规范免 Flush；QueueSubmit 原隐式 HOST domain 操作仍在，没有增加全局 barrier 或 GPU wait。
5. HLE CPU exposure 在此编译实验下独立于 defer-publication 开关生效，覆盖 late packaged TextureInitialize、Finalize-before-dirty_remove 等原路径。event/query 的直接 owner 入口也先准备。深层镜像上传、overwrite borrow、writeback 和 completion destination 是 fail-closed 断言，不在内部偷偷退回/等待。
6. sticky retirement 先私下分配普通 mirror；随后在原外层 owner 边界结束/提交当前 CB，等待并回收真实 GPU/CPUeffect/CB 引用，再从权威 CPU bytes 重建 mirror+shadow，更新物理/registry/buffer 代数，free/unpin import 后 release 保护排除。原初始化 bitmap/stamp 保守重建。writer 才能继续，未来不重新 import。只有这次生命周期转换会等待；不是每个 CPU 写/每帧等待。

这不是完整生产闭包。独立程序保证 fresh 全池证书且只调用列出的原接口；跨子节点迁移、任意历史图像/raw handle、全局动态策略切换、生产 allocator 归属等仍待设计/验收。CPU 与 GPU 并发修改同一 byte 的旧 mirror 快照语义不承诺；CPU 合法修改必须遵守原 NVN 完成同步。本阶段不接 GPU 写共享，不用全局等待掩盖未分类路径。

## 实际证据与成本

官方 Lavapipe + Khronos VVL/synchronization validation + 原异步完成：2371 checks。前三轮 CPU prepare/直接写后的 UBO 像素正确，保持同一个 VkBuffer 和 backing 代数、一次 import；第四轮原 UpdateUniformBuffer 将 CPU 的 .375 更新为 GPU 的 .875，真实像素、CPU 和 shadow 三者都核实，随后原脏页 fault 恢复。另有 initially-PROT 在零次 import 下拒绝、无手工解保护/重试，以及 late 原 packaged image 在第一次使用前退回并读出真实像素。现有 memory 447763 与 renderer scoped 9301 checks 也通过。实际类型为 coherent 0xf，不能声称 Mali noncoherent 验收通过。

NDK 28.2.13676358/API29 完整 `native_vulkan_test` 编译为 AArch64 可执行文件；不是 APK、手机或游戏验收。独立默认 OFF 构建还核实导入选择/attach 符号不存在，并运行原 memory suite。

本实现最多可省本测试 64KiB VkBuffer mirror 的比较/上传；Pool shadow 仍分配，不能宣称双份内存全部消除。显式 ON 构建增加每 Pool 一个8B指针、每 vk_buffer 两个int、lazy certificate约48B和 owner pin 中的判定，及每次非 coherent 实际读取的 Flush；维护时新旧 Buffer 暂时共存。另有前层 scoped API 全局 metadata 成本及每 image +24B。普通 OFF 构建不包含本层字段/分支，但前层 API/renderer 源仍是独立实验，未经游戏收益验收不合主线。

最先两次失败已原样保留：测试在首次 mtl_init 前查询能力、以及所选旧素材目录缺 marker39 metadata。随后显式初始化并用原 make_vulkan_fixture.py 生成独立测试资源；没有删除或放宽任何像素断言。没有本阶段真机、FPS、带宽或散热收益数据。
