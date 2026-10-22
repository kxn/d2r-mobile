# Explicit host sample-profile compiler experiment

Default is disabled. Sample-PGO is separate from ThinLTO; neither is enabled
by environment variables, profile filename discovery or an implicit fallback.
Host C/C++ sources, Shader/game data, driver and gameplay are unchanged.

The original APK entry adds four explicit arguments (all four required):

```
python3 scripts/dev/build-android-apk.py ORIGINAL_OPTIONS \
  --sample-profile PROFILE \
  --sample-profile-sha256 SHA256 \
  --sample-profile-provenance OFFICIAL_PRODUCER_READY_JSON \
  --sample-profile-provenance-sha256 PROVENANCE_SHA256
```

The producer must be a completed exact f75 three-record official AutoFDO
non-LBR → NDK19 LLVM sample profile. The builder validates each record's ELF,
`--use_lbr=false` and successful LLVM reader, plus the actual files pinned in
its qualification/input/manifest documents. All original diagnostics remain
producer inputs. This is a finite initial experiment contract, not a claim
that every sample mapped or every branch probability is known.

CMake verifies the profile SHA and makes a content-addressed private copy.
The input participates in configuration dependencies; a changed file is not
silently reused. Only dmhost_android and dm_original_features C/C++ consume
it. The original host GPU fixture can use the same explicit CMake parameters.
ASM, Opus, loader SOs, game modules and Vulkan driver do not consume it.
Default OFF does not add compile/link flags; APK entry explicitly resets the
CMake sample fields to empty when the four arguments are absent.

The first experiment preserves O3/-g/source names/noLTO; it does not add unique
internal linkage names, pseudo-probes, profi, sample-accurate, remapping,
fast-math or an instrumentation runtime. Compiler remarks are written through
original Clang optimization-record support, including sample-profile-impl.
No linker profile flags are required because this is not an LTO experiment.

The APK build manifest records original profile/provenance, all pinned
producer files, actual private copy and effective options. Canonical freeze
must also call native_sample_profile.archive_inputs(manifest, NEW_DIRECTORY)
to archive these external inputs separately from tracked source and record the
returned manifest pin as canonical external_compiler_input_archive. Do not count
an input hash appearing in JSON as a substitute for verifying its bytes.

Qualification requires actual APP commands to produce positive AppliedSamples
matching source functions/offsets. Option acceptance, ProfileSummary metadata,
zero-valued AppliedSamples or a new binary hash alone are insufficient. The
same source/debug/ABI/APP guards, canonical/fresh-original-NSO RAW28 and actual
route/save/restore requirements remain in force; no old binary is patched.
Optimization may grow some functions while shrinking others. There is no CPU
or FPS promise until an ordinary matched actual game comparison is complete.
