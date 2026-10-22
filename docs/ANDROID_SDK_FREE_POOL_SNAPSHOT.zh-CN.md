# 同一SDK停点的NVN Pool归属快照

此工具只补充内存归属证据，不修改游戏、驱动、分配器或页面。复用已验证的官方LLDB SDK停点流程；设备、原APK/NSO资格、服务端、转发、存档和恢复仍由原owned事务管理。

## 来源与入口

- 基底：`f75de421d0558c746ffc9cd4fee2e1a66c5e9c40`，生产运行时文件完全不改。
- `scripts/dev/sdk_free_resident.py`继承已真实成功的忽略目录v3钩子，原SHA为`ec94bac5e5b978b294b8699cce723d9f954f8e4802f2c3ce7f5f5f6348e0418b`。
- 原入口和env不变：`command script import /绝对路径/sdk_free_resident.py`，随后`script sdk_free_resident.snapshot(lldb.debugger)`。
- 新可选伴随模块：`scripts/dev/sdk_nvn_pool_registry.py`。不使用它时不读取NVN注册表，保留原SDK分区/pagemap流程。

原config增加且仅增加可选`nvn_pools`对象；原runner/gate不在本提交修改。调用方冻结源时必须同时复制和pin这两个Python文件，伴随模块实际SHA须进入config，不只pin入口文件。示例（MODULE_SHA需从本提交实际文件计算）：

```json
{
  "nvn_pools": {
    "enabled": true,
    "mode": "f75-bionic-normal-resources-zero-v1",
    "ELF_sha256": "4a8420209e2ac6c71c161626472458ac498ee844bdce58677c8e2769b5ec4618",
    "ELF_BuildID": "6e6f12c5c5f7a858704a84ee294b19d95aa3f894",
    "qualify_unlocked": true,
    "module": {
      "path": "/绝对冻结源路径/sdk_nvn_pool_registry.py",
      "sha256": "MODULE_SHA"
    }
  }
}
```

此模式仅资格化实际审过的f75正式ELF/源码，不把其他同字段布局的运行时自动视为合格。原顶层APK、ELF、canonical、SDK输入和ABI资格仍必须匹配，不绕过原事务。

## 为什么不能仅all-stop后遍历

SDK `GetMemStats_`停点持有的是SDK分配器中央锁，不是Vulkan资源锁。f75 `pool_add_metadata`在`V.resources`内先发布`pools`头，再更新epoch/identity/counter；`mtl_pool_del`在同锁内摘链、等待引用并销毁。窗口发布、migration、shared union、lazy canonical/covers发布也都由`V.resources`排他保护。任意all-stop可能停在这些中间步骤。

因此本模式只在实际DWARF所指的`V.resources`为完整40字节零初始化表示时才遍历；非零、读失败、未知类型或尺寸均不猜测“未加锁”。f75明确使用`PTHREAD_MUTEX_INITIALIZER`，没有对该锁重新设置属性。AOSP Bionic LP64 normal/private mutex的首个atomic uint16状态0表示unlocked、1/2表示owned；本工具要求全部40字节为零，比仅检查状态位更保守，不尝试解码owner/递归/PI状态，不调用trylock或修改futex。

一手依据：

- [AOSP Bionic pthread_mutex.cpp](https://android.googlesource.com/platform/bionic/+/refs/heads/android14-release/libc/bionic/pthread_mutex.cpp)：normal state0/1/2、LP64布局、正常锁原子获取/释放。
- 实际NDK `bits/pthread_types.h`和f75正式ELF DWARF：`pthread_mutex_t`40字节，Pool448字节，Buffer node152字节，vk_buffer56字节。代码按实际DWARF字段取值，不把这些数当地址偏移进行猜读。

`qualify_unlocked=true`只扩展原自然SDK断点的资格：先匹配maincentral/x0和SDK返回地址，再只读40字节锁表示。非零交官方LLDB自动继续，在原最长60秒natural wait内等首个合格事件；不取多份snapshot，不主动调用SDK函数。记录实际考察/拒绝次数、最后拒绝表示与成功表示。读失败沿原callback_error路径停止并脱离。未设置时不执行此额外过滤；首停点锁忙将给出NVN unavailable。

## 采集内容、边界和失败语义

在同一个已合格的SDK all-stop、原pagemap/SDK分区读取之后：

1. 全40字节资源锁为零；记录实际命名全局的DWARF declaration、地址、类型。
2. 读取`pools`、`pool_registry_epoch`、`pool_adds`；完整遍历当前注册链，最多64个Pool、合计4096个Buffer node。
3. 每个Pool记录guest/size/identity、已发布backing generation、共享/原NVN flags、canonical backings和shared_covers；只保存这些宿主元数据结构原字节，不读取Pool内容。
4. 使用官方`SBData`和实际DWARF从已读原字节生成本地typed SBValue，检查正identity/无重复/无环、逻辑范围非绕回/无交叠、next链完整、Buffer范围和size、已发布generation/handle/memory、共享Buffer的原guest origin。canonical链要求有序且不重叠；covers允许合法交叠，不能当canonical误判。
5. lazy Buffer导入的节点/覆盖节点/导入字节数与链一致；不把这些字节当物理独占RAM。`pool_registry_epoch`也因迁移/窗口变化推进，**不能**用`2*pool_adds-epoch`推断当前Pool数。
6. 再读head/epoch/adds和资源锁，必须未变且仍全零。
7. 可读texture_capacity/sampler_capacity及实际descriptor元素类型大小；不遍历descriptor数组。此附加标量失败单独标明，不冒充描述符内容或阻断已完整Pool结果。

新增元数据总量上限1MiB，原40MiB总原字节和60秒停点预算不变。Pool和Buffer结构读取仍用原Snapshot的受预算ReadMemory，每段最多64KiB；原finally detach不变。所有原元数据字节进入同一个raw_files哈希清单。registry失败的部分字节也保留。

原SDK成功与NVN归属结果分开：只有`nvn_pools.complete=true`且`status=SAME_SDK_STOP_NVN_POOL_REGISTRY_METADATA_COMPLETE`才证明本轮当前注册表已取全。锁忙、坏指针、预算/校验失败都返回`NVN...UNAVAILABLE`及非空errors，不猜零，也不为了取全而重启。SDK本身仍可以complete；若原SDK/全局stop预算失败，仍保持原整体失败语义。调用方不能把SDK成功写成Pool归属成功。

范围仅为**当前已注册**Pool和Buffer链：已从注册链摘除、但可能仍等待引用排空的Pool不在本表；原生VkImage allocation链也不遍历，只标记其存在。缺少注册归属不等于没有GPU保留引用。Pool logical范围、重叠Buffer import和PTE并集都不是物理独占字节、PSS或安全可回收量。后续只需在同run/同stop把这些guest范围与原SDK span/PTE相交；不可拼接旧PID快照。

## 离线验证

- 原22个SDK decoder正负用例继续通过。
- 新5个自然资格/失败隔离用例，涵盖非零拒绝、全零接受、读失败不当零、未启用时零额外锁读取、坏pin明确unavailable。
- 16个Pool结构正负用例覆盖未物化洞、合法overlap covers、锁中间态、链/范围/代数/计数/CPU origin错误。
- 官方LLDB在实际f75正式ELF上以`SBData.SetData`/`CreateValueFromData`核验Pool/Buffer字段、指针和nested vk_buffer解码，无设备或inferior。

以上是工具离线资格，不是新真机采集成功，也不报告任何新增内存收益。
