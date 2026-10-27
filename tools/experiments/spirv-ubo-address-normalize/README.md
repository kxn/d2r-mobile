# Exact offline UBO address normalization candidate

This independent tool uses official SPIRV-Tools for disassembly, assembly,
cleanup and validation. Its only custom transform normalizes scalar 32-bit
integer address arithmetic when known low bits prove equivalence, including
wraparound. It never changes floating-point precision, descriptor ranges,
robustness, memory loads/stores, interfaces or game logic. It has no shader-key,
bone, material, scene or fixed-stride allowlist.

The observed input has byte-address multiplication and offsets followed by
word-index and vec4-index shifts. Exact no-carry normalization gives all four
scalar components the same vec4 index. Whether Mesa then combines the scalar
loads is a separate native compilation test, **not** a consequence of
`spirv-val` or a smaller file.

The rules are:

1. Compose logical shifts. With `c=0`, `(x>>s)>>t == x>>(s+t)` for any bit
   pattern. With an added word offset `c`, require its addition not to carry
   out of the exact known low residue. Keep `s,t>0` and `s+t<32`.
2. For `a` with the low `s` bits known zero, replace
   `((a+b) mod 2^32)>>s` with
   `((a+(b & ~((1<<s)-1))) mod 2^32)>>s`. This keeps high-bit overflow;
   it does not turn a word-index addition into an overflowing byte addition.

Integer type and width remain unchanged. Unknown alignment, arithmetic shifts,
vector shifts, specialization constants and an unproved carry are excluded.
New nonconstant instructions are inserted at the use, retaining dominance.
Each rewritten result is recorded; `prove.py` uses standard Z3 bitvectors to
prove its inequality unsatisfiable for **all** 32-bit inputs. Unsupported,
unchanged source expressions are independent symbolic values, not assumed
runtime constants. SMT proofs do not replace real Vulkan validation or visual
testing.

Example (no device access):

```sh
python3 tools/experiments/spirv-ubo-address-normalize/normalize.py \
  --input /absolute/original-module.spv --output /absolute/new-candidate
PYTHONPATH=.local/tools/ubo-bitproof-python:tools/experiments/spirv-ubo-address-normalize \
  python3 tools/experiments/spirv-ubo-address-normalize/prove.py /absolute/new-candidate
PYTHONPATH=.local/tools/ubo-bitproof-python:tools/experiments/spirv-ubo-address-normalize \
  python3 -m unittest discover -s tools/experiments/spirv-ubo-address-normalize -v
```

The optional isolated proof dependency is `z3-solver==4.13.3.0`; the actual
installation receipt for this audit is under
`.local/logs/robustness-buffer-access-audit-20261028/z3-install-report.json`.
The normalizer itself needs only Python and the pinned NDK SPIRV-Tools.

Current host checks: the two actually observed hot VS sources normalize from
72 logical right shifts / 44 integer adds to 12 / 8. The preexisting `spirv-opt
-O` trial retained 72 / 44, documenting the existing-tool gap. Both outputs
pass the official Vulkan1.1 validator. A real ordinary-UID Android compile-only probe now demonstrates native
vectorization on the exact Turnip12b8 driver: the actual shadow VS baseline
reproduces349 instructions/48 scalar LDC, and its normalized candidate has142
instructions/12 vector LDC. Full registers12 and16 waves stay unchanged.
The second VS, linked to the same small shadow fragment shader, changes413→195
instructions and48→12 LDC (16 registers/12 waves unchanged). Its original
full HDR link was485/486 instructions; do not substitute this413-instruction
compile fixture for the full HDR native workload.

The frozen run is
`.local/logs/robustness-buffer-access-audit-20261028/actual-compile-v2/`.
Its APK SHA256 is `b2fa8dd522273bef38d97a1c65d3b83f59ad5e9a201b316b9395ea285de8319d`;
loaded driver SHA256 is
`12b8b54a2514ff1cdf8da422c71db79a683d4517eb9aecfbb20a4125522559f5`.
All four graphics pipelines succeeded with robustBufferAccess and
shaderDrawParameters enabled. Official Mesa IR3 output, exact input hashes,
ordinary UID/maps, and raw results are retained. No draw or queue submission
was issued, no timer was implemented, and no FPS or pixel-correctness claim is
made. The earlier `actual-compile-v1` records a host component-name typo before
any app start; it is not a driver failure. No existing usable host
`ir3_cmdline` was found; the frozen Mesa source's IR3 test disassembler is not
a Vulkan pipeline compiler.

Build this independent compile-only APK with the original qualified loader:

```sh
python3 tools/experiments/spirv-ubo-address-normalize/build-probe.py --help
```

The build script takes explicit driver-build and output arguments and selects
the frozen original/normalized inputs and observed fragment partner declared
in its source; its build record freezes
all source, driver, Java, tools and shader hashes. `probe.c` only creates and
destroys four PSOs. Deployment and data retrieval use the separately frozen
ordinary-UID controller in the audit directory; the original game is not
modified by this fixture.

Before deployment, integrate the rule into the existing declared shader build
entry and regenerate from its original inputs with a new complete source and
artifact ledger. Do not copy this candidate over an accepted shader cache or
mislabel its old chain. The compile-only native check is now complete. Next check visual and
whole-frame tool results in a newly verified derived game chain. Per-draw timestamps identify shader ownership and
must not be used as isolated shader costs.
