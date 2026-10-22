"""Official LLDB offline SBData fixture against actual formal ELF DWARF.
Run inside LLDB with a local target only, never attach a device/process.
"""
import importlib.util
from pathlib import Path
import struct


def run(debugger):
    import lldb
    root = Path(__file__).resolve().parents[1]
    spec = importlib.util.spec_from_file_location('pool_fixture_module', root / 'sdk_nvn_pool_registry.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    t = debugger.GetSelectedTarget()
    assert not t.GetProcess().IsValid(), 'this fixture must be offline'
    p = t.FindGlobalVariables('pools', 100).GetValueAtIndex(0).GetType().GetPointeeType()
    fs = {p.GetFieldAtIndex(i).GetName(): p.GetFieldAtIndex(i) for i in range(p.GetNumberOfFields())}
    b = fs['backings'].GetType().GetPointeeType()
    bf = {b.GetFieldAtIndex(i).GetName(): b.GetFieldAtIndex(i) for i in range(b.GetNumberOfFields())}
    bt = bf['buffer'].GetType()
    vf = {bt.GetFieldAtIndex(i).GetName(): bt.GetFieldAtIndex(i) for i in range(bt.GetNumberOfFields())}

    def typed(typ, raw, name):
        d, e = lldb.SBData(), lldb.SBError()
        d.SetData(e, raw, lldb.eByteOrderLittle, 8)
        assert e.Success(), str(e)
        return t.CreateValueFromData(name, d, typ)

    def set_field(raw, f, value, base=0):
        fmt = {4: '<I', 8: '<Q'}[f.GetType().GetByteSize()]
        struct.pack_into(fmt, raw, base + f.GetOffsetInBytes(), value)

    raw = bytearray(p.GetByteSize())
    expect = dict(next=0x2000, guest=0x100000, size=8192, identity=9,
                  shared=1, shared_closing=0, native_allocations=0xabcdef,
                  shared_import_nodes=2, shared_import_bytes=8192)
    for name, value in expect.items():
        set_field(raw, fs[name], value)
    got = m.fields(typed(p, raw, 'offline_pool'), expect)
    assert got == expect, (got, expect)
    raw = bytearray(b.GetByteSize())
    for name, value in dict(next=0, begin=4096, end=8192, generation=7).items():
        set_field(raw, bf[name], value)
    expect_buffer = dict(handle=0x1234, memory=0x5678, allocation=0, map=0x101000,
                         size=4096, properties=15, shared_import=1)
    for name, value in expect_buffer.items():
        set_field(raw, vf[name], value, bf['buffer'].GetOffsetInBytes())
    value = typed(b, raw, 'offline_buffer')
    assert m.fields(value, ['begin', 'end', 'generation']) == dict(begin=4096, end=8192, generation=7)
    assert m.fields(m.member(value, 'buffer'), expect_buffer) == expect_buffer
    print('OFFICIAL_LLDB_SBData_TYPED_FIXTURE_PASS', p.GetByteSize(), b.GetByteSize(), bt.GetByteSize(), got, expect_buffer)
