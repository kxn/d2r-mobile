# 共享 Pool 的原生图像复制与 canonical NVN backing

隔离候选，基于 c075d41；默认共享模式仍关闭。c075 真机再次失败的实际栈落在 `vk_texture_copy_t2t -> image_upload(source)`，scope=1/stamp有效，因此不能继续归因于未登记。原始日志没有此前 writer 身份，不能据 ASTC 格式断言来源为 CPU。

B2T/T2T 原 GPU copy 保留。仅目的图像在复制前已有完整 CPU canonical 内容、没有外部原始句柄逃逸、没有 CPU BC 解码、且原生同格式二维 mip/layer 块几何与局部 storage descriptor 范围可验证时，同时维护原 NVN 排布：B2T 在 GPU 命令位置把实际 buffer 源 gather 到私有 GPU buffer；T2T 从真实源 image ROI gather 原生块；GPU compute 使用原 `nvn_bl_offset` 的 GOB 地址公式将这些相同块写入原导入 Pool。源可以是 GPU 生成，完全不依赖 CPU-known 猜测。私有 gather 防止原始存储别名破坏源；原 B2T 读与随后的 retile 写之间也有真实依赖。

只对实际写入的最多16B连续片段登记 writer/writeback，沿原 shared fence/Invalidate/完成发布，不对整幅图或其他 mip 作 Invalidate。GPU buffer 的 page/atom 依赖仍保守；跨 cache atom 的合法性仍服从原非一致内存同步契约。没有 CPU readback、Pool shadow、逐 draw snapshot，也没有改变游戏资源或玩法。

996ebd4 保留为不确认自身发布的正确性基础；最终组合采用独立审查的41bbda9 dirty-engine token。begin先捕获全局changes再检查原stamp无dirty，并要求目标scope所有页PG_PROT；每次实际GPU marker唯一地累积自身events和max generation，finish仅在全局差精确等于own events、页仍PROT、scope生命期无变化/溢出时确认captured ownMax。CPU写、延迟reserved N/PG_BUSY、其他GPU/alias插入均保留旧stamp；绝不取 `nvn_dirty_now()`。caller使用counted marker + pretracked writer history，不双标记。干净 GPU 消费不要求 CPU publication 已完成；只有实际 CPU conversion 才核对原外层 preflight 的完成可见性。

真实owned GPU oracle验证：GPU Fill→B2T→T2T（publication仍pending）保持同一recording serial且uploads/reuploads不增；CPU optional读取实际retile ROI仍拒绝pending。用既有Volk dispatch包装边界，在token begin后、own marker前实际写入另一mip，token拒绝确认，后继真实GPU读到了CPU修改；没有生产计时/监控hook。另保留same-value CPU写和原GPU未知/跨identity拒绝负例。

共享模式的 `vk_buffer_writeback_known` 还跳过原legacy CPU snapshot malloc/memcpy，因为shared WB根本不消费该known指针，只发布原GPU内存。原OP_UPDATE_UB GPU source temporary/录制拷贝生命周期未改。owned vkCmdUpdateBuffer + known-writeback观察snapshot计数不增、随后改掉调用方栈源，原GPU与guest64B仍精确；该fixture不冒充直接执行了OP_UPDATE_UB。

不满足完整 canonical 前提的 partial copy、格式转换 blit、decoded、linear/3D/非4字节 native块等仍保留 GPU authority 与原拒绝。完全 GPU 定义的未知图不能因局部 retile 获得整图 CPU proof。Finalize 与物理8提交保留期的保守拒绝未扩展修复。

新增成本：每个 eligible copy 的私有 GPU gather buffer、额外 GPU copy、compute retile、局部 descriptor、逐实际片段的 CPU writer/WB登记。GOB物理序遍历只合并真正连续片段，不跨洞；没有临时区间数组/排序/新持久cache。实际 Vulkan image 数据复制不删除。当前 owned Lavapipe 验证了 RGBA8 与原生BC3(4x4/16B)；本机不支持 native ASTC，fixture明确输出 unavailable，不能把 BC3 验证冒充 Mali ASTC/game 验收。真实设备带宽、驻留、等待与热成本尚待原厂工具/实际游戏验证。
