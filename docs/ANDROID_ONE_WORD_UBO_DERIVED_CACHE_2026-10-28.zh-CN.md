# One-word UBO：经验证父链上的独立派生候选

当前候选只变更两个既有FS specialization载荷，不重新选择材质、不改游戏binary/NSO规则、资源或APK。两份载荷逐字匹配已经在实际12b编译的nested-condition v2；原`.spec`六个selector字段、base SHA、base SPIR-V及二进制metadata保留。静态branch与predication已保住“每lane只选一个word”，分歧wave成本和动态带宽/帧率仍待真实游戏验证，不能由逻辑word数推总线降低75%。

实现是两个独立工具，未将此机制混入address pass：

- `dm/gfx/tools/dmsh_one_word_ubo.py`：识别同readonly Uniform **Block**的四component、两位selector与完整one-hot位运算；Z3对所有uint32载荷证明，再生成三个带DontFlatten的显式条件header。可写旧BufferBlock、volatile/coherent、分组装饰或未知载荷用途保守拒绝。只读Block仍可能别名到另一SSBO/image，所以模块存在外部Store/Copy、ImageWrite、原子、屏障或interlock时也拒绝；仅静态可追溯的Output/Function/Private局部存储可保留，未知指针/外部GLSL输出指针拒绝。实际完整FS插入合法SSBO别名写/原子/copy与屏障的负控经官方验证后被拒绝。
- `dm/gfx/tools/dmsh_derived_one_word_ubo.py`：明确父cache/记录SHA，复用原recipe repacker和dkey/dspan/pixel-local emitters，完整重建hash闭包。只改变已声明的两个paired payload及依赖产物，不生成新selector。

父链是`cf83dfece8e7`，记录SHA `f5b0499a1aef48fff8e97f2437dafc64e1d6cbc8e1fa3acea276a20e3416b99b`，原29e receipt SHA `59320caac04154d1dd118581ed4443b563cd862b17ded88c73adc1c6da2083c2`。父记录绑定的是git111154fc版本的combined verifier（SHA86c34d…），当前源码后来增加了“原本未配对FS”支持。因此实际执行父记录所绑定的历史verifier，并将其字节归档；仅将其自读源码身份绑定为真正执行的历史字节，所有其它依赖/工具仍由原函数重新校验。没有把当前superset冒充旧来源或忽略source mismatch。

独立验证会重验完整父链、全部候选文件SHA与文件集合、重放两次变换、原selector打包和全部closure生成；重写候选ledger也不能绕过变换/closure/无关产物检查。构建前即拒绝父目录链接、子目录和未知文件。构建复用未变文件的父ledger，仅计算变化项，末次验证父/候选各一次全量SHA，不为每个阶段重复扫描52k文件。

构建及验证入口：

```sh
python3 dm/gfx/tools/dmsh_derived_one_word_ubo.py build \
  --parent-cache .local/build/address-combined-derived-v2/vk-cf83dfece8e7 \
  --parent-record-sha256 f5b0499a1aef48fff8e97f2437dafc64e1d6cbc8e1fa3acea276a20e3416b99b \
  --keys 5c1ca4e7c3e4657c 38f344adf801b153 \
  --output-parent <new-directory> --report <new-report.json>
```

`build`已包含独立verify。需要单独重验时使用`verify --cache <candidate> --parent-cache <parent> --parent-record-sha256 <same-SHA> --report <new-report>`。首次取历史源码需要仓库对应git历史；候选内归档的历史源码用于后续verify。运行环境需原SPIRV-Tools/spirv-cross与Z3，版本/完整哈希均绑定到chain。

本次最终完整产物是`.local/build/fs-one-word-game-cache-v2/vk-80f1fda6f184`，6931 shader、52259个声明artifact，末次独立验证通过；记录在`build-alias-final.json`，交付说明在`ready-alias-final.json`。先前886/a5/a0中间版本及来源拒绝记录保留，不作为部署版本。

手机只需**14个runtime变化文件，共1,582,023B**：`.chain`、`_vk_manifest.json`及每key的`spec/json/dkey/dspan/plocal/plocal.json`。逐项新旧SHA见`ready-alias-final.runtime_delta`，host verifier/archive/buildrecord不上传。手机上的精简runtime视图不冒充完整host候选目录；当前通用发布registry尚未注册这个新schema，部署须使用独立审核的adapter，不能绕过源验证。本文不宣称已经部署或测得游戏性能。

测试入口：`dm/gfx/tools/tests/test_derived_one_word_ubo.py`（实际父链/两FS闭包及selector、payload、closure、无关文件、档案/历史verifier篡改、构建前链接拒绝）和`test_one_word_ubo_pattern.py`（真实readonly Block正例、旧BufferBlock/分组装饰负例）。全部原始实际编译证据在`.local/logs/fs-one-word-load-experiment-v2/`，原v1失败和旧解析器拒绝均保留。
