# 共享 Pool 的原生图像复制与 canonical NVN backing

隔离候选，基于 c075d41；默认共享模式仍关闭。c075 真机再次失败的实际栈落在 `vk_texture_copy_t2t -> image_upload(source)`，scope=1/stamp有效，因此不能继续归因于未登记。原始日志没有此前 writer 身份，不能据 ASTC 格式断言来源为 CPU。

B2T/T2T 原 GPU copy 保留。仅目的图像在复制前已有完整 CPU canonical 内容、没有外部原始句柄逃逸、没有 CPU BC 解码、且原生同格式二维 mip/layer 块几何与局部 storage descriptor 范围可验证时，同时维护原 NVN 排布：B2T 在 GPU 命令位置把实际 buffer 源 gather 到私有 GPU buffer；T2T 从真实源 image ROI gather 原生块；GPU compute 使用原 `nvn_bl_offset` 的 GOB 地址公式将这些相同块写入原导入 Pool。源可以是 GPU 生成，完全不依赖 CPU-known 猜测。私有 gather 防止原始存储别名破坏源；原 B2T 读与随后的 retile 写之间也有真实依赖。

只对实际写入的最多16B连续片段登记 writer/writeback，沿原 shared fence/Invalidate/完成发布，不对整幅图或其他 mip 作 Invalidate。GPU buffer 的 page/atom 依赖仍保守；跨 cache atom 的合法性仍服从原非一致内存同步契约。没有 CPU readback、Pool shadow、逐 draw snapshot，也没有改变游戏资源或玩法。

当前基础版本不确认自身 dirty generation。它保留原 dirty/外层等待与必要重传，优先证明数据一致性；因此不是最终无等待优化。随后精确自身发布 token 必须拒绝夹入的 CPU 写、未保护页、延迟发布、生命周期变化与溢出，成功只能确认自己的 generation，不能取 `nvn_dirty_now()` 吞掉其他写。干净 GPU 消费不要求 CPU publication 已完成；只有实际 CPU conversion 才核对原外层 preflight 的完成可见性。

不满足完整 canonical 前提的 partial copy、格式转换 blit、decoded、linear/3D/非4字节 native块等仍保留 GPU authority 与原拒绝。完全 GPU 定义的未知图不能因局部 retile 获得整图 CPU proof。Finalize 与物理8提交保留期的保守拒绝未扩展修复。

新增成本：每个 eligible copy 的私有 GPU gather buffer、额外 GPU copy、compute retile、局部 descriptor、逐实际片段的 CPU writer/WB登记。实际 Vulkan image 数据复制不删除。当前 owned Lavapipe 验证了 RGBA8 与原生BC3(4x4/16B)；本机不支持 native ASTC，fixture明确输出 unavailable，不能把 BC3 验证冒充 Mali ASTC/game 验收。真实设备带宽、驻留、等待与热成本尚待原厂工具/实际游戏验证。
