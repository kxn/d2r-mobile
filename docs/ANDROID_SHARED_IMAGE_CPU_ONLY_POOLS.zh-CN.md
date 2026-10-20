# 图像 CPU 转换不提前导入整个 Buffer 池（实验）

`7c60ff4` 的区间索引已消除大部分别名扫描开销。本候选继续处理共享路径的
驻留成本：旧的图像 CPU 读取准备会顺带把整个 NVN Pool 注册成 VkBuffer，
即使其后只转换并上传到独立 VkImage，也会触发整池注册。

不能直接删除这次注册。图像脏页保护随后可能把 guest 页设为只读，而 KGSL
后来的可写 USERADDR pin 需要可写 VMA。为重试而暂时放开 guest 写权限会
破坏原脏页合同。本候选在第一次发布 guest AS 前，把同一固定大小 memfd
映射为 guest 视图和内部 RW 导入视图。文件大小封印后关闭 fd，两视图仍
映射同一文件、相同偏移。guest 视图保留原保护；只有驱动注册使用 RW 视图。
额外映射占用虚拟地址，不是额外复制一份 Pool。

图像 CPU preflight／观察保留原 Pool 引用、脏页作用域、文件写入握手、实际
CPU effect 的提交完成等待及来源检查，不再为此创建 VkBuffer。真正的线性
GPU Buffer 访问仍导入整个相应 Pool；没有逐次切回旧镜像的模式。7c 的别名
保护、逻辑退役、物理八提交保留期保持不变。这个改动不让普通 malloc 全部
参与 GPU 注册。

冷启动的有限两页 Vulkan 检查复用相同 memfd 双映射构造，先把 guest 视图
设为 RO，再通过 RW 导入视图执行原 GPU copy／fill，从 guest 视图核对字节。
构造／能力失败在首次 AS 发布前固定使用原 legacy 模式；实际整池容量和
长期稳定性仍需真机检查。Android 拒绝无法取得已提交 RW 别名的 Pool，
即使该 APK 定义了 DM_VULKAN_TEST_EMBED，也不得重试原 guest 指针。

仍有明确边界：

- 大部分纹理继续执行原 NVN 排布转换、独立 VkImage 上传及脏页检查；这
  不是纹理直接共享，也不是消除所有 CPU／GPU 数据搬运。
- 旧 ASTC 传播证明要求已经物化的 Pool；image-only 未导入 Pool 仍保守
  回退，不能宣称此候选保留了这部分传播的全部性能收益。
- 额外 RW VMA 对 Android 内存统计／驻留的影响必须实际测量。解析 smaps
  时要识别同一 memfd 的两个视图，不能把两个虚拟 RSS 简单相加当物理复制。
- host 的真实 SVC、VM 与 GPU 回归不证明 Android KGSL 导入可用，不证明
  6GB 可玩，也不证明总内存、FPS 或发热已有改善。Android 真游戏尚待验收。

该候选仍为独立实验，没有替换已验证的可玩版本。
