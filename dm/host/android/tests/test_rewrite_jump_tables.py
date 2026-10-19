#!/usr/bin/env python3
"""Disassemble real rewrite helpers and prove every landing-table edge.

No guest instructions are executed or emulated. This checks emitted layout,
register restoration and branch destinations, including the TLSDESC expansion.
"""
import ast
import hashlib
import json
from pathlib import Path
import struct
from types import SimpleNamespace

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'dm/tools/patch_nso.py'
FUNCTIONS = {
    'here', 'emit', 'emit_key_addr', 'emit_slot_addr', 'B', 'STP', 'LDP',
    'STP_PRE', 'LDP_POST', 'STUR', 'LDUR', 'LDRX', 'STRX', 'MRS_TPIDRRO',
    'ADRP', 'ADDI', 'ADDR', 'SUBI', 'BR', 'BLR', 'site_br',
}
tree = ast.parse(SOURCE.read_text(), filename=str(SOURCE))
definitions = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in FUNCTIONS]
assert {n.name for n in definitions} == FUNCTIONS
helpers = compile(ast.Module(body=definitions, type_ignores=[]), str(SOURCE), 'exec')
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = True


def verify(abi, start, base, count):
    namespace = {
        'struct': struct, 'patch': bytearray(), 'fix': [], 'PATCH_OFF': start,
        'KEY_PAGE_OFF': start - 0x4000, 'KEY_INDEX': {'K18': 0},
        'HOST_ELF': abi != 'darwin', 'HOST_TLSDESC': abi in ('elfdesc', 'elfdesc-gate'),
        'NOP': 0xD503201F,
    }
    exec(helpers, namespace)
    assert namespace['site_br'](SimpleNamespace(address=base - 4), base, count) == start
    code = bytes(namespace['patch'])
    # BR x16 is the end of the prefix. The following words must be table
    # branches, then one restore-and-branch tail per original case.
    prefix = list(md.disasm(code[:256], start))
    dispatch = next(i for i in prefix if i.mnemonic == 'br' and i.op_str == 'x16')
    table = dispatch.address + 4
    while struct.unpack_from('<I', code, table - start)[0] == namespace['NOP']:
        table += 4
    adjustments = [i for i in prefix if i.address < dispatch.address
                   and i.mnemonic in ('add', 'sub') and i.op_str.startswith('x16, x16, #')]
    displacement = sum((1 if i.mnemonic == 'add' else -1)
                       * i.operands[2].imm * (1 << i.operands[2].shift.value)
                       for i in adjustments)
    assert base + displacement == table, (abi, hex(base + displacement), hex(table))
    if abi not in ('elfdesc', 'elfdesc-gate'):
        assert table == start + 96, (abi, 'legacy layout changed')
    tails = table + count * 4
    assert len(code) == tails - start + count * 8

    def branch_target(at):
        word = struct.unpack_from('<I', code, at - start)[0]
        assert word & 0xfc000000 == 0x14000000, (abi, hex(at), 'not B')
        immediate = word & 0x3ffffff
        if immediate & 0x2000000:
            immediate -= 0x4000000
        return at + immediate * 4

    restore = namespace['LDP_POST'](16, 17, 31, 16) if abi != 'darwin' else 0xF85F03F0
    for index in range(count):
        tail = tails + index * 8
        assert branch_target(table + index * 4) == tail, (abi, index, 'wrong restore tail')
        assert struct.unpack_from('<I', code, tail - start)[0] == restore, (abi, index, 'restore')
        assert branch_target(tail + 4) == base + index * 4, (abi, index, 'wrong original case')
    return {'abi': abi, 'cases': count, 'prefix_bytes': table - start,
            'displacement': displacement, 'every_case_correct': True,
            'start': hex(start), 'base': hex(base), 'code_sha256': hashlib.sha256(code).hexdigest()}


results = [verify(abi, start, base, count)
           for abi in ('darwin', 'elf', 'elfdesc', 'elfdesc-gate')
           for start, base in ((0x3000000, 0x7d0240), (0x100000, 0x7000000),
                               (0x3000000, 0x2fff060))
           for count in (256, 65536)]
for item in results:
    if item['abi'] == 'elfdesc-gate':
        old = next(r for r in results if r['abi'] == 'elfdesc' and
                   all(r[k] == item[k] for k in ('start', 'base', 'cases')))
        assert old['code_sha256'] == item['code_sha256'], 'ABI3 changed the TLS/jump template'
print(json.dumps({'result': 'passed', 'layouts': results,
                  'guest_execution': False, 'real_game_acceptance': False}))
