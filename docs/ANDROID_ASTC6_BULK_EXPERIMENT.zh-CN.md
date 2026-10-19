# ASTC6 有界批量转换

`astc_pack.py` 默认仍为原有 ASTC4×4。本入口只在显式执行 `astc_bulk.py batch`
时转换选中的非 NRM 纹理，不运行游戏、不部署、不自动开始全量编码。复用
`astc_repack.convert()`、原 NVN C layout/swizzle/deswizzle 和真实 astcenc；
没有新增块编码器或拼图布局。原 `f7b3ada` 小样源及产物保持不变。

## 清单与支持范围

```sh
python3 dm/tools/astc_bulk.py inventory /path/to/original/RomFS inventory.json
```

原 manifest 和 descriptor cache 每次调用各读一次。支持范围延续小样：
BC3 format61/62，type2/depth1/layoutType3，原 C 可表达的打包布局、有限尺寸/mip。
清单先核 descriptor/manifest，完整 header、每级 mip、thumbnail 与原图对应关系
在转换时继续严格验证；不能把 metadata 候选当成已完成逐图验证。

当前原资源有 27,195 个 BC3 descriptor，其中 8,447 个 `_nrm.texture` 保持现有
full4，不参与本入口。非 NRM 共 18,748：ALB 8,881、ORM 8,570、其它线性 1,018、
其它 sRGB 279。主要 ALB 为 256²/9mip，ORM 为 128²/8mip。非 NRM 原 decoded
文件体积共 950,815,224 字节。按原 C 布局预估，NVN padded storage
948,893,184→695,153,152 字节，linear compressed blocks
907,616,528→420,964,144 字节。没有计入实际 VkImage requirements，也不是实测
总线带宽、显存占用或 FPS 收益；质量回退还会减少可转换范围。

## 小批量与恢复

selection.txt 每行一个完整逻辑路径，例如 `data/hd/.../image_alb.texture`。
`--limit` 必填且为正数，选择数量不能超过它；首次验证仅使用 8～16 图。

```sh
python3 dm/tools/astc_bulk.py batch /path/to/original/RomFS /path/to/new-output \
  --selection selection.txt --limit 8 \
  --baseline-cache /path/to/verified/full4/files/data/hd/texture_desc_cache.json \
  --baseline-index /path/to/verified/full4/pack_index.tsv \
  --astcenc /path/to/real/astcenc --threads 2 --preset thorough
```

每次任务原 cache、full4 cache 各解析一次，按顺序逐图执行，astcenc 最多 2 线程。
每图的 changed descriptor、质量报告和数据放在独立目录；全部产物哈希完成后，
原子发布该图 checkpoint。原 cache、manifest、full4 cache/index、选择、配方、
工具源码/动态库/编码器哈希共同绑定 job。相同命令可恢复：重新核输入内容和
checkpoint 文件哈希，已完成的图不重编码；中断图只清理自己的未提交输出后重做。
输入/配方变化必须新建输出目录，损坏产物报错，不把失效命中冒充成功。

最后才在内存中合并一次并写一次完整 descriptor cache；仅选中且通过检查的
6×6 entry 替换 full4 对应 entry，所有其它条目逐项保留。`repl.tsv` 只声明这些
纹理及合并后的 cache。后续必须以原 CASC 工具将其叠加到已验证的完整 full4 pack，
其余 prebuilt entry 不变。baseline archive 自身的完整性仍由原 pack provenance
和组合入口验证，本工具不把只有 index 的校验冒充整个大档的校验。

## 质量选择与成本

默认检查可配置：

- 中/大 mip（最短边≥8）使用现成 astcenc 的 RGB PSNR，默认最低约 32dB。
- 恒定 alpha 默认要求精确保留；`--constant-alpha-max-error` 可显式调整。
- 其它 alpha 最大误差默认 8；透明/截断相关覆盖阈值 1、128 默认允许变化比例 0.001。
- 记录 255 覆盖，但不把 254→255 单独当作透明覆盖错误。
- 极小 mip 不以 PSNR 单项否决，报告绝对误差、linear block 字节及体积权重。

这些是保守且可审查的候选配方，不是游戏视觉等价证明。未达门槛只标记
`quality_fallback` 并保留现有 full4，候选文件与完整指标仍可审查；不将其归为
方向无效。格式不支持同样保留 full4，损坏输入或未解释的一致性错误直接终止。
所有 NRM，包括非 BC3 NRM，均保留 baseline，禁止选择 NRM 进行 bulk 转换。

安全路径仍按每 mip 运行编码、质量 test-mode 再编码、解码及 retile 解码；
即编码两次、产生多份 PNG/EXR/log，额外 CPU、进程启动和临时 I/O 不小。
为核恒定 alpha，再读取一次该 mip 已有 source PNG。尚未优化成 6 对齐 mosaic，
也不以小图批次吞吐推断全量耗时。质量日志的 Coding time/rate 汇总仅覆盖官方
test-mode 的编码阶段，不等于完整任务时间；没有自写计时器/采样器。

自有数据测试：

```sh
ASTCENC_TEST=/path/to/real/astcenc python3 scripts/dev/test_astc_bulk.py -v
```

测试复用原 NPOT 自有 BC3 fixture，覆盖第一张完成后中断、恢复只处理未完成图、
不支持格式保留 baseline、NRM/未选条目不变、再次恢复零编码、产物损坏拒绝，
并验证质量门槛的恒定 alpha 与透明覆盖语义。它们不是游戏运行或原厂性能结论。
