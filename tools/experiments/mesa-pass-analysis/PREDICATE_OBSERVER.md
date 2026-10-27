# DPR1 ready-input observations

This optional diagnostic fills a gap in the existing tools: the current Mesa
draw/IR3 capture identifies actual native shaders but does not record guest
Uniform values. The older API-structure GFXReconstruct capture has descriptor
offsets and initial resource snapshots, but no running-window CPU `FillMemory`
updates. Its different driver/process cannot supply the current mask distribution.
This diagnostic counts existing ready inputs; it does not add a timer, sampler,
GPU query, frame callback or submission.

`DM_VULKAN_UNIFORM_PREDICATE_OBSERVE=1` is a cold, default-OFF policy. It is
mutually exclusive with executing the hybrid DPR1 module. It is initialized at
the first valid existing `.pred` recipe; no recipe means no observations.
All original base hash, complete old `.spec` hash, binding, range, predicate and
SPIR-V qualification checks remain in `uniform_predicate_load`. Observe mode
allocates the same recipe fields, but does **not** create its shader module.

For an admitted draw, `vk_uniform_hybrid_pick` first calls the unchanged original
specialization picker. It returns that same module and the same two values.
If the original picker does not select `.spec`, no extra source is read.
Otherwise it reuses the original selected word and reads the second recipe word
only through the existing nonblocking `vk_pool_try_read`; insufficient range,
overflow or a pending GPU writer counts as unavailable. No flushing, demand,
waiting or input modification is permitted. The original current-draw writer
guard still controls whether this picker is reached.

Per-recipe counts include ready observations, baseline fallbacks, unavailable
second words, sixteen mask buckets and sixteen complete `(raw Spec0, mask)`
tuples. Tuple overflow is counted rather than allocating an unbounded map. A
fallback includes all original reasons and is not falsely labelled GPU pending.
Counts are neither fragment invocations nor exclusive HDR costs. The original
`shader_end` publishes them with atomics; the getter exports coherent per-row
snapshots or explicitly marks a concurrent snapshot unavailable. Rows carry the
shader key and full original base/spec hashes. Caller frees the string from
`vk_uniform_predicate_observation_json()`. The cheap
`vk_uniform_predicate_observation_enabled()` only reads the existing atomic flag;
it never initializes policy or reads the environment, allowing the host to omit
JSON construction in normal OFF runs. The original Android game-state writer
conditionally exports `uniform_predicate_observation`; normal OFF runs skip
its JSON allocation. The existing APK builder's mutually exclusive diagnostic
option `--uniform-predicate-observe` installs a small standard `wrap.sh` to set
the cold policy. The build manifest marks that APK ineligible as an ordinary
performance baseline. There is no additional in-game UI or runtime toggle.

The default path adds one cold-policy test. Observe mode adds a ready-word read,
bounded tuple lookup and counters; publication copies a bounded snapshot for
changed rows. A diagnostic FPS must not be reported as the uninstrumented
baseline. Objects follow the original process-lifetime shader ownership.

## Recipe construction boundary

Do not place generated recipes into the old `29e56b51bce5` cache while retaining
its old manifest. Current tools calculate a different source closure, so this
would misstate the input/build identity. A full rebuild is unnecessary **if** the
original build entry explicitly supports an already verified cache as an input
stage:

1. Consume the prior complete cache manifest **and a full per-file SHA ledger**,
   recheck its 6931 key set and each payload. Preserve the old generating identity;
   do not claim the current translator generated those bytes.
2. Include that input ledger digest, original generator identities, current
   DPR1 generator/regenerator source/tool hashes and explicit options in a new
   derived-chain identity. This is a new declared build stage.
3. Copy original `.spv`, `.meta`, `.spec` byte-for-byte into an independent output.
   Generate `.pred` via existing `dmsh_uniform_predicates.transform`; update only
   metadata/proof fields and regenerate existing driver-span/pixel-local closure
   files through their original emitters. Do not hand-edit binary sidecars.
4. Emit a new complete manifest that explicitly says `verified-cache-derived`.
   The original verifier needs an explicit matching derived mode: reconstruct
   recipes/closures and demand every base/meta/spec SHA equals the declared
   frozen input. Reject a missing/wrong input ledger, changed base byte, malformed
   old recipe or undeclared output. Normal source-build verification stays strict.
5. Only deploy after that original-entry/manifest path is reviewed and passes.
   A two-module diagnostic artifact is not a full game cache. If derived mode
   is not implemented, use the original complete build and compare all unchanged
   base/meta/spec bytes; do not bypass source identity to save build time.

This document is the minimal construction plan, **not** a claim that the derived
entry or Android observation qualification has already run. No shader cache or
game input was changed by the host tests.

## Validation

`test_predicate_observer.py` builds the real observer include with ASan/UBSan.
It checks default OFF and cold immutability, invalid settings/hybrid conflict,
unchanged baseline module/values, all four two-bit masks, no extra read on
baseline fallback, pending/range overflow, aliased inputs, bounded tuple overflow,
and publication/JSON accounting. The actual production `nvn_vk_shader.c` also
passes a syntax check against its real types/headers. These host contracts do not
substitute for an Android build or real-game observation acceptance.
