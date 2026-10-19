# 自有页保护排除 API：第一层实验

基线 `6fd5cf7892e97b4b3ec0d524a793b11b09f07cdd` 的 `nvn_dirty.c/.h`
与旧 e56 导入实验逐字节相同。本分支从该主线基线建立，没有带回旧 PanVK/NVN 导入、JNI 或驱动。

本层只提供显式自有页凭证及功能测试。没有产品调用、环境变量默认开关、游戏分配器接管、
固定 guest 地址操作或设备测试。Apple 返回不支持，避免让 Metal 原有 `nvn_dirty_now`
快路取得错误证明；Linux/Android 的 Vulkan 路径才属于本实验范围。

## API 与状态

`nvn_dirty_owned_exclude(address, size, &token)` 只接受调用方自己持有、已经登记且唯一覆盖的
整页可写内存，单次最多 16MiB。凭证必须初始全零。调用方负责证明原始分配寿命、无其他
VMA/remap/protection owner；API 不能从裸指针识别分配来源，不能拿来泛化任意 malloc/guest。

第一版每个 tracked range 只允许一个凭证，不支持重叠或独立双凭证。全程持原 slot mutex
防止登记/销毁竞态，逐页 CAS `PG_NONE → PG_OWNED`。与原 protect 的 `PG_NONE → PG_BUSY`
竞争；遇到 `PG_PROT/PG_BUSY` 立即失败，不等待该页、不解保护、不 GUP 重试。只回滚自己
已经取得的前缀，发布保守 dirty generation 后恢复 `PG_NONE`，返回的输出仍是全零。

`PG_OWNED` 页保持可写，原 protect 会跳过它；其正常 CPU 写不经过写保护异常。预约之前
便使相关证明不可用，确保不能在预约与未知 CPU 写之间获得新的 clean 证明：

* 该 range 的 `dirty_since` 永远返回 dirty，`dirty_visit` 访问整个请求范围。
* 该 range 的 `range_observe/range_refresh` 返回 unknown，包括此前取得的凭证。
* 任意排除活跃时，`nvn_dirty_change_epoch` 返回 0，关闭 Vulkan 的全局 epoch 快路。
* `nvn_dirty_now` 仍然是原上传时间戳；不把它伪装成已经观察到每次 CPU 改写。

此处故意保守：同池其他页的 range proof 和所有池的 global 快路也暂时失效。它是 correctness
接口原型，不声称当前粗粒度策略有性能收益。普通无凭证路径还新增少量原子读、range 表每项
增加 24 字节（4096 项约 96KiB）；未做 CPU 开销/帧率验收，不应因此直接合入或默认启用。

`nvn_dirty_owned_release(&token)` 不替调用方等待 GPU。调用方必须先证明所有 GPU/import
引用已完成并解除 pin，再撤销凭证。撤销先发布新的页 generation 和 change epoch，才恢复
`PG_NONE`；此后原 protect/写故障跟踪重新生效。成功清空 token；旧副本、修改过的凭证和
重复撤销返回失败。活凭证时销毁 range 或新增重叠登记会明确 abort，不能偷偷取消排除。

## 与导入的衔接边界

预期顺序是：原有 owned 分配及 tracked range → 预约可写页 → 标准 HOST_ALLOCATION
导入成功 → 有界 GPU 只读使用/原 Flush/原队列提交 → 完成并释放 import → 撤销凭证。
导入失败也必须先确认无 pin/队列引用，再撤销；已经受保护的页不走解保护再试。

**本层没有接入 GPU import 或 NVN pool admission。** API 不知道 VkBuffer、GPU fence、图像或
NVN 命令，因此不能自行保证 quiescent。也没有实现任意图像/别名/GPU writer/CPU effect
进入前的自动 demotion。这必须在既有 preborrow 层统一证明，不能在已借到 image/view 的深层
函数随意 wait/recycle。未来接入需要：

1. 所有 sampled/storage image、texture copy/feedback/format/usage promotion 在任何 image
   借用、upload 早退、cached proof 之前检查重叠并 quiescent demote。
2. 既有 writable pool operation/CPU completion destination 门禁同样先 demote，保留真实读取、
   GPU 写与原提交快照语义，不能仅凭排除 token 宣称 CPU shadow 已更新。
3. 退出时在无引用边界恢复 mirror/shadow 和 dirty，sticky 禁止该窗口再次导入；邻近非导入
   纹理继续使用原 dirty 跟踪，不能永久把整个 pool 当 dirty 导致每 draw 重传。
4. 非 coherent imported memory 仍按 atom 对齐 Flush；共享地址不能省略真实依赖与退休边界。

现有通用 dirty API无法证明这些 NVN 操作的完整闭包，所以第一层冻结在此，不制造“游戏共享
内存已经修好”的结论。此前 Mali mprotect 导入失败原文与断言保持不变。

## 实际验证

* 自有匿名页 unit：68 检查通过。包含不对齐、空/越界/溢出、重叠登记、双凭证、销毁冲突、
  修改/旧凭证、重复撤销、邻页原始写故障以及真实 protect 线程处于 `PG_BUSY` 时的中途失败
  与前缀完整回滚。预期 abort 仅在隔离自有页子进程，未读取或修改设备/guest 地址。
* 既有 Vulkan dirty-range oracle 扩展为 6467 检查，宿主 Lavapipe + 官方 VVL/sync、异步提交
  通过。新部分只在 owned 页排除期间验证保守证明及不重叠纹理，再显式 quiescent release
  后才把其字节用于原 image 上传/读回；没有把图像使用绕过未来的 demotion 要求。
* 原 memory-pool visibility：447763 检查，Lavapipe + 官方 VVL/sync、异步通过。
* 真实 Android NDK28.2 / API29 编译得到 AArch64 `nvn_dirty` 对象；不是完整 APK/设备验收。
* 早期文件名含 `vvl` 的初次宿主检查未启用层，只算无层检查；最终
  `*-official-vvl-sync.txt` 才是明确启用 Khronos validation/sync 的记录，不混用。

没有 PanVK 导入+排除组合实测、Mali 真机、完整 Android signal chain、游戏可玩性、FPS 或发热
收益结论。后续首先应在独立旧导入实验树里复用此接口做纯 owned GPU-readonly byte oracle，
保留原断言和 cache/fence 责任；再评审完整 NVN admission，设备由根代理单独协调。
