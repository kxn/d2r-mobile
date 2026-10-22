# SDK free-span / page-present read-only capture

`run-debugger.py` and `sdk_free_resident.py` are byte-exact copies of the reviewed local capture v3. They reuse the accepted `4984509` owned LLDB attach, identity, detach, TracerPid, server and forward cleanup. The caller retains ownership of game launch, original route, save2, stop and directory restoration.

The missing tool capability is decoding this exact original SDK's locked span partition and intersecting its whole free host pages with the two existing `/proc/PID/pagemap` views. This is read-only layout decoding and official tool orchestration, not a sampler, timer, allocator or decommit implementation. It makes no inferior calls and does not modify SDK/game memory. SDK offsets and proof preconditions are specific to the verified input; unknown layouts fail closed. Present pages are not a promise of safely reclaimable bytes.

Freeze from an exact committed checkout:

```sh
python3 scripts/dev/sdk-free-resident-v1/freeze-source.py --output /absolute/new/owned-directory
```

The new directory contains only the two capture sources and `tool-inputs.json`: their hashes, source commit, and actual official LLDB/liblldb, NDK server, ADB and host Python file hashes. Freezing executes no ADB/LLDB command. It includes no NSO, keys, snapshots or private target configuration. Existing directories are refused. A later capture requires a separately pinned config and root-owned game transaction; freezing is not execution authorization.

The CLI remains `run-debugger.py OWNED_PID OUTDIR --config CONFIG --config-sha256 SHA [--plan]`. `--plan` verifies local inputs only. The config pins serial, APK/ELF/BuildID, canonical source-ready, tool-inputs, original SDK and flat SDK. Source freezing does not supply or publish those private inputs. `test_sdk_free_resident.py` reuses the original 22 synthetic decoder tests with a same-directory import, without any SDK binary or target snapshot.

The v3 sources retain the actual server-listen readiness fix and official `dd status=none` binary transport fix. The earlier tool-failure records under `.local/logs/sdk-free-resident-Q735-v1/` (`game-v2`, `game-v3`) remain historical evidence; this backup does not overwrite or relabel them. No successful free-resident conclusion is asserted by this source delivery. Existing natural-wait and bounded debugger-control deadlines are cleanup/readiness limits, not performance measurements.
