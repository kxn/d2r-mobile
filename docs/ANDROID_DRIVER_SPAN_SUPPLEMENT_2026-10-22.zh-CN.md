# 仅补充 Driver Uniform Span 证明

此工具只补充离线证明，不改运行时，不编译 Shader，不连接设备。

## 为什么需要独立合同

原 `deploy-android-shaders.py` 验证的是当前源码指纹对应的完整库：
`.chain`、JSON、编译选项和所有声明产物必须一致。它没有“保持旧库、
仅追加另一条完整加工链中已证明的 DSP”合同。因此不能删除新链的大型
DPL、改回旧 chain 名称后冒充新完整库，也不能修改旧库 JSON 让它看似
由新链生成。

当前目标是保留 `9507875007c6` 的 45,312 个原文件，追加 6,927 个
96 字节 `.dspan`（共 664,992 字节）和 `_driver_span_supplement.json`。
旧 `.chain`、所有 JSON、原 DPL 和其他文件逐字节不变；不会带入新 DPL。

## 验证和证明转移

`scripts/dev/compose-driver-span-supplement.py` 要求显式 SHA256 pin：

1. 原库的完整部署 receipt 和原 cache。每个文件的大小、哈希、精确文件集合
   必须匹配原 receipt，包含 `.chain`、原 JSON 和 DPL。
2. 完整新库、原工具实际完成的独立 dryverify report、SourceReady 和
   build-inputs。所有新库文件严格匹配该 report；Ready 必须关联同一 report、
   同一 build-inputs、实际 producer source archive 和 translator tool 记录。
3. 每个 Shader 的 `meta/spv/spec/pred/tssbo` 存在性和实际字节必须分别相同，
   不是仅比较基础 SPIR-V。使用原 `dmsh_driver_keys.metadata/fingerprint`
   检查 stage/set/descriptor binding 和五类指纹，并按原 DSP1 定义重建
   binding/end/header/digest，要求 96 字节完全一致。

完整新库的原 dryverifier 已从实际模块重算了所有分支和变体的 read bound。
补充工具只转移这份已验证证明到相同的不可变模块，不重新做 SPIR-V 分析，
不从运行场景、材质值或 Shader key 猜测边界。没有完整新链资格时不能 compose。

独立 manifest 分开记录原 chain、proof chain、所有输入 receipt、补充工具
及其实际导入源码指纹、每个新增 DSP 的哈希和五类指纹、输出 inventory 摘要。
manifest 自身的哈希仅放在外部 compose receipt，避免递归哈希。此输出是一份
**原库加独立补充产物**，不声称是 proof chain 的完整库。

## 使用

先通过 `--help` 查看显式输入；需要原 cache/receipt、proof cache/report/Ready/
build-inputs，以及四个 JSON 文件的预先核实 SHA256。例：

```sh
python scripts/dev/compose-driver-span-supplement.py \
  --baseline-cache /absolute/path/vk-9507875007c6 \
  --baseline-receipt /absolute/path/original-deploy.json \
  --baseline-receipt-sha256 "$ORIGINAL_RECEIPT_SHA256" \
  --proof-cache /absolute/path/vk-8b016120f586 \
  --proof-receipt /absolute/path/completed-dryverify.json \
  --proof-receipt-sha256 "$PROOF_RECEIPT_SHA256" \
  --proof-ready /absolute/path/proof-source-ready.json \
  --proof-ready-sha256 "$PROOF_READY_SHA256" \
  --proof-build-inputs /absolute/path/build-inputs.json \
  --proof-build-inputs-sha256 "$BUILD_INPUTS_SHA256" \
  --output /absolute/path/composed-cache \
  --report /absolute/path/compose-receipt.json
```

再用同一组输入、同一 output、一个新的外部 report 路径，附加 `--verify-only`
独立核验完整组合目录。输出和 report 均不能覆盖输入 cache。compose 不覆盖
已有 output/report；先在自有临时目录完整验哈希再 rename，失败不发布半份目录。

该工具没有 `--deploy`，也不调用 ADB。后续发布可以由现有 inventory/CAS/
目录 rename 原语使用外部 receipt 的完整 `artifacts`，保留原库作为回退；
发布与真实游戏性能验收单独记录。

## 现有证据和范围

- 原完整 receipt：`.local/logs/pixel-local-runtime-v1-q-shaders-deploy.json`，
  SHA256 `55f2eb030ebf6366a7ecc12adc0c57cd6ef4a13339fa6e1a34fad796a87ea927`。
- 原库重新核验：
  `.local/logs/driver-uniform-span-5a5571d-shaders-v1/original-baseline-artifacts-reverified.json`。
- 原新链 build-inputs：
  `.local/logs/driver-uniform-span-5a5571d-shaders-v1/build-inputs.json`，
  SHA256 `6bfac08d68af45984aed79d299632600ff20f07726c983bcfd5f1c7343826d12`。

`test_driver_span_supplement.py` 是小型宿主 provenance/wire 正负测试，
不编译或分析 Shader，也不是 GPU/游戏测试。覆盖原文件变更、五类模块变化及
成员缺失、重签顶层记录后错误 DSP binding/fingerprint、错误/重复 manifest、
额外文件、错误 receipt/Ready 关联，并证明旧 JSON/chain/DPL 未被替换。
这些测试和本工具成功都不代表降低了真机 PSS、温度或提高了 FPS。
