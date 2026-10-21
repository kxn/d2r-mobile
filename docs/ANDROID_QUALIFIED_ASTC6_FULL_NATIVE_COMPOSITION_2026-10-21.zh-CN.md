# 已通过原质量门的2838项 ASTC6：按真实 native 分配选择组合

此后继基 d1fa237 的原 composer，默认 `pilot32` 保持原32项输入、实际收条、严格减小分配条件和原6217家族。新的 `--profile qualified-full2838-v1` 固定 prepared SHA `cfa2f0852b08d41b71d1599a3757dec7211a6483d29d5f29b55c4a49948d2b1a` 与 selection SHA `597cd590fb6002cc086fab9ceaccb5d9023c176d4810d9f7371722c0048efc74`；不重新编码，不改 runtime/driver/game。

完整2838输入来自原6217交集，仍沿原单层2D、非UI/normal/已声明array排除、16MiB源输入以及逐mip原质量门。实际产物需要23批全部通过原8064/f830 producer/actual_batches_gate。新增模式必须显式给本次实际 result/index SHA；缺失、旧32收条、未完成或清理不闭合时拒绝组合。不能将支持Vk165/166名单或CPU重新排布当作GPU实证。

筛选比较原 strict pack 的 `image_size` 与真实 producer 的 `physical_image_bytes`：

- 实际分配变小：保留 ASTC6。
- 相等：仅在 prepared `linear_ASTC_bytes`（实际 mip compressed block hashes一致）或被认证源CFT字节下降时保留，分配节省记0。
- 实际分配变大：保留原 ASTC4，哪怕压缩块或源padding减少。

`allocation-selection.json` 对全部2838逐项记录较小/相等/较大、选择原因、实际metadata SHA、源字节与 useful ASTC 字节；`notlarger`、实际保留数和真正小于原分配的字节和分别报告。native 分配不能用源NVN padding替代。16MiB是原源输入限制，不臆造额外的native分配上限使较大项无法正常回退。

只把选中条目交给原 strict packer，重组完整6217序列与原family协议；其余 entry detail、wire、family、descriptor、thumbnail 和整个未替换 cache 原样验证。原CASC writer、TVFS受限更新、原始BC其余资源、上游UI、完整源SHA和输出哈希保护均保留。等大项是显式源/压缩数据收益，不声称 GPU bandwidth 或 FPS 已改善。全部大小统计是构建/分配查询证据，不是PSS、DRAM、持续散热或实际游戏内存净收益。

新增成本只在离线组合：读取已认证原/新metadata，2838行字典与决策报告；不复制整批payload，不增加应用运行时分支/缓存/内存。历史pilot32及旧6217输出不改，始终新输出目录；所有新composition仍 `game_acceptance=false`、`deploy_allowed=false`，后续须原NVN/原游戏实际视觉与资源资格。
