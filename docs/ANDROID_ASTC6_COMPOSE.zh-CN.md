# 已完成 ASTC6 批次的可复现组合

`astc_compose.py` 只验证和组合资源，不编码贴图、不加载/生成 NVN `.so`、不运行
`gen_romfs_overlay.py`，也不改 APK、游戏 NSO 或部署设备。保留原先经过校验的 UI
覆盖层；原 UI 生成器的 skill PNG 校验不放宽，本入口不重新生成无关 UI。

要求 Python 3.13+。使用原 `casc_overlay.py` 子进程，并通过标准解释器选项
`-X cpu_count=2` 限制原 ProcessPoolExecutor 的 worker 数；执行前核对该限制生效。
没有另写 CASC/BLTE 打包器。

```sh
python3 dm/tools/astc_compose.py \
  --batch /path/to/completed-batch \
  --raw /path/to/original/RomFS \
  --out /path/to/new-composition \
  --baseline-cache /path/to/full4/work/files/data/hd/texture_desc_cache.json \
  --baseline-index /path/to/full4/pack/pack_index.tsv \
  --baseline-pack-manifest /path/to/full4/pack/pack.json \
  --baseline-overlay /path/to/full4/deploy-overlay \
  --baseline-overlay-record /path/to/verified-deployment-record.json
```

baseline-overlay-record 使用既有部署报告的 `expected_hashes` 字段，列出覆盖层的
全部文件及其预先验证的 SHA-256。既有原 ASTC4/UI 报告可直接传入。工具要求文件
集合及所有哈希匹配，同时核对 full4 archive8 的完整大小和 SHA，以及其实际缓存
内容与传入 baseline cache 一致。不能拿 index 校验代替整档校验。

批次校验取得只读 shared lock；转换仍持 exclusive lock 时立即拒绝，不读取
正在生成的 partial 输出。要求最终 `complete` 报告存在，重新计算 job identity，
核源码/动态库/编码器和原 manifest/cache/full4 输入哈希，核选择与 checkpoint
一一对应、checkpoint 与最终结果相等、每图产物完整哈希、原输入内容哈希、
实际命令日志、layout/codec 校验记录、cache 与 replacement 声明。未知状态、
缺文件、额外文件、损坏哈希或配方不符均失败；不补造成功标记。

只有 batch 中的 converted 条目才可能采用。组合还独立保护 basename 后缀
`_nrm.texture`、`_normal.texture`、`_nrm$nrm.texture`（不区分大小写），不按目录
名称猜测法线：`detailnormal/..._alb.texture` 仍可采用。这个检查可保护早期冻结
bulk 分类漏掉的别名；不修改正在转换的源码或 checkpoint，不重新编码。最终 cache
从完整 full4 仅合并实际采用项一次；质量回退、未选资源和所有法线保留 full4
对应条目，所有未替换 index 行逐字保留。报告分别列 batch_converted、selected
及 extra_normal_aliases_preserved。

原 CASC 工具将采用的纹理和最终完整 cache 写到 archive9。虚拟提取清单只把这些
逻辑路径的旧 EKey 改成已验证的 full4 EKey，供原工具定位既有 CFT；原工具使用
prebuilt 条目重写 VFS/root 到 archive0a。验证解压后的 VFS 只有选中 CFT 的 EKey、
编码长和内容长变化，整棵路径树、规格、其它条目均不变；root 只有 VFS 引用及长度
变化，build config 仅更新 VFS 键/长度。每个新档条目再通过原读取器完整回读。

输出 `romfs_overlay_astc/` 继承原 UI7、full4 archive8、ctrl_touch、strings、pack.json
原字节，新增 archive9/0a 并替换 config。沿用原 pack.json（仍只描述 archive8），
组合的完整权威清单是 `composition.json`：包括来源、工具/命令、所有正式输出哈希、
新档、完整可部署覆盖层和上述验证结果。最后才原子写出 complete 清单；失败目录
不能部署，重做须使用新目录。文件通过硬链接继承，输入和结果均须视为不可变资源。

```sh
python3 scripts/dev/test_astc_compose.py -v
```

自有 BLTE/TVFS 测试覆盖 partial/仍在转换拒绝、损坏/多余文件拒绝、正常法线及
两类别名不进 archive9、cache 和原 CFT 保持不变、原 CASC 实际跨9写入/读取、
越界的 VFS 改动被拒绝。另以已完成 8 图批次验证真实资源完整组合，不替代运行
游戏、稳定性或 GPU 带宽/FPS 收益验收。不要将完成一个小批次的组合说成全量结果。
