# Operation-owned sampled image source handoff

此候选针对 actual716 的 Android 官方 CPU 记录所见重复逻辑工作，不是新的物理 image cache：已有 physical recipe 初始命中约94%。actual716 whole NvN Running 为6416.676/235=27.305ms/Surface（同一 Perfetto crop）；该数字不能与非匹配历史窗口作净收益比较。`memmove` 17/636 用户态样本的调用来源不能全部归到本路径，不能据此预报毫秒或FPS。

仅 native 资源已激活、window 非零、当前 S、所有有关 stage 有原完整 Pool schema 证明且仅单个 sampled kind3/6 的 operation 使用新路径。ordinary/nativeOFF、storage/复杂 binding、saved-state、深度溢出及分配失败继续原实现。原200B logical cache 条目不变；每 shader 冷 metadata 增4B（实际结构760→768B，含padding）。

CPU source 原必需的 image iterator 单遍直接产出每项168B operation-owned record（含160B source value/144B tex_t及寄存器）。CPU source调用原 preflight；所有RT preflight完毕后才发布完整plan，native admission直接消费同一份tex_t，免去第二次image iterator/kind/reg解码、logical registry lookup及144B副本。在每次消费者入口读取实际S handle、检查descriptor epoch；变化时原lookup重建值。plan不携带 ready、dirty、物理对象、hazard或GPU完成证明。原 collector/native pin、prepare、alias/promotion、hazard、last_use、descriptor阶段保持原路径。

新增工作是 stage eligibility/count/收尾校验、每record写reg/id、consumer epoch/ID比较；一次CPU source decode/copy变为持续到admission的owner值，未删除原CPU source工作。若发生注册/handle变化仍重解码，可能无节省；native已开启但实际没有native条目的普通sampled绑定仍有record维护成本。只宣称正常未变情况下减少重复工作，不宣称实测CPU或FPS已改善。

8层有界owner scope；每层至多640项，按实际count懒分配、保留容量、不按draw分配。生产plan128B，全部最大861184B（约841KiB）；nested/ghost有独立slot，root退出清valid，overflow原路径。无Vk资源引用、额外GPU等待或完成回调。分配失败当代保留原路径且不每draw重试增长。

最终host验证：Core/SYNC Lavapipe与ASan/UBSan共8正例，覆盖原graphics/compute/promotion/ghost、CPU同值/无Flush、原native coherent RGBA FS/CS/transfer与Image→Buffer真实GPU字节；3个变异负例分别去epoch、冻结handle、nested覆盖parent，必须由原oracle拒绝。另复用已真机烘焙f830的1216B表+94208B opaque payload，经原NVN Builder/CPU source/collector得到非空native range；这一项是metadata接入证据，不称Lavapipe执行了ASTC或f830。未执行真实游戏、未构建Android、未访问设备。

证据：`.local/logs/operation-image-source-flow-v1-final-tests-v3/results.json`、`operation-image-source-flow-v1-negative-tests-v3/results.json`；之前f5/165版本、原shared模式fixture同样失败的716对照及初始失败记录均保留。
