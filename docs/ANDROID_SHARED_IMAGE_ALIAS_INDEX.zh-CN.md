# 共享池图像别名检查的精确范围索引

Q735、Turnip 3c871、2f0 原 ARM64 游戏共享模式的官方 Simpleperf
99 Hz 用户态采样中，577 个 NvN 样本有 248 个落在
`vk_texture_shared_linear_access/conflict`，占 42.98%。精确 ELF/LLVM
映射表明这是 Buffer 访问时遍历 GPU 图像别名链，不是图像内容扫描。
这些是该共享实验的用户态比例，不能外推到未开启共享的系统驱动版本、
整帧耗时、GPU 带宽或 FPS。内核成本没有被这次事件采到。

热查询改用确定性 AVL：键为 guest start、serial、owner 指针，节点维护
子树最大 end。仍在候选节点读取当前 initialized、共享模式/ASTC 编码、
非空来源与长度。共享模式在查询入口读取一次。零长度、请求地址溢出以及
原无符号区间 end 语义保留；没有概率结构、最后查询缓存或预测证明。

原链表保留给冷 ASTC 冲突检查和错误诊断。首次 GPU writer 同时挂链、
插入树；原 CPU 重建、逻辑退役、最终销毁同时撤销二者。2f0 已验证的
“翻译经过退休点且 GPU 完成后撤销 guest 字节权威，物理缓存再保留八块”
规则不变。promotion 留下的旧物理副本清除别名成员状态，不复制第二个 owner。

每个 image 新增 64 字节元数据和一个进程级根指针，无额外图像内容副本。
冷插入、删除增加 AVL 平衡与最大 end 维护；原冷链表删除仍保留。
查询利用地址和 max_end 剪枝，几何重叠很多且候选都不合格时仍须逐项检查。
这不允许删掉现有图像别名错误保护，也不改变任何同步和 authority 合同。

验证包含同一生产算法与旧全链 oracle 的 24,000 次随机增删及动态 eligibility、
重复地址/serial、排序/逆序、嵌套、邻接、零长度和 end 溢出，178,106,921
次检查通过，ASan/UBSan 无错误。原 NVN GPU 退休地址复用、未完成 GPU、
未翻译退休点、CPU 新写、retile、promotion 和三项原 FATAL 负例通过。
完整 LVP 官方 VVL+SYNC 的 async enabled 与 serial reference 分别通过
22,560,821 和 22,196,695 次检查；实际 host 内存为 coherent 0xf，不能代替
Android 或非 coherent 验证。实际新 APK/原游戏和官方工具对照另行记录。
