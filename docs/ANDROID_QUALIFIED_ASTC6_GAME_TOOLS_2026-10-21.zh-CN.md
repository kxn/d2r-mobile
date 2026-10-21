# 32 项 ASTC6 原生共享资源：原游戏短资格工具

本工具只编排既有原游戏事务，不修改运行时、NSO、驱动、着色器或资源。固定 d148/f830、1MiB window、.60/2750×1260，6217 个原生资源中仅32项采用已有质量筛选的 ASTC6，其余6185项描述、配方与 family 字节不变。

显式 `qualified_native_asset_validation` 需要 root 资产审查及165/166双格式原 NVN 真机收条。完整复核12447个加工输出 SHA；复用原 CASC/TVFS/UI 证明和严格 packer 格式，不新增资源协议。历史 composition 的 `deploy_allowed=false`、`game_acceptance=false` 保留；本次是 root 授权的可逆实验，并非将资源默认发布。165 旧收条 GPU 与清理通过但 CLI 返回1的已知白名单错误如实保留。

旧 candidate_gate.py、六目录备份/CAS、安装器、首 PID/UID、正常三点摇杆、fresh save2、停止和原 APK/PSO/六目录恢复逻辑均不改变。control composition 仅作为原6217成员及来源参照被认证，不执行第二个 phase。这轮仍沿用现有七个官方 CPU 窗口及一次 system showmap，数据不能叫同资源 CPU A/B，也不证明整场景32项都实际消费、全游戏画面、持续热稳定或性能收益。普通输入造成的真实游戏进度按原流程保存，不回滚用户存档。

默认无设备动作；不加 `--execute` 仅完成全部离线 proof。执行输入的 `execution_ready` 仍须显式为 true；root 独占设备后按冻结输入 SHA 启动一次。成功 stage 为 `qualified-native-ASTC6-original-game-resource-validation-complete`，后续仍须人工查看实际截图/日志和完整恢复结果。

新门拒绝旧表/旧 composition、额外/遗漏/重复替换、ID/顺序/名称漂移、未选条目改变、非165/166变化、没有 root 授权或误标同资源对照、缺任一双格式 GPU 资格，以及输出文件被修改。原停止/存档失败门不因资产模式绕过。
