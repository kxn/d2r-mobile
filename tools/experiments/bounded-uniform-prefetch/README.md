# Bounded readonly Uniform prefetch

This is an optional, verified child of the exact combined-address shader cache.
It does not compile game modules, modify a parent cache, upload resources or
change runtime defaults. Runtime must separately implement and admit DSP3.

The tracked transformer is `dm/gfx/tools/dmsh_bounded_uniform_prefetch.py`.
It reuses the original exact four-word selector proof, adds four constant vec4
reads, retains the complete uint32 dynamic-index fallback, and marks only that
fallback diamond `DontFlatten`. It rejects volatile/coherent/external-write
sources. Selection equivalence is checked with Z3 for arbitrary uint32 indices
and payload bits; SPIRV-Tools validates before and after dead-code cleanup.

The two selected current modules regenerate byte-for-byte to the independently
compiled v2 inputs. Their actual12b native output put constants in the preamble
and retained real branches around the dynamic fallback, without changing19 full
GPRs /10 max waves. This is a compilation mechanism result, not game performance.

DSP3 has a128-byte header: magic/version, complete legacy recipe byte count,
candidate SPIR-V word count, required binding/reg/minimum effective bytes, base
SHA256, complete legacy SHA256, candidate SHA256, reserved zero. Its body is the
unaltered original DSP2 followed by the candidate SPIR-V. Current qualification
uses192 bytes because actual12b adds fixed-copy bytes[64,192), although the source
table reads only[112,176). Range failure must retain the original DSP2 module.
The finite Float32 range proof is reproducible with:

```sh
python tools/experiments/bounded-uniform-prefetch/prove-domain.py --report /tmp/domain-proof.json
```

NaN/Inf/invalid conversions are not removed: finite-domain reasoning only predicts
the useful path, while exact source equivalence retains all original indices.

The derivation entry is `dmsh_derived_bounded_uniform_prefetch.py`. It requires an
externally pinned parent record SHA and the exact parent directory. It reuses the
parent's recorded verifier and frozen compiler helpers; it does not silently
accept today's newer source as the parent's identity. The parent independently
verifies its complete original input authority. No implicit parent chosen from
an untrusted child record is an acceptable deployment substitute.

```sh
PYTHONPATH=dm/gfx/tools python dm/gfx/tools/dmsh_derived_bounded_uniform_prefetch.py build \
  --parent-cache PARENT --parent-record-sha256 EXACT_PARENT_RECORD_SHA \
  --keys SELECTED_FRAGMENT_KEYS --output-parent NEW_OUTPUT --report REPORT
```

The original base `.spv`, `.meta`, selectors and all unrelated artifacts remain
byte-identical. Changed `.spec` files are explicitly new DSP3 envelopes. Driver
key/span adapters reuse the frozen interpreter, prove the candidate's c0/c2 read
sets equal to legacy, then serialize the actual DSP3 five-artifact fingerprint.
Pixel-local adapters call the original analyze/input-variant APIs and serialize
original DPL1: kind2 remains legacy, kind6 is the new candidate. There is no edit
to frozen `dmsh_pixel_local.py` or its original wire helper.

Tests cover actual paired modules, complete parent/child regeneration, preserved
legacy/actual-v2 payload bytes, undeclared/symlink files, rewritten-ledger tamper
attempts, SHA/range/binding/reserved fields and original PL transformation with
kind1/2/6 source/variant identities. These host tests do not replace real game
range admission, readonly graphics rules, visual checks or official whole-frame
GPU/CPU profile comparisons.
