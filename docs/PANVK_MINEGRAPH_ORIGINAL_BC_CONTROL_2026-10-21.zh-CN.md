# 完整 PanVK fork 的原 BC 资源语义基线

前一次静态 unsupported-HAL 驱动已在原022真实启动后通过实际 PanVK 物理设备查询，随后在 ASTC4→BC3 的 T2T 转换前被原后端拒绝。新驱动对 BC 的真实采样支持使目标仍为 BC；原 stock Mali 走 decodedRGBA 目标。已有 legacy ASTC copy 开关不改变创建时选择，且仅能接管 decodedRGBA 目的，单独启用不能解决此路径。这个结果不证明 GPU DeviceLost，也不能靠虚报格式能力解决。

本实验使用相同022 APK/原始模块、相同703db静态HAL驱动，固定 .60、2772×1240，shared/legacy copy OFF，只切到有来源的原 BC CASC 覆盖层。目的确认完整 fork 的真实绘制路径是否可运行；不作为 ASTC 性能对照或共享成功。

`prepare-panvk-original-bc-control.py` 复用原 casc_overlay/touch_layout 工具及先前固定的纯 UI/BC 证明，重新加载原始3.3.94012 Update RomFS、原 BC/UI 覆盖层与当前 Mali full4 主机资源：

- 原132412路径仍引用相同文件表位置，788新增路径来自既有上游UI；BC 与 full4 的整个文件表相同。
- 原31093项 texture/cache CFT记录在原位置保持完整原字节；描述缓存完整SHA及所有原格式数量匹配，不重新编码纹理。
- 其余102107项CFT，包括所有既有UI与触控资源，与当前full4逐字节相同；ctrl_touch与strings也与生产SHA完全相同。

候选只复制四个小覆盖文件：ctrl_touch、touch_strings.tsv、data/.build.config、data/00-00000007.data。所有原 BC 纹理 payload 仍引用原 RomFS 档案，不复制/链接整套 RomFS。未引用的 ASTC archive8及tool-only pack.json不进入候选目录；另存独立 composition manifest，不伪装生产验收。

工具增加 overlay 为第四个 stopped-app CAS 目录，连同 driver/temp/cache 全目录备份恢复，原最新PSO和资产不覆盖。传输为四个确定SHA、仅普通文件的有界tar，入站与发布后逐SHA/owner验证。已有真实函数联合测试扩展到四目录：80组部分发布/幂等恢复、16组完整 backup→publish→restore→cleanup，包含空格路径及实际四文件overlay；坏SHA/重复条目/ownership仍拒绝。默认离线，Root审新Ready后才允许一轮实际启动；结束依原生命周期fresh save2停止，已退出不重启。
