# 借用大 Pool 的只读子窗口实验

从真机已验证的 `603e3e6`（核心 `687b156`）另开分支，默认关闭、未合主线。沿用原 NVN Pool 的存储有效期，不要求知道 malloc 来源，不拦截 allocator，也不转换所有 Pool。此版没有手机部署或真实游戏验收。

## 选择与表示

原 `MemoryPoolInitialize` 登记完整 Pool dirty lifetime lease。仅已知 `CPU_CACHED|GPU_CACHED`、完整页、起始64KiB对齐、专用异步翻译 owner 可发凭证。Pool 可以大于64KiB，尾部可以只有一个完整页。最终 import 仍检查驱动 host-pointer alignment/实际 memory type/完整 allocation requirement；不支持则沿用镜像。

仅原 `pool_operation_pin()` 的 span miss 使用原完整 operation/ghost requirement 列表及原 `pool_window_bounds()` 选择一个 canonical window。保留 prefix 与完整 binding，禁止截短、拼跨节点 descriptor。每 Pool 最多8个同时导入窗口，单窗口最多16MiB；现有镜像节点不替换，不因热绑定重新查全 registry。原生产 window 默认16MiB；本功能夹具显式64KiB，这两个不能混为一谈。

每个导入 node 保存独立排除凭证与 VkBuffer/memory。非 coherent mapped memory 在原提交前，只对实际读取页按该 VkDeviceMemory 的本地 offset/atom Flush。多个 imported node 与普通 mirror node 混合时，原 used-run 按物理节点分段；原资源依赖和 GPU 完成仍保留。没有 GPU 写共享、全局 HOST barrier、每次 Flush 重 pin 或新计时器。

## 页与证明

borrowed 构建为每个 dirty range 预留8个排除 slot（非 borrowed 构建仍一个）。只有 `PG_NONE` 完整页可以逐页 CAS 成排除状态；PROT/BUSY/重叠/越界/slot耗尽立即失败，只回滚本次前缀，不解保护 GUP 重试。租约的完整 Pool base/size/slot/lifetime 在同一原 mutex 下复核，排除 token 则只覆盖导入窗口。

旧全局和无地址 range proof 仍在存在排除时保守返回未知。固定查询 scoped proof 的 lifetime、bounds、SC exclusion version 全相同时直接 O(1) 刷新；版本改变后才对最多8个 slot 做有界不重叠检查。没有改用一个覆盖所有窗口的大 envelope，因此同 Pool 中间未导入的纹理页仍可得到独立 clean proof。ordinary dirty-since/visit 的慢路径要做有界 slot snapshot/交叠检查，不能称其无成本。

## 历史与退回闭包

HLE 原 image/texel/event/command memory/CPU-effect/mapping 入口在新语义使用前登记页范围。每 Pool 懒分配最多64项历史区间；失败和退休窗口也记历史，不清除；超限或 OOM 保守禁用全池新导入。只封锁交叠 canonical window，不封锁无关窗口。完整 operation plan 的 unknown/writer/index/copy/non-UBO-or-VBO 使用也先记范围。

已有导入的 writer、image、CPU-effect/映射变化，在原 owner 外层入口退回交叠 node。先分配替代镜像，再原 commit/wait/finish/effects/reset/reference drain；复制 live CPU 到该镜像与 shadow，重置该范围上传/初始化证明，替换 handle/generation，销毁/unpin import，最后 release 页排除。其他 node 的 handle/generation 和普通页证明保留。**当前 pool refs 粒度仍要求整个原队列 quiescent；逐范围物理替换不表示此等待已经逐资源化。**

跨 node binding、whole-reference 切换、原 backing consolidation 先退回所有相关 import，再走原迁移。Finalize 先在 registry mutex 下 mark closing，原 FIFO owner 无条件关闭所有 import，再 dirty remove/Pool del；新 alias/attach 与 closing 使用同一锁，保留旧 closing/ABA 协议。

新夹具发现并保留一个真实遗漏：HLE `ReadTexels` 的目的地址曝光原先只受 defer-publication 模式控制，borrowed 单独开启时会绕过，导致实际 `Borrowed CPU effect missed outer retirement`。此版只在显式 borrowed 实验中也走原曝光/owner readback 分支；不全局开启 defer。原 FATAL 断言没删，修后原回读目的范围确实退回，其他窗口保持导入。

## 可复现功能范围与成本

新增原 NVN 2MiB Pool/64KiB 窗口 GPU 夹具：多个 UBO/VBO 窗口并存，本地4KiB/128字节偏移，合法 CPU 改写保持同句柄，真实顶点 full/clipped/full，独立 gap 纹理的 renderer cache hit 和修改后 GPU 字节，初始部分 PROT 的单窗口回退，历史/晚 image、实际 UpdateUniformBuffer、ReadTexels CPU-effect、跨窗 binding 整合、Finalize attach race/ABA，以及4KiB尾窗。它是宿主官方 LVP coherent0xf + VVL/sync 验证，不是 Mali0xb 实测。

新的 dirty 测试覆盖8 slot/容量、重叠与越界、独立释放和旧 token、同 Pool gap 真实 CPU 改写，以及已有7个 exclusion 时第8次遇到真实 PG_BUSY 的前缀回滚。旧 owned/whole-borrowed/async拒绝/worker创建失败/完整 GPU suite 保留。

GDB 读取当前宿主 DWARF 得到：range280B（旧104B，borrowed构建全4096槽额外704KiB）；非borrowed range112B（额外32KiB）。Pool328B；physical node96B；每导入 certificate48B；每有历史 Pool 懒分配1040B。shadow仍是原 Pool 大小的虚拟 calloc 保留，不声称全部双份内存消失。热读取也有显式实验 branch、scoped metadata 和活动 node 分段成本；退休另有分配/等待/镜像复制成本。

## 尚不能外推

没有真实游戏各 ptr/window 的完整历史证据，不能断言现游戏有多少可导入窗口或净收益。未知外部 VMA/protection owner、非 NVN 重映射、并发修改 GPU 正读的同一 byte 的快照语义、GPU 写共享，以及整个 PanVK 游戏运行/功耗/FPS，均未在本版验收。当前 NVN 合法 storage lifetime/原 mapping hooks 是前提，不是任意 host-pointer 可永久 pin 的证明。旧 mprotect 导致 stale bytes 的失败及 whole64KiB Mali 成功记录都保留；本版不能把前者写成驱动已普遍修复。

## 独立审查修订

`750bbbe` 冻结独审找到两项阻断，本树另分支修正，旧树和旧报告不变：

- full-reference 原回退以 `base,1` 退整个 Pool，子窗可能都从非零偏移开始而无交叠，原有界循环4096次后报错。新增**原 NVN 多非零窗口 + 原 full-reference 切换**夹具，只加测试的 `4a9689a` 实际复现 `Vulkan whole-reference pool registry did not settle`（退出134）；修订在同一 registry snapshot 下携带完整 Pool size 给原退回函数，修后 GPU 像素/两次退休/whole backing 验证通过。不是延长重试或删断言。
- `sync_page` 运行在翻译 owner，但原 guard 读取 `p->borrowed_windows`；其他 HLE 线程可在首次不相交 image/CPU-effect 声明时懒分配该历史指针。改为使用 owner 持有的原物理 span/import 标志作深层拒绝检查，不读该 lazy 指针、不增加全局锁或原子。其余 overlap caller 经逐点检查受 `V.resources` 或同一 owner 保护；不能笼统把 retained pool ref 说成 next 链不可变。此项是源代码并发闭包修正，没有声称 TSan 真机动态验收。

这些修订不改变8-slot预算、实际0xf宿主/尚未0xb子窗真机的边界，也不提供全应用的全局 pinned-byte预算；后者仍是生产启用前需要明确的资源策略。
