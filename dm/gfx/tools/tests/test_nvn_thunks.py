#!/usr/bin/env python3
"""Host-only policy/lifecycle and LLVM AArch64 ABI checks; no CPU emulation."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


def run(argv, **kwargs):
    return subprocess.check_output([str(a) for a in argv], text=True, **kwargs)


def function(source, name):
    # Compile the actual production function, not a second implementation.
    start = re.search(r"^static void \*?" + name + r"\(", source, re.M).start()
    end = source.index("\n}", start) + 2
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", required=True, help="NDK LLVM clang")
    parser.add_argument("--objdump", required=True, help="matching LLVM objdump")
    parser.add_argument("--host-cc", default="cc")
    args = parser.parse_args()
    gfx = Path(__file__).resolve().parents[2]
    source = (gfx / "nvn/hle_nvn.c").read_text()
    names = (gfx / "nvn/names.txt").read_text().splitlines()
    assert len(names) == 523
    with tempfile.TemporaryDirectory(prefix="nvn-thunk-test-") as td:
        temp = Path(td)
        run(["python3", gfx / "tools/gen_protos.py", gfx / "nvn/include/nvn.h",
             gfx / "nvn/names.txt", temp / "nvn_protos.h"])
        for filename in ("nvn_protos.h", "nvn_thunks.S", "nvn_count.h"):
            assert (temp / filename).read_bytes() == (gfx / "nvn" / filename).read_bytes(), filename
        # LLVM checks every actual generated entry, including slot number and scratch ABI.
        for app in (False, True):
            obj = temp / ("app.o" if app else "cli.o")
            run([args.clang, "--target=aarch64-linux-android28", "-c", "-I", gfx / "nvn",
                 *(["-DDM_ANDROID_APP=1"] if app else []), gfx / "nvn/nvn_thunks.S", "-o", obj])
            text = run([args.objdump, "-d", "--no-show-raw-insn", obj])
            entries = dict(re.findall(r"^[0-9a-f]+ <(nvnth_\w+)>:\n(.*?)(?=\n\n|\Z)", text, re.M | re.S))
            assert len(entries) == len(names) * (2 if app else 1)
            for i in range(len(names)):
                for fast in ((False, True) if app else (False,)):
                    body = entries[f"nvnth_{'fast_' if fast else ''}{i}"]
                    ins = [line.split(":", 1)[1].split("//", 1)[0].strip() for line in body.splitlines() if ":" in line]
                    expected = ["adrp", "add", "mov", "add"] + ([] if fast else ["mov", "stadd"]) + ["ldr", "br"]
                    assert [line.split()[0] for line in ins] == expected, (i, fast, ins)
                    assert re.fullmatch(r"mov\s+x17, #(0x[0-9a-f]+|[0-9]+)", ins[2])
                    assert int(ins[2].split("#")[1], 0) == i * 16
                    assert ins[3] == "add\tx16, x16, x17"
                    assert ins[-2] == "ldr\tx17, [x16, #0x8]"
                    assert ins[-1] == "br\tx17"
                    assert not re.search(r"\b(?:sp|x(?:[0-9]|1[0-5]|1[89]|2[0-9]|30)|[qvd][0-9]+)\b", "\n".join(ins))
                    if not fast:
                        assert ins[4:] == ["mov\tx17, #0x1", "stadd\tx17, [x16]", "ldr\tx17, [x16, #0x8]", "br\tx17"]
            relocs = run([args.objdump, "-r", obj])
            table = re.findall(r"^([0-9a-f]+) R_AARCH64_ABS64\s+(nvnth_\w+)$", relocs, re.M)
            expected_table = [(i * 8, f"nvnth_{i}") for i in range(len(names))]
            if app:
                expected_table += [((len(names) + i) * 8, f"nvnth_fast_{i}") for i in range(len(names))]
            assert [(int(offset, 16), name) for offset, name in table] == expected_table
            syms = run([args.objdump, "-t", obj])
            assert "nvn_thunk_fast" in syms if app else "nvn_thunk_fast" not in syms
        # Actual dispatch + frame reporting code, with inert platform dependencies.
        harness = r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#define NVN_NFUNC 523
#define DM_MEM_CAP_DEFAULT_GB 0
typedef struct { _Atomic uint64_t cnt; void *fn; } nvn_slot_t;
_Static_assert(sizeof(nvn_slot_t)==16, "slot ABI");
nvn_slot_t nvn_slot[NVN_NFUNC];
void *nvn_thunk[NVN_NFUNC], *nvn_thunk_fast[NVN_NFUNC];
static void **g_thunks=nvn_thunk;
static int g_call_counts=1;
static uint64_t g_prev[NVN_NFUNC], g_frame;
static FILE *g_tsv;
static unsigned dumps, ticks, memory, count_logs, g_nsem;
static int g_log;
typedef struct { const char *name; void *fn; } nvn_impl;
static void nvn_unimpl_entry(void) {}
__attribute__((section("swnvn"),used)) static const nvn_impl test_impl={"nvnTest0",nvn_unimpl_entry};
static char name_storage[NVN_NFUNC][32];
static const char *nvn_names[NVN_NFUNC];
static void test_log(const char *format, ...) {
    if(strstr(format,"次调用")) count_logs++;
}
#define LOG(...) test_log(__VA_ARGS__)
#define die(...) abort()
static void cs_dump(void) { dumps++; }
static void dm_guest_frame_tick(void) { ticks++; }
static double nvn_resident_mb(void) { return 1; }
int dm_guest_heap_stat(unsigned long long *a,unsigned long long *b,unsigned long long *c) { *a=*b=*c=0; return 0; }
void mtl_mem_stat(unsigned long long a,unsigned long long b,int c,unsigned long long d,unsigned long long e,unsigned long long f) { memory++; }
'''
        harness += "\n".join(function(source, name) for name in ("nvn_init_dispatch", "nvn_init_slots", "nvn_lookup", "dump_frame"))
        harness += r'''
int main(int argc,char **argv) {
    assert(argc==2); int expected=atoi(argv[1]);
    for(unsigned i=0;i<NVN_NFUNC;i++) {
        snprintf(name_storage[i],32,"nvnTest%u",i); nvn_names[i]=name_storage[i]; nvn_slot[i].cnt=999;
        nvn_thunk[i]=(void *)(uintptr_t)(i*2+1); nvn_thunk_fast[i]=(void *)(uintptr_t)(i*2+2);
    }
    nvn_lookup(nvn_names[0]);
    assert(g_call_counts==expected);
    assert(g_thunks==(expected?nvn_thunk:nvn_thunk_fast));
    void **selected=g_thunks;
    setenv("DM_NVN_PROF","late-change",1);
    for(unsigned i=0;i<NVN_NFUNC;i++) assert(nvn_lookup(nvn_names[i])==selected[i]);
    assert(g_thunks==selected); /* Published dispatch cannot change per call. */
    dump_frame(); assert(g_frame==1 && dumps==1 && ticks==1);
    assert(g_prev[0]==(expected?999:0));
    assert(count_logs==(expected?1:0));
    g_frame=299; dump_frame();
    assert(g_frame==300 && dumps==2 && ticks==2 && memory==1);
    if(g_tsv) { fflush(g_tsv); fclose(g_tsv); }
    return 0;
}
'''
        (temp / "policy.c").write_text(harness)
        clean = {k: v for k, v in os.environ.items() if not k.startswith("DM_")}
        cases = [{}, {"DM_NVN_FRAMES": "0"}, {"DM_NVN_FRAMES": "4"},
                 {"DM_NVN_PROF": "0"}, {"DM_NVN_TSV": str(temp / "calls.tsv")},
                 {"DM_NVN_TSV": str(temp / "timing.tsv"), "DM_NVN_TSV_CALLS": "0"},
                 {"DM_NVN_LOG": "1"}, {"DM_VULKAN_EXTERNAL_PROFILE": "1"}]
        for app in (False, True):
            exe = temp / ("app-policy" if app else "cli-policy")
            run([args.host_cc, "-std=c11", "-D_GNU_SOURCE", "-g", "-fsanitize=address,undefined",
                 *(["-DDM_ANDROID_APP=1"] if app else []), temp / "policy.c", "-o", exe])
            for env in cases:
                expected = not app or any(k in env for k in ("DM_NVN_FRAMES", "DM_NVN_TSV", "DM_NVN_PROF"))
                run([exe, str(int(expected))], env={**clean, **env})
        assert "nvnTest0\t999" in (temp / "calls.tsv").read_text()
        assert "nvnTest" not in (temp / "timing.tsv").read_text()
    print("PASS: generator roundtrip, 1569 LLVM entries, 16 actual-policy/lifecycle ASan+UBSan cases")


if __name__ == "__main__":
    main()
