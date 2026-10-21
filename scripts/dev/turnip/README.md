# Turnip KGSL timestamp-wait classification

This directory archives one independent Mesa 26.2.5 diagnostic correction. It is
**not applied by `../build-turnip.py`**, included in an APK, linked into a driver,
or enabled in the current f830 CPU experiments.

`patches/mesa-26.2.5/0001-kgsl-classify-timestamp-wait-errors.patch` changes only
`wait_timestamp_safe()`:

- Successful waits and the existing EINTR/EAGAIN deadline retries stay unchanged.
- ETIMEDOUT still returns `VK_TIMEOUT`.
- Other ioctl failures log the original context, timestamp and errno, and return
  `VK_ERROR_DEVICE_LOST`. EPROTO must not be treated as an elapsed deadline,
  retried as a harmless interruption, or accepted as completed GPU work.

The actual A650 failure returned ioctl `-1`, errno 71 / EPROTO after a reported
GPU fault and command skipping. This correction reports that failure accurately;
it **does not fix the GPU hang or permit rendering to continue after skipped
commands**. The helper has no `vk_device` pointer and does not itself latch a
device-lost flag. The existing fence caller separately checks KGSL reset status.

## Exact source and patch

The archived patch is qualified against the existing f830 producer TU, not an
arbitrary tree labelled Mesa 26.2.5. No full Mesa source is copied here.

| Input | SHA-256 |
| --- | --- |
| Original `tu_knl_kgsl.cc` | `691fcd1f5b90643da3165c3329fd939a86d662646a7802845a1578c1a5ed48c7` |
| Patch | `6cc3ce6166fb95689ab2abf938dd958fa6ef48ceac037da0096625d924be2f24` |
| Patched `tu_knl_kgsl.cc` | `f50908760fe3152038422fcb8a85d6933425ea921b331631edec87f3202ff9fb` |
| Existing f830 driver, unchanged | `f830285fe68f0a744792269da5f160ed8636c172b1f6511db9d99478e1a2acce` |

The test runner refuses another source hash or a fuzzy port. Keep any later
driver build separate, with its complete producer record and a distinct driver
identity. Do not relabel a changed SO as f830.

## Reproduce offline tests

Supply an existing original f830 Mesa tree and a new output directory:

```sh
python3 scripts/dev/turnip/test-wait-classification.py \
  --mesa-source /path/to/f830/mesa-26.2.5 \
  --out .local/build/turnip-wait-classification-test
```

The runner uses `patch --fuzz=0` to write **one copied TU outside the source tree**,
then extracts the actual `get_relative_ms()` and `wait_timestamp_safe()` functions.
The test wraps only ioctl, logging and a scripted fake clock. It is not a GPU
timer, sampler, PM4 decoder or substitute graphics implementation.

The host test uses GCC ASan+UBSan and covers 13 cases: success, ETIMEDOUT, six
unexpected errno values including EPROTO/EDEADLK, decreasing retry deadlines,
deadline expiry, EPROTO after EINTR, zero-time polling with EPROTO, and an infinite
wait. A separate original-release negative reference reproduces the incorrect
EPROTO-to-TIMEOUT result. Logging deliberately changes errno to check that the
captured error is retained in the report.

Optionally compile the patched **single production TU** with the original NDK
producer command; this emits a relocatable object and dependency hashes only:

```sh
python3 scripts/dev/turnip/test-wait-classification.py \
  --mesa-source /path/to/f830/mesa-26.2.5 \
  --compile-commands /path/to/f830/mesa-kgsl-build/compile_commands.json \
  --out .local/build/turnip-wait-classification-ndk-test
```

No command above links a SO, builds an APK, downloads software, uses ADB, or
changes the supplied Mesa tree. Sanitizer and compiler dependencies must already
be installed. The output directory must be new or empty to retain prior results.

## Qualification boundary

On 2026-10-21, the original-parameter NDK AArch64 TU compile and both host tests
passed. The 1,090 unchanged compiler inputs matched the original f830 producer;
only the patched TU differed. The local source qualification receipt is
`.local/build/tablet-f830-wait-error-classification-v1/qualified-source.json`
(SHA-256 `e38951556ac7f47eaa4d020af256f5735dad17977f35eefb9d4771ffb02ca605`).

The independent dependency comparison receipt has SHA-256
`2ab1457c5abe88cca0e6e5d85a60cc40c2d85305f75d823d1ef30b7f8ac68d4a`.
These source/TU/function tests are not qualification of a newly linked driver,
hardware recovery, device-lost latching, game stability, or improved performance.
