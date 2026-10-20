#!/usr/bin/env python3
"""Host-only harness around the unchanged official GFXR Android parser method.
No replacement capture parser, Android app, API tracing or GPU workload.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess
R=Path('/home/dl/d2r-mobile');S=R/'.local/tools/gfxreconstruct-source';P=argparse.ArgumentParser();P.add_argument('--output',type=Path,required=True);a=P.parse_args();D=a.output;D.mkdir(exist_ok=False,parents=True)
source=S/'framework/encode/capture_settings.cpp';header=S/'framework/encode/capture_settings.h';text=source.read_text();start=text.index('CaptureSettings::RuntimeTriggerState\nCaptureSettings::ParseAndroidRunTimeTrimState');end=text.index('\n}',start)+2;method=text[start:end]
h=header.read_text();es=h.index('    enum RuntimeTriggerState');ee=h.index('\n    };',es)+7;enum=h[es:ee]
license=text[:text.index('*/')+2]
cpp=license+'''\n#include "util/options.h"
#include "util/logging.h"
#include <cassert>
#include <cstdio>
namespace gfxrecon { namespace encode {
class CaptureSettings { public:
'''+enum+'''
static RuntimeTriggerState ParseAndroidRunTimeTrimState(const std::string&,RuntimeTriggerState);
};
const char kCaptureAndroidTriggerEnvVar[]="GFXRECON_CAPTURE_ANDROID_TRIGGER";
'''+method+'''
}}
int main(){ using C=gfxrecon::encode::CaptureSettings;
assert(C::ParseAndroidRunTimeTrimState("",C::kNotUsed)==C::kNotUsed);
assert(C::ParseAndroidRunTimeTrimState("false",C::kNotUsed)==C::kDisabled);
assert(C::ParseAndroidRunTimeTrimState("false",C::kNotUsed)!=C::kNotUsed);
assert(C::ParseAndroidRunTimeTrimState("",C::kDisabled)==C::kNotUsed);
assert(C::ParseAndroidRunTimeTrimState("true",C::kNotUsed)==C::kEnabled);
assert(C::ParseAndroidRunTimeTrimState("0",C::kNotUsed)==C::kDisabled);
assert(C::ParseAndroidRunTimeTrimState("",C::kEnabled)==C::kNotUsed);
puts("PASS7: unchanged official Androidtrigger parser; false/0=disabled, empty=notused; priorfalse is a real negative for untrimmed startup");
}
'''
p=D/'official-trigger-test.cpp';p.write_text(cpp);lib=R/'.local/build/gfxreconstruct-host/framework/util/libgfxrecon_util.a';cmd=['c++','-std=c++20','-O2','-I'+str(S/'framework'),str(p),str(lib),'-ldl','-pthread','-o',str(D/'official-trigger-test')]
with(D/'compile.log').open('w')as out:subprocess.run(cmd,stdout=out,stderr=subprocess.STDOUT,check=True)
with(D/'test.log').open('w')as out:subprocess.run([str(D/'official-trigger-test')],stdout=out,stderr=subprocess.STDOUT,check=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();row=lambda p:{'path':str(p),'sha256':sha(p)}
r={'scope':'unchanged official method + actual util ParseBoolString library; host-only no GPU/ADB/runtime change; declaration harness only','correlated_source_revision':'280a0ac; not claiming exact ArmSDK2bb4438 source','source':row(source),'header':row(header),'linked_official_util':row(lib),'extracted_method_SHA256':hashlib.sha256(method.encode()).hexdigest(),'compile':cmd,'artifacts':[row(q)for q in(p,D/'official-trigger-test',D/'compile.log',D/'test.log')],'checks':7,'result':'passed'}
(D/'report.json').write_text(json.dumps(r,indent=2)+'\n');print((D/'test.log').read_text().strip())
