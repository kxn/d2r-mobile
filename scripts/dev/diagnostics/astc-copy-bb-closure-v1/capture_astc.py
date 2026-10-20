"""One exact bb4 ASTC refusal stop; official typed metadata, no inferior calls."""
import json
import os
from pathlib import Path
import lldb

PC = 0x33ACF0
IMAGES = 0xC88F98
POOLS = 0xC780C8
GLOBALS = {'shared_active': (0xC77FC0, 4), 'shared_cpu_observe_phase': (0xC77F80, 4),
           'shared_window_bytes': (0xC780F0, 8)}
BUILD_ID = '243f00c4491b21bc3bde4a7a582c163d2cf876b7'
OUT = Path(os.environ['DM_ASTC_COPY_OUTPUT'])
events, errors = [], []


def scalar(v):
    if not v.IsValid() or v.GetError().Fail():
        return {'error': str(v.GetError()) if v.IsValid() else 'unavailable'}
    return {'type': v.GetTypeName(), 'value': v.GetValue(), 'unsigned': v.GetValueAsUnsigned()}


def child(v, path):
    for name in path.split('.'):
        v = v.GetChildMemberWithName(name)
    return v


def fields(v, names):
    return {name: scalar(child(v, name)) for name in names}


def pointed(target, address, name):
    typ = target.FindFirstType(name)
    if not address or not typ.IsValid():
        raise ValueError('missing pointer/type: ' + name)
    return target.CreateValueFromAddress(name, lldb.SBAddress(address, target), typ)


def read(process, address, size):
    error = lldb.SBError()
    blob = process.ReadMemory(address, size, error)
    if error.Fail() or len(blob) != size:
        raise ValueError('ReadMemory %x/%d: %s' % (address, size, error))
    return bytes(blob)


def member_layout(typ, path):
    offset = 0
    for name in path.split('.'):
        matches = [typ.GetFieldAtIndex(n) for n in range(typ.GetNumberOfFields())
                   if typ.GetFieldAtIndex(n).GetName() == name]
        if len(matches) != 1:
            raise ValueError('missing/nonunique struct field: ' + path)
        f = matches[0]
        offset += f.GetOffsetInBytes()
        typ = f.GetType()
    if not 0 < typ.GetByteSize() <= 8:
        raise ValueError('not a scalar field: ' + path)
    return offset, typ.GetByteSize()


def raw_fields(typ, blob, names):
    result = {}
    for name in names:
        offset, size = member_layout(typ, name)
        result[name] = int.from_bytes(blob[offset:offset+size], 'little')
    return result


IMAGE_FIELDS = ['tex.serial', 'tex.mem', 'tex.L.size', 'tex.has_layout',
    'tex.d.format', 'tex.d.target', 'tex.d.flags', 'tex.d.samples', 'tex.d.width',
    'tex.d.height', 'tex.d.depth', 'tex.d.levels', 'tex.L.layers', 'format', 'extent.width',
    'extent.height', 'extent.depth', 'levels', 'layers', 'aspect', 'initialized', 'decoded',
    'external_access', 'astc_copy_encoding', 'astc_copy_stamp', 'shared_dirty_scope',
    'shared_alias_linked', 'description.samples', 'description.usage', 'description.flags',
    'description.imageType', 'cpu_rebuildable', 'shared_writer', 'shared_writer_source',
    'handle', 'memory', 'native_entry', 'native_pool', 'access', 'last_use']
PARAM_FIELDS = ['src', 'dst', 'flags', 'has_sv', 'has_dv',
    'sv.set_levels', 'sv.base_level', 'sv.nlevels', 'sv.set_layers', 'sv.min_layer', 'sv.nlayers',
    'sv.set_format', 'sv.format', 'sv.set_target', 'sv.target',
    'dv.set_levels', 'dv.base_level', 'dv.nlevels', 'dv.set_layers', 'dv.min_layer', 'dv.nlayers',
    'dv.set_format', 'dv.format', 'dv.set_target', 'dv.target',
    'sr.xoffset', 'sr.yoffset', 'sr.zoffset', 'sr.width', 'sr.height', 'sr.depth',
    'dr.xoffset', 'dr.yoffset', 'dr.zoffset', 'dr.width', 'dr.height', 'dr.depth']


def image_cells(target, process, d):
    typ = target.FindFirstType('image_access')
    levels, layers = child(d, 'levels').GetValueAsUnsigned(), child(d, 'layers').GetValueAsUnsigned()
    count = levels * layers * 3
    if not typ.IsValid() or not 0 < count <= 262144:
        return {'complete': False, 'count': count}
    stride = typ.GetByteSize()
    blob = read(process, child(d, 'access').GetValueAsUnsigned(), count * stride)
    names = ['writers', 'write_clock', 'writes', 'readers', 'raster_pass', 'mip_layout', 'mip_scope']
    active = []
    for n in range(count):
        cell = raw_fields(typ, blob[n*stride:(n+1)*stride], names)
        if cell['writers'] or cell['write_clock'] or cell['writes']:
            active.append({'index': n, **cell})
    return {'complete': True, 'cells': count, 'stride': stride, 'freshness_blocking_cells': active}


def aliases(target, process, slide, d, dst_pointer):
    base, size = child(d, 'tex.mem').GetValueAsUnsigned(), child(d, 'tex.L.size').GetValueAsUnsigned()
    typ = target.FindFirstType('image')
    names = ['next', 'tex.serial', 'tex.mem', 'tex.L.size', 'initialized', 'external_access',
             'shared_alias_linked', 'astc_copy_encoding', 'format', 'last_use']
    current = int.from_bytes(read(process, slide + IMAGES, 8), 'little')
    visited, overlap = set(), []
    while current:
        if current in visited or len(visited) >= 32768:
            return {'complete': False, 'visited': len(visited), 'overlaps': overlap}
        visited.add(current)
        value = raw_fields(typ, read(process, current, typ.GetByteSize()), names)
        begin, length = value['tex.mem'], value['tex.L.size']
        if current != dst_pointer and begin and length and base < begin + length and begin < base + size:
            overlap.append({'pointer': current, **value,
                'freshness_blocking': bool(value['initialized'] or value['external_access'] or value['shared_alias_linked'])})
        current = value['next']
    return {'complete': True, 'visited': len(visited), 'overlaps': overlap}


def pools(target, process, slide, address, size):
    current = int.from_bytes(read(process, slide + POOLS, 8), 'little')
    seen, result = set(), []
    while current:
        if current in seen or len(seen) >= 1024:
            raise ValueError('pool list cycle/cap; incomplete metadata')
        seen.add(current)
        p = pointed(target, current, 'pool')
        base, length = child(p, 'guest').GetValueAsUnsigned(), child(p, 'size').GetValueAsUnsigned()
        if address < base + length and base < address + size:
            row = {'pointer': current, 'fields': fields(p, ['guest', 'size', 'identity', 'shared',
                'shared_closing', 'backings', 'references', 'nvn_flags', 'nvn_flags_known'])}
            span = child(p, 'backings').GetValueAsUnsigned()
            spans, walked = [], set()
            while span:
                if span in walked or len(walked) >= 4096:
                    raise ValueError('span list cycle/cap')
                walked.add(span)
                b = pointed(target, span, 'pool_backing_span')
                spans.append({'pointer': span, 'fields': fields(b, ['begin', 'end', 'generation', 'buffer.handle', 'buffer.map', 'buffer.size'])})
                span = child(b, 'next').GetValueAsUnsigned()
            row['spans'] = spans
            result.append(row)
        current = child(p, 'next').GetValueAsUnsigned()
    return result


def save():
    (OUT / 'astc-copy-stops.json').write_text(json.dumps({'events': events, 'errors': errors,
        'capture_protocol': 'astc-copy-bb4-rejected-v1',
        'api': 'Official LLDB SBValue/SBType/ReadMemory only; no expressions or inferior calls',
        'scope': 'One actual refused operation; not sampling/performance or a successful copy'}, indent=2) + '\n')


def capture(frame, bp_loc, _dict):
    thread = frame.GetThread()
    process, target = thread.GetProcess(), thread.GetProcess().GetTarget()
    slide = int(os.environ['DM_ASTC_COPY_SLIDE'], 16)
    row = {'kind': 'astc_copy_refusal', 'PID': process.GetProcessID(), 'TID': thread.GetThreadID(),
           'pc': hex(frame.GetPC()), 'registers': {n: scalar(frame.FindRegister(n)) for n in
               ['x19', 'x20', 'x21', 'x22', 'x23', 'x26', 'sp', 'fp', 'lr']}}
    try:
        if frame.GetPC() != slide + PC:
            raise ValueError('not frozen refusal PC')
        reg = lambda n: frame.FindRegister(n).GetValueAsUnsigned()
        p, s, d = pointed(target, reg('x20'), 'p_copy_t2t'), pointed(target, reg('x22'), 'image'), pointed(target, reg('x21'), 'image')
        row['parameters'] = fields(p, PARAM_FIELDS)
        row['source'], row['destination'] = fields(s, IMAGE_FIELDS), fields(d, IMAGE_FIELDS)
        row['packaged_layout_bytes'] = {name: read(process, child(image, 'tex.layout').GetLoadAddress(), 8).hex() for name, image in [('source', s), ('destination', d)]}
        # Exact frozen prologue reserves0x1b0, x26=sp+0xb8. The immutable
        # observation is copied to [x26+0x18], i.e. SP+0xd0. No source evaluation.
        row['observation'] = fields(pointed(target, reg('sp') + 0xD0, 'astc_copy_observation'), ['scope', 'stamp', 'legacy'])
        row['cell_history'] = image_cells(target, process, d)
        row['alias_scan'] = aliases(target, process, slide, d, reg('x21'))
        row['pools'] = pools(target, process, slide, child(d, 'tex.mem').GetValueAsUnsigned(), child(d, 'tex.L.size').GetValueAsUnsigned())
        row['startup_globals'] = {name: int.from_bytes(read(process, slide+addr, size), 'little')
                                  for name, (addr, size) in GLOBALS.items()}
        row['backtrace'] = [{'index': f.GetFrameID(), 'function': f.GetDisplayFunctionName(),
            'pc': hex(f.GetPC()), 'file': str(f.GetLineEntry().GetFileSpec()), 'line': f.GetLineEntry().GetLine(),
            'small_variables': {v.GetName(): scalar(v) for v in f.GetVariables(True, True, False, True)
                                if v.GetName() and v.GetType().GetByteSize() <= 16}} for f in thread]
    except BaseException as e:
        row['read_error'] = repr(e)
        errors.append(repr(e))
    events.append(row)
    save()
    bp_loc.GetBreakpoint().SetEnabled(False)
    return True  # Original hook batch immediately detaches; game follows original failure.


def install(debugger):
    target = debugger.GetSelectedTarget()
    if target.GetModuleAtIndex(0).GetUUIDString().replace('-', '').lower() != BUILD_ID:
        raise ValueError('exact bb4 BuildID required')
    if target.GetProcess().GetProcessID() != int(os.environ['DM_ASTC_COPY_PID']):
        raise ValueError('not caller-owned PID')
    slide = int(os.environ['DM_ASTC_COPY_SLIDE'], 16)
    bp = target.BreakpointCreateByAddress(slide + PC)
    bp.SetScriptCallbackFunction(__name__ + '.capture')
    save()
