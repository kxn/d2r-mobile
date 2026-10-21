# 操作内图像访问 phase artifact（宿主候选）

实际依据是8480原生游戏 Q735 官方 Simpleperf：605个NvN用户态样本中，descriptor资源消费/构造140（23.14%）、operation admission57（9.42%）、CPU source30（4.96%）、image prepare14（2.31%）、postDraw image history26（4.30%）。按每条实际栈去重，本窗五组无重叠，合267/605；其中12个资源样本缺draw祖先，不能补栈或把全部267当作可省工作。高层账在 `.local/logs/actual8480-wholepath-structural-review-v1/result.json`，原官方数据/ELF来源均由该报告引用。

这次只改已有实际图像请求 packet 的后置消费，不新增另一套逻辑/物理资源缓存。`image_request_add_normalized` 已经从真实consumer拿到 owner、经过规范化的range、stage、access；后置 `vk_texture_shader_record` 原先仍对这份packet扫三遍：全N找writer清旧历史，全N记录reader，再全N记录writer。

生产者现在随追加请求记录 `writer_begin` 和 `writer_suffix`：首次writer的位置，以及之后是否出现了reader。普通sampled shader先追加只读资源、随后追加附件writer时，形成已证明的readonly前缀/writer后缀。消费者直接清writer后缀、记录reader前缀、记录writer后缀，扫描3N降为N+W。每个真正 `image_record_normalized` 的调用、参数及顺序，与原完整算法完全相同；没有把多个stage/range合并，也没有略过真实读写或清历史。

若storage/read、只读附件等交错使证明不成立，走原三遍过滤算法。全readonly批为N次扫描；全writer批为2N。附加成本是追加时一次已有access位分类、少量phase控制分支，以及每个batch16B，8层共128B；原40B/request、动态capacity、每resource快照/布局不变。混合fallback也付出追加分类，不能称对所有批都更快。

这份证明只在本operation的请求顺序中成立，不跨draw保存resource ready。begin重新初始化，abandon关闭原scope，nested使用原独立8层batch。promotion只重定向owner，不改变请求ordinal和access，phase证明仍适用。原CPU脏页/发布、native aliases、promotions前置、每consumer hazard/布局、GPU last_use、独立Pool pins和各recording ledger全不变。

选择此方案是为了让已有consumer结果承担下一phase的分类，而非再加小key/hash缓存。它不免除shader descriptor agent负责的真实资源消费，也不声称消掉postDraw整个4.30%：其中还有必须执行的native/每mip/layer历史动作。尤其不能用局部比例预测10ms或FPS。

宿主资格调用原C NVN/Vulkan后端，用Lavapipe软件GPU，不运行真实游戏模块。有限历史oracle覆盖6个请求的全部64种读写排列×4种owner/mip/layer拓扑；28种前缀/后缀命中、228种交错fallback，逐字节比较实际访问历史、proof、generation、clock与旧完整算法，并核nested/abandon。GPU部分复用原production-policy graphics/compute/UBO/SSBO/VBO/index/ghost、recipe嵌套回放/格式promotion，以及四种IMAGE hint/sRGB组合的array/mip/storage与后续复制像素。normal、ASan/UBSan均保留Core/SYNC validation。

首次focused入口复用较晚的image_dependency_test时，前一个用例遗留scissor为enabled、8/0/8/16，导致最大8×8的清屏被完全裁掉（像素alpha0）。独立仅执行原三遍算法的reference也同点失败。新入口用原NVN SetScissor(0,0,16,16)显式建立该用例在完整main中的原有前提；原所有expected像素不变。修正前的失败、reference及实际scissor日志保留。首次host编译把void Finalize包入CHECK的测试编排错误也保留，未更改生产逻辑。

本分支没有合入另一独立bounds proof fusion小修，亦未Android构建、部署或采集；Mali/PanVK暂停，所有设备由root管理。
