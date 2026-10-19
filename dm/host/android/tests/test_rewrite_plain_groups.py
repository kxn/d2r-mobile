#!/usr/bin/env python3
"""Disassemble real emitter entries, including late-rule rollback and all suffix edges.

Only the offline producer is invoked: no guest instructions are executed or
interpreted here. The companion original NSO exercises these paths on ARM64.
"""
import ast
from itertools import combinations
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
import re

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'dm/tools/patch_nso.py'
FUNCTIONS = {
    'here','emit','stat','emit_key_addr','emit_slot_addr','B','STP','LDP',
    'STP_PRE','LDP_POST','LDRX','STRX','MRS_TPIDRRO','ADRP','ADDI','ADDR','BLR',
    'dis','regset','num','has18','regs_in','pick','subst','sp_delta','elf_sp_operand',
    'site_plain_elf','plain_group_member','site_plain_group','finish_plain_groups','put_branch',
}
tree = ast.parse(SOURCE.read_text(), filename=str(SOURCE))
definitions = [n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in FUNCTIONS]
assert {n.name for n in definitions} == FUNCTIONS
helpers = compile(ast.Module(body=definitions,type_ignores=[]),str(SOURCE),'exec')
md = Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail = True


def namespace():
    n = dict(struct=struct,re=re,combinations=combinations,md=md,
             patch=bytearray(),fix=[],stats={},IMG=bytearray(0x4000),
             PATCH_OFF=0x8000,KEY_PAGE_OFF=0x4000,KEY_INDEX={'K18':0},
             HOST_ELF=True,HOST_TLSDESC=True,HOST_DIRECT_GATE=True)
    exec(helpers,n); return n


with tempfile.TemporaryDirectory(prefix='d2r-x18-groups-') as directory:
    work = Path(directory)
    # Three real source opcodes, including W18 zero-extension and carry/Z flags.
    (work/'input.S').write_text('''.arch armv8.3-a
.text
adds w18,w18,#1
adc x18,x18,xzr
csel x18,x18,xzr,eq
add x18,sp,#16
adr x18,.
ldr x18,.
cbz x18,.
br x18
braa x18,x9
blraa x18,x9
mrs x18,tpidr_el0
msr tpidr_el0,x18
ldaxr x18,[x9]
stlxr w10,x18,[x9]
dc zva,x18
''')
    subprocess.run(['clang','--target=aarch64-none-elf','-c',str(work/'input.S'),'-o',str(work/'input.o')],check=True)
    subprocess.run(['llvm-objcopy','-O','binary','--only-section=.text',str(work/'input.o'),str(work/'input.bin')],check=True)
    original = list(md.disasm((work/'input.bin').read_bytes(),0x100))
    n=namespace()
    assert all(n['plain_group_member'](x) for x in original[:3])
    assert all(not n['plain_group_member'](x) for x in original[3:])

    def prepare():
        n=namespace();entries={}
        for i in range(3):
            seg=original[i:3]
            start=n['site_plain_group'](seg) if len(seg)>1 else n['site_plain_elf'](seg[0])
            entries[seg[0].address]={'start':start,'seg':seg}
            n['put_branch'](seg[0].address,start)
        return n,entries

    n,entries=prepare()
    byte_count=len(n['patch'])
    n['finish_plain_groups'](entries)
    assert len(n['patch'])==byte_count and not n['stats']
    layouts=[]
    for a,item in entries.items():
        start=item['start'];seg=item['seg']
        branch=list(md.disasm(n['IMG'][a:a+4],a))[0]
        assert branch.mnemonic=='b' and branch.operands[0].imm==start
        end=next((v['start'] for k,v in entries.items() if k>a),n['PATCH_OFF']+len(n['patch']))
        code=list(md.disasm(bytes(n['patch'][start-n['PATCH_OFF']:end-n['PATCH_OFF']]),start))
        assert code[-1].mnemonic=='b' and code[-1].operands[0].imm==seg[-1].address+4
        assert sum(i.mnemonic=='blr' for i in code)==1
        if len(seg)>1:
            assert len(code)==22+2*len(seg)
            assert code[0].op_str=='x0, x1, [sp, #-0x20]!'
            assert code[-2].op_str=='x0, x1, [sp], #0x20'
            body=code[-2-2*len(seg):-2]
            for i,src in enumerate(seg):
                # Actual native opcode is the producer's verified substitution.
                assert bytes(body[2*i].bytes)==struct.pack('<I',n['subst'](src,0))
                assert body[2*i+1].mnemonic=='str' and body[2*i+1].op_str=='x0, [x1]'
        layouts.append({'entry':hex(a),'source_instructions':len(seg),'emitted_instructions':len(code),
                        'native_TLS_resolutions':1,'keeps_own_entry':True})

    # A late observation/parameter-like override inside the suffix must win.
    failures=[]
    for offset,word in ((0x104,0xd503201f),(0x108,0xd503201f),(0x104,n['B'](0x104,0x3000))):
        n,entries=prepare()
        old_start=entries[0x100]['start'];struct.pack_into('<I',n['IMG'],offset,word)
        n['finish_plain_groups'](entries)
        assert struct.unpack_from('<I',n['IMG'],offset)[0]==word
        assert entries[0x100]['start']!=old_start
        st=entries[0x100]['start'];code=list(md.disasm(bytes(n['patch'][st-n['PATCH_OFF']:]),st))
        assert code[-1].mnemonic=='b' and code[-1].operands[0].imm==0x104
        assert n['stats']['plain_group_fallback']>=1
        failures.append({'overridden_member':hex(offset),'late_word_preserved':True,'head_falls_back':True})

    # A late operation immediately after the suffix is reached, not skipped.
    n,entries=prepare();n['put_branch'](0x10c,0x3000);before=len(n['patch'])
    n['finish_plain_groups'](entries)
    assert len(n['patch'])==before
    assert struct.unpack_from('<I',n['IMG'],0x10c)[0]==n['B'](0x10c,0x3000)

    # Exercise the actual startup option and complete NSO producer. Temporary
    # outputs intentionally do not replace the previously hardware-tested pack.
    subprocess.run([sys.executable,str(Path(__file__).with_name('make_fixture.py')),str(work)],
                   check=True,stdout=subprocess.PIPE,text=True)
    source_image=(work/'fixture.bin').read_bytes()
    option_outputs={};option_layouts=[]
    for abi in ('elfdesc','elfdesc-gate'):
        for limit in (1,2,3):
            target=work/f'{abi}-{limit}'
            env=dict(os.environ,DM_HOST_ABI=abi,DM_X18_GROUP_SIZE=str(limit),DM_RULESET='native')
            subprocess.run([sys.executable,str(SOURCE),str(work/'fixture.nso'),str(target),'fixture'],
                           env=env,check=True,stdout=subprocess.PIPE,text=True)
            image=(target/'fixture.patched.bin').read_bytes()
            option_outputs[(abi,limit)]=image
            headers=dict(line[2:].split('\t') for line in (target/'fixture.fixups.tsv').read_text().splitlines())
            groups=[];single_exact=0;entries_checked=0
            for line in (target/'fixture.sites.tsv').read_text().splitlines():
                fields=line.split('\t')
                if len(fields)!=2 or fields[1]!='x18':continue
                a=int(fields[0],0);ins=next(md.disasm(source_image[a:a+4],a))
                if not namespace()['plain_group_member'](ins):continue
                entry=next(md.disasm(image[a:a+4],a))
                assert entry.mnemonic=='b'
                st=entry.operands[0].imm
                code=[]
                for emitted in md.disasm(image[st:st+160],st):
                    code.append(emitted)
                    if emitted.mnemonic=='b':break
                assert code[-1].mnemonic=='b'
                count=(code[-1].operands[0].imm-a)//4
                assert 1<=count<=(limit if abi=='elfdesc-gate' else 1)
                if count>1:groups.append(count)
                else:
                    # Limit1 must use the original template byte-for-byte at
                    # its real address, including scratch selection and ADRP.
                    expected=namespace();expected['PATCH_OFF']=st
                    expected['KEY_PAGE_OFF']=int(headers['KEY_PAGE_OFF'],0)
                    expected['site_plain_elf'](ins)
                    assert image[st:st+len(expected['patch'])]==bytes(expected['patch'])
                    single_exact+=1
                entries_checked+=1
            if abi=='elfdesc-gate':
                assert (not groups) if limit==1 else max(groups)==limit
            else:assert not groups
            option_layouts.append({'abi':abi,'limit':limit,'source_entries_checked':entries_checked,
                                   'exact_old_single_entries':single_exact,'group_lengths':groups})
    assert option_outputs[('elfdesc',1)]==option_outputs[('elfdesc',2)]==option_outputs[('elfdesc',3)]
    target=work/'default';env=dict(os.environ,DM_HOST_ABI='elfdesc-gate',DM_RULESET='native')
    env.pop('DM_X18_GROUP_SIZE',None)
    subprocess.run([sys.executable,str(SOURCE),str(work/'fixture.nso'),str(target),'fixture'],
                   env=env,check=True,stdout=subprocess.PIPE,text=True)
    assert (target/'fixture.patched.bin').read_bytes()==option_outputs[('elfdesc-gate',3)]
    for invalid in ('','0','4','-1','02','x'):
        target=work/f'invalid-{invalid}'
        env=dict(os.environ,DM_HOST_ABI='elfdesc-gate',DM_X18_GROUP_SIZE=invalid)
        failed=subprocess.run([sys.executable,str(SOURCE),str(work/'fixture.nso'),str(target),'fixture'],
                              env=env,capture_output=True,text=True)
        assert failed.returncode!=0 and 'DM_X18_GROUP_SIZE' in failed.stderr and not target.exists()

print(json.dumps({'result':'passed','suffix_layouts':layouts,'late_rule_negative_cases':failures,
                  'sp_pc_control_system_atomic_negative_instructions':len(original)-3,
                  'startup_option_complete_producer':option_layouts,
                  'startup_default_is_three':True,'non_ABI3_images_byte_identical':True,
                  'invalid_startup_values_rejected':6,
                  'per_instruction_K18_publication':True,'guest_execution':False,
                  'real_game_acceptance':False}))
