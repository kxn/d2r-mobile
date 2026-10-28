"""Boundary tests for the explicit scheduler transition; no device/payload scanning."""
from pathlib import Path
import copy,hashlib,json,subprocess,tempfile,unittest,re
import scheduler_route as m
R=Path(__file__).resolve().parents[3]
class Contract(unittest.TestCase):
 @classmethod
 def setUpClass(c):
  a=R/'.local/build/turnip-internal-queue-sync-v1';b=R/'.local/build/turnip-memory-critical-schedule-v3';c.args=[json.load(open(a/'build.json')),json.load(open(b/'build.json')),json.load(open(a/'candidate-source-ledger.json')),json.load(open(b/'candidate-source-ledger.json'))]
 def reject_change(self,fn):
  args=copy.deepcopy(self.args);fn(args)
  with self.assertRaises(ValueError):m.source_contract(*args)
 def test_actual_source_contract(self):m.source_contract(*self.args)
 def test_incomplete_build(self):self.reject_change(lambda a:a[1].update(complete=False))
 def test_wrong_driver(self):self.reject_change(lambda a:a[1]['driver'].update(sha256='00'*32))
 def test_unrelated_image_change(self):self.reject_change(lambda a:a[3].update({'src/freedreno/vulkan/tu_image.cc':'00'*32}))
 def test_extra_source(self):self.reject_change(lambda a:a[3].update({'unexpected.c':'00'*32}))
 def test_environment(self):self.reject_change(lambda a:a[1]['declared_environment'].update(UNDECLARED='1'))
 def test_tool_change(self):self.reject_change(lambda a:a[1].update(tools={}))
 def test_each_actual_UUID_mismatch(self):
  q=json.load(open(R/'.local/build/native-bc3-linear-consumer-12b8-v1/compatibility.json'))['actual_UUIDs'];m.matching_uuids(q,q)
  for k in m.UUIDS:
   with self.subTest(k=k),self.assertRaises(ValueError):m.matching_uuids(q,{**q,k:'00'*16})
 def test_missing_actual_capture(self):
  with tempfile.TemporaryDirectory()as p,self.assertRaises(FileNotFoundError):m.query_from_compile(R,Path(p),'00'*32)
 def test_wrong_actual_capture_digest(self):
  with self.assertRaises(ValueError):m.query_from_compile(R,R/'.local/logs/fs-critical-schedule-v3-compile-v1/ACTUAL-COMPILE1','00'*32)
 def test_actual_C_provider_expressions(self):
  native=(R/'dm/gfx/vulkan/nvn_vk_native_resource_device.inc').read_text();lazy=(R/'dm/gfx/vulkan/nvn_vk_shared_pool.inc').read_text();arrays=lambda t:'\n'.join(re.findall(r'static const uint8_t\s+\w+\[32\]\s*=\s*\{[^}]+\};',t))
  ne=re.search(r'native_resident_driver=([^;]+);',native)[1];le=re.search(r'uint8_t digest\[32\];int provider=([^;]+);',lazy)[1];self.assertIn('coherent_turnip_memory_chain_v3',ne);self.assertIn('exact_memory_chain_v3',le)
  src='#include <stdint.h>\n#include <string.h>\n#include <assert.h>\nstatic uint8_t input[32];static int authenticated;\nstatic int vk_loader_native_driver_digest(uint8_t d[32]){memcpy(d,input,32);return authenticated;}\n'
  src+='static int resident(const uint8_t d[32]){struct{uint8_t driver_sha256[32];}identity;memcpy(identity.driver_sha256,d,32);'+arrays(native)+'\nreturn '+ne+';}\n'
  src+='static int lazy(const uint8_t d[32],int auth){memcpy(input,d,32);authenticated=auth;'+arrays(lazy)+'\nuint8_t digest[32];return '+le+';}\n'
  vec=lambda h:'{'+','.join('0x'+h[i:i+2]for i in range(0,64,2))+'}'
  src+='int main(void){uint8_t fresh[32]='+vec(m.NEW)+';uint8_t old[32]='+vec(m.OLD)+';uint8_t raw[32]='+vec('82f29febf953f9e3c4891ce3d793c171366af965ef1b25a2fbdb1738d725f3cb')+';assert(resident(fresh)&&lazy(fresh,1)&&!lazy(fresh,0));assert(resident(old)&&lazy(old,1));assert(resident(raw)&&lazy(raw,1));for(unsigned n=0;n<256;n++){uint8_t b[32];memcpy(b,fresh,32);b[n/8]^=1u<<(n%8);assert(!resident(b)&&!lazy(b,1));}return 0;}\n'
  with tempfile.TemporaryDirectory()as t:
   p=Path(t);(p/'provider.c').write_text(src);subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'provider.c'),'-o',str(p/'check')],check=True,capture_output=True);subprocess.run([str(p/'check')],check=True)
if __name__=='__main__':unittest.main()
