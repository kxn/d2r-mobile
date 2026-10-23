# Buffer-only sparse host-import experiment

This patch is an isolated experimental delta against the project's verified
f830 Turnip source, **not** against unmodified upstream Mesa 26.2.5. It is not
applied by the default build or installed into the daily game.

`baseline-source-proof.json` identifies the original ICD and its compiled source
inputs. Apply `buffer-only.patch` only to that verified baseline, using
`git apply --unidiff-zero buffer-only.patch` (zero context). Its three resulting
source files match the original frozen build; the build record retains the
original patch hash and build identity. The tested
experimental ICD SHA256 is
`a90c1b4debbf71a7b88cf7c5daa8bd0e71a03b7747629b40906ef81921d824ff`.
It changes software eligibility for Buffer sparse bindings only; the existing
KGSL capability probes, image restrictions and host import implementation remain.

Build/source manifests are retained locally under
`.local/build/turnip-sparse-host-buffer-v1/source-ready-freeze/`. The finite GPU
qualification, its limits and current CPU budget are recorded in
[`ANDROID_CPU_RESIDUAL_SPARSE_2026-10-24.zh-CN.md`](../../../docs/ANDROID_CPU_RESIDUAL_SPARSE_2026-10-24.zh-CN.md).

The Q735 transfer and compute/semaphore qualification does not establish support
on other devices, native-image alias compatibility, production Pool lifetime
correctness or any FPS improvement. Production integration remains an opt-in
experiment on `codex/pool-sparse-backing-v1`.
