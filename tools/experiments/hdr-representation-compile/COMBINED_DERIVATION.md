# VS2 + FS2 exact-address candidate

The canonical candidate is derived from the complete original29e verified
cache and its original receipt, **not from modified VS-only cache1065**.
The finite transform has no shader-key policy; the CLI explicitly selects the
four observed modules for this small experiment.

New entry: `dm/gfx/tools/dmsh_derived_address_combined.py`.
Schema: `dmsh-verified-cache-address-combined-v2`, registered explicitly in the
existing derived verifier. The original v1 normalizer/cache stages remain
unchanged; their source/tool identities are still valid.

For each selected fragment shader, the original base and original `.spec`
payload are normalized independently with the same exact integer rules. The
new `dmsh_uniform_specialization_repack.py` reconstructs the declared DSP1/DSP2
header from named fields using the original container format. It preserves
magic, flags, binding, Uniform slot/offset and SpecId, and the original uint32
specialization/default contract. Only the declared module word count and base
SHA change. It does not rerun the selector-scoring heuristic. Original recipe
identity, separate base/payload proofs and the unchanged selector tuple are
recorded. Given the original base/selector relation, the two exact equivalences
preserve that relation for every selector bit pattern.

The independent verifier checks the entire original receipt, original manifest
bytes, original selected source/JSON/recipe archives, complete output set,
mutation boundary, and source/tool fingerprints. It reruns all six module
transforms, the original structured recipe packing and the existing driver-key,
span and pixel-local emitters. Unselected files and every `.meta` stay exact.
It rejects changing the original selector even if the outer artifact ledger
is rewritten. Z3 proofs are qualification evidence, not a build dependency.

Reproduce the candidate without device access:

```sh
python3 dm/gfx/tools/dmsh_derived_address_combined.py build \
  --input-cache .local/build/adreno-shader-upstream-rewrites-v2/vk-29e56b51bce5 \
  --input-receipt .local/logs/adreno-submit-granularity-v1/shader-combined-deployment.json \
  --input-receipt-sha256 59320caac04154d1dd118581ed4443b563cd862b17ded88c73adc1c6da2083c2 \
  --keys 3deaf68fd65b5298 54c93fa22abff9ee 5c1ca4e7c3e4657c 38f344adf801b153 \
  --output-parent /absolute/new-output-parent \
  --report /absolute/new-build-report.json
python3 -m unittest discover -s dm/gfx/tools/tests \
  -p test_derived_address_combined.py -v
```

Seven actual-input contract tests cover successful closure regeneration,
unchanged VS-only SPIR-V, exact selector fields and dual FS proofs, mutated
selector, replaced base, original metadata/manifest/archive mutations,
undeclared files, missing FS pairs and invalid original recipe identity.
They use original shaders when locally available, and explicitly skip rather
than invent original identities when those inputs are absent.

The actual candidate was built as
`.local/build/address-combined-derived-v2/vk-cf83dfece8e7`:
6931 shaders /52240 original input artifacts,52252 derived artifacts plus its
build record. Both vertex modules exactly match the measured1065 candidate;
both FS specialized payloads exactly match the native1993/2038 compile inputs.
The original six selector fields remain identical. Four FS whole-domain
proofs using the same pinned `/usr/bin` SPIRV-Tools match the actual candidate
bytes; earlier NDK-tool proof variants remain separate and are not claimed
byte-identical to outputs from a different optimizer binary.

Host-only publication verification:

```sh
python3 scripts/dev/deploy-android-shaders.py \
  --verified-cache-derived \
  --cache .local/build/address-combined-derived-v2/vk-cf83dfece8e7 \
  --derived-input-receipt-sha256 59320caac04154d1dd118581ed4443b563cd862b17ded88c73adc1c6da2083c2 \
  --report /absolute/new-host-verification.json
```

Incremental deployment uses the existing publisher, with `--deploy
--reuse-device-input --serial <serial>` appended. **Its current device-input
precondition is the exact original29e cache from the declared receipt.** It
cannot be pointed at an active1065 cache and silently treat that as the input.
The caller first uses the existing stopped-game transaction to park its current
candidate and select the original input, retaining user backups. The publisher
fully verifies that input, makes a non-hardlinked copy, uploads only changed
artifacts, verifies the complete new candidate and publishes atomically. No
ADB/deployment command was run by this combined-cache preparation task.

The existing `combined-transfer-diff.json` lists every changed/added file and
hash, including the original-source audit archives. These bookkeeping files
are not runtime shader alternatives. The experiment changes only shader
integer expression forms; no runtime, precision, binding, robustness, gameplay
or resource encoding changes are added here. Real-game pixel/stability/FPS
validation remains a separate step.
