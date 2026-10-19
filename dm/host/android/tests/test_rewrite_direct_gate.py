#!/usr/bin/env python3
"""Check actual emitted ARM64 gates and genuine NSO rewrites, without executing a CPU.

Capstone verifies concrete operands/edges; native fixture execution belongs on
ARM64 hardware. This does not emulate instructions or replace game acceptance.
"""
import ast
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'dm/tools/patch_nso.py'
FUNCTIONS = {
    'here', 'emit', 'emit_key_addr', 'emit_slot_addr', 'B', 'STP', 'LDP',
    'STP_PRE', 'LDP_POST', 'LDRX', 'STRX', 'MRS_TPIDRRO', 'ADRP', 'ADDI',
    'ADDR', 'BLR', 'emit_gate_call', 'site_gate',
}
tree = ast.parse(SOURCE.read_text(), filename=str(SOURCE))
definitions = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in FUNCTIONS]
assert {n.name for n in definitions} == FUNCTIONS
helpers = compile(ast.Module(body=definitions, type_ignores=[]), str(SOURCE), 'exec')
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True


def emit_gate(abi, start, key_page, kind, arg):
    namespace = dict(struct=struct, patch=bytearray(), fix=[], gates=[],
                     PATCH_OFF=start, KEY_PAGE_OFF=key_page,
                     KEY_INDEX={'KGATE': 3}, HOST_GATE_IDX=8,
                     HOST_ELF=True, HOST_TLSDESC=True,
                     HOST_DIRECT_GATE=abi == 'elfdesc-gate')
    exec(helpers, namespace)
    original = 0x12340
    namespace['site_gate'](SimpleNamespace(address=original), kind, arg)
    instructions = list(md.disasm(bytes(namespace['patch']), start))
    assert instructions[0].mnemonic == 'stp'
    assert instructions[0].op_str == 'x29, x30, [sp, #-0x10]!'
    assert instructions[-2].mnemonic == 'ldp'
    assert instructions[-2].op_str == 'x29, x30, [sp], #0x10'
    assert instructions[-1].mnemonic == 'b'
    assert instructions[-1].operands[0].imm == original + 4
    ret, actual_kind, actual_arg, actual_orig = namespace['gates'][0]
    assert (actual_kind, actual_arg, actual_orig) == (kind, arg, original)
    assert instructions[-3].address + 4 == ret == instructions[-2].address
    if abi == 'elfdesc-gate':
        assert len(instructions) == 6
        assert [i.mnemonic for i in instructions] == ['stp', 'adrp', 'ldr', 'blr', 'ldp', 'b']
        page, load, call = instructions[1:4]
        assert page.op_str.split(',')[0] == 'x30'
        assert page.operands[1].imm == key_page
        assert load.op_str == 'x30, [x30, #0x40]'
        assert call.op_str == 'x30'
    else:
        assert len(instructions) == 25
        assert sum(i.mnemonic == 'blr' for i in instructions) == 2
        # The old resolver path still restores the original guest frame pointer.
        assert instructions[-4].op_str == 'x29, [sp]'
    return {'abi': abi, 'start': hex(start), 'key_page': hex(key_page),
            'kind': kind, 'arg': arg, 'instructions': len(instructions), 'ret': hex(ret)}


layouts = [emit_gate(abi, start, page, kind, arg)
           for abi in ('elfdesc', 'elfdesc-gate')
           for start, page in ((0x400ffc, 0x3f8000), (0x200000, 0x204000), (0x1fff80, 0x4000))
           for kind, arg in (('svc', 0x1e), ('cntpct', 9), ('cntpct', 23), ('obs', 6))]

# True per-thread state is deliberately not optimized. Prove byte equality of
# every original TLS lookup, not only the flag used to select its algorithm.
tls_lookups = []
for key_index, key in enumerate(('K18', 'KTLS', 'KTLR', 'KGATE', 'KRES0', 'KRES1', 'KASPECT', 'KASPECTUI')):
    emitted = []
    for abi in ('elfdesc', 'elfdesc-gate'):
        namespace = dict(struct=struct, patch=bytearray(), fix=[], gates=[],
                         PATCH_OFF=0x400ffc, KEY_PAGE_OFF=0x3f8000,
                         KEY_INDEX={key: key_index}, HOST_GATE_IDX=8,
                         HOST_ELF=True, HOST_TLSDESC=True,
                         HOST_DIRECT_GATE=abi == 'elfdesc-gate')
        exec(helpers, namespace)
        namespace['emit_slot_addr'](16, 17, key)
        emitted.append(bytes(namespace['patch']))
    assert emitted[0] == emitted[1], (key, 'true TLS ABI changed')
    tls_lookups.append({'key': key, 'bytes': len(emitted[0]), 'byte_identical': True})

# A real input NSO traverses the complete producer, not only extracted helpers.
with tempfile.TemporaryDirectory(prefix='d2r-direct-gate-test-') as directory:
    work = Path(directory)
    subprocess.run([sys.executable, str(Path(__file__).with_name('make_fixture.py')), str(work)],
                   check=True, stdout=subprocess.PIPE, text=True)
    output = {}
    for abi, number in (('elfdesc', 2), ('elfdesc-gate', 3)):
        target = work / abi
        environment = dict(os.environ, DM_HOST_ABI=abi, DM_RULESET='native')
        subprocess.run([sys.executable, str(SOURCE), str(work / 'fixture.nso'), str(target), 'fixture'],
                       env=environment, check=True, stdout=subprocess.PIPE, text=True)
        headers = dict(line[2:].split('\t') for line in (target / 'fixture.fixups.tsv').read_text().splitlines())
        assert int(headers['HOST_ABI'], 0) == number
        assert {headers['KEY_' + key + '_IDX'] for key in
                ('K18', 'KTLS', 'KTLR', 'KGATE', 'KRES0', 'KRES1', 'KASPECT', 'KASPECTUI')} == set(map(str, range(8)))
        assert (headers.get('HOST_GATE_IDX') == '8') == (number == 3)
        image = (target / 'fixture.patched.bin').read_bytes()
        gate_rows = [line.split('\t') for line in (target / 'fixture.gates.tsv').read_text().splitlines()]
        assert len(gate_rows) >= 4
        for ret, kind, arg, orig in gate_rows:
            ret, orig, arg = int(ret, 0), int(orig, 0), int(arg)
            if number == 3:
                code = list(md.disasm(image[ret - 16:ret + 8], ret - 16))
                assert [i.mnemonic for i in code] == ['stp', 'adrp', 'ldr', 'blr', 'ldp', 'b']
                assert code[1].operands[1].imm == int(headers['KEY_PAGE_OFF'], 0)
                assert code[2].op_str == 'x30, [x30, #0x40]'
                assert code[-1].operands[0].imm == orig + 4
        output[abi] = dict(gates=len(gate_rows), input_sha256=hashlib.sha256((work / 'fixture.nso').read_bytes()).hexdigest(),
                           image_sha256=hashlib.sha256(image).hexdigest())
    assert output['elfdesc']['input_sha256'] == output['elfdesc-gate']['input_sha256']

print(json.dumps({'result': 'passed', 'layouts': layouts, 'tls_lookups': tls_lookups, 'nso_outputs': output,
                  'guest_execution': False, 'real_game_acceptance': False}))
