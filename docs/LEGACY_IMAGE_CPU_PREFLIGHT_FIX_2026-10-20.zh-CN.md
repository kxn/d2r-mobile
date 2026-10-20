# Legacy ASTC：原绘制入口 CPU 源有序观察修正

基线 `1f29f2af43e1833e619e1d63f3298ae52ce642d8`。真实 stock Mali 普通 UID 的 ASTC GPU oracle 已通过，随后真实游戏在图像 CPU 转换被原 guard 拦截；本修正不删除该 guard，不修改游戏或像素数据，不引入另一套同步机制。

现有 tombstone 的 BuildID `d21ca1c2e172145f253dfa6721fdf1f83cd08f7d`，PC `0x32a570` 经冻结 ELF/NDK llvm-addr2line 对应 `image_upload_observed_contents:46`，调用链是 sampled texture binding → shader bindings → `vk_draw_record:1248`。原 draw/dispatch outer gates 和公共 CPU-source helper 只认可 defer/shared，漏了新 legacy CPU 观察模式，因此在借用 image/view/temp 之前没有执行原 prepare。

三处 gate 增加原不可变 legacy mode。另一个有序性边界是：已 initialized/clean 的 image，其排队 CPU event/writeback 在 completion 之前尚未推进 dirty generation。legacy preflight 不能只检查 dirty pages，须先检查真实 host-page 包络的 recording/pending CPU effects，必要时执行原 prepare/commit/wait，再按原 dirty proof 上传。查询仅在 legacy/private 模式生效；不把 shared 自有 GPU canonical publish 当成 CPU effect，保持 shared clean sampling/no-wait。deep image guard在 clean fastreturn 前检查晚到 CPU effects，失败仍拒绝，绝不在 image 被借用后临时 submit/wait。

可选 ghost 重放故意位于外层已借用资源期间，不能启动可能等待 FS lease 的 CPU 观察。legacy 下凡涉及 tracked packaged source/target，原非阻塞 readiness 保守拒绝这次额外重放，包含当前 clean、之后才被 CPU 写的情况。普通 required draw/dispatch 使用完整 outer preflight，非 packaged/default 模式原样；没有扩大为全队列等待或新增 signal 行为。

原 finite/private NVN fixture 新增 `legacy-image-outer-preflight`，必须七个布尔均 true：defer 禁用、fresh draw/dispatch、initialized-clean pending CPU effect、同 host 页邻 word effect、不同页 effect 不等待、packaged ghost decline、真实 GPU 像素。调用实际 `vk_draw`/`vk_dispatch`，排队原 `mtl_sub_event` CPU 发布，没有在其间手动调用 texture preflight；实际 CopyBufferToImage 回调先验证 CPU 已发布，再验证原 GPU shader 结果。当前 LVP 可验证此 RGBA 路径，不能代替实际 Mali ASTC GPU oracle。新版独立控制器额外强制新行及旧实际 ASTC/storage/CPU-phase 证据全部成立，旧 1039198 次结果单独不能通过。

源冻结后只从同一次完整 canonical APK 重包独立普通 UID fixture，六个 native 库保持同字节；默认 plan-only，无设备操作。真实游戏仍由 root 从原始 NSO 同源重生、验证、存档和回滚。此修正不宣称 FPS/带宽/发热收益。
