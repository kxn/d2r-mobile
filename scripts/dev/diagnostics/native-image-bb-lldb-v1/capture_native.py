"""Two finite official LLDB stops; SBValue/ReadMemory only, never target calls."""
import json
import os
from pathlib import Path

import lldb

BIND = 0x32A954
USE_OK = 0x32AFAC  # after record_pool_use, before unlock/register restoration
OUT = Path(os.environ['DM_NATIVE_BIND_OUTPUT'])
events = []
errors = []


def scalar(v):
    if not v.IsValid() or v.GetError().Fail():
        return {'error': str(v.GetError()) if v.IsValid() else 'unavailable'}
    return {'type': v.GetTypeName(), 'value': v.GetValue(),
            'unsigned': v.GetValueAsUnsigned()}


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


def raw_field(process, v, size):
    if not v.IsValid() or v.GetError().Fail() or v.GetByteSize() != size:
        return {'error': 'unavailable or unexpected fixed field size'}
    error = lldb.SBError()
    data = process.ReadMemory(v.GetLoadAddress(), size, error)
    return {'hex': bytes(data).hex()} if error.Success() and len(data) == size else {'error': str(error)}


def caller_images(thread, token=0):
    rows = []
    for frame in thread:
        # Do not select the command-line current frame, or infer image pointers
        # from unrelated same-name globals. A matched native_pool ties i to token.
        v = frame.FindVariable('i')
        if not v.IsValid() or not v.GetType().IsPointerType():
            continue
        i = v.Dereference()
        pool = child(i, 'native_pool')
        if not pool.IsValid() or pool.GetError().Fail():
            continue
        if token and pool.GetValueAsUnsigned() != token:
            continue
        row = {'frame': frame.GetFrameID(), 'function': frame.GetDisplayFunctionName(),
               'image_pointer': scalar(v),
               'image': fields(i, ['tex.serial', 'tex.mem', 'tex.L.size', 'tex.d.format',
                                  'tex.d.width', 'tex.d.height', 'tex.d.depth', 'tex.d.levels',
                                  'tex.d.target', 'tex.d.samples', 'tex.d.flags', 'tex.L.align',
                                  'tex.has_layout',
                                  'handle', 'memory', 'native_pool', 'native_entry',
                                  'initialized', 'levels', 'layers'])}
        row['original_NVNever_layout_bytes'] = raw_field(thread.GetProcess(), child(i, 'tex.layout'), 8)
        ep = child(i, 'native_entry')
        if ep.IsValid() and ep.GetValueAsUnsigned():
            entry = ep.Dereference()
            row['native_entry'] = fields(entry, ['id', 'reserved', 'recipe.image_size',
                'recipe.image_alignment', 'recipe.format', 'recipe.width', 'recipe.height',
                'recipe.depth', 'recipe.levels', 'recipe.layers', 'recipe.usage',
                'recipe.external_handle', 'recipe.producer_family'])
            row['recipe_bytes'] = {name: raw_field(thread.GetProcess(), child(entry, 'recipe.' + name), size)
                for name, size in [('layout_uuid', 16), ('device_uuid', 16), ('driver_uuid', 16),
                                   ('payload_sha256', 32), ('input_sha256', 32)]}
        rows.append(row)
    return rows


def stack(thread):
    rows = []
    for frame in thread:
        line = frame.GetLineEntry()
        rows.append({'index': frame.GetFrameID(), 'function': frame.GetDisplayFunctionName(),
            'pc': hex(frame.GetPC()), 'source': str(line.GetFileSpec()), 'line': line.GetLine(),
            'small_variables': {v.GetName(): scalar(v) for v in frame.GetVariables(True, True, False, True)
                                if v.GetName() and v.GetType().GetByteSize() <= 16}})
        if len(rows) == 24:
            break
    return rows


def save():
    (OUT / 'native-binding-stops.json').write_text(json.dumps({
        'api': 'Official LLDB SBFrame/SBValue/SBType/ReadMemory; no inferior expressions/functions',
        'events': events, 'errors': errors,
        'scope': 'At most first bind entry and first validated use; not sampling, timing or frame-wide accounting'
    }, indent=2) + '\n')


def capture(frame, bp_loc, _dict):
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    slide = int(os.environ['DM_NATIVE_BIND_SLIDE'], 16)
    kind = 'bind_entry' if frame.GetPC() == slide + BIND else 'use_validated'
    row = {'kind': kind, 'PID': process.GetProcessID(), 'TID': thread.GetThreadID(),
           'thread': thread.GetName(), 'pc': hex(frame.GetPC()),
           'registers': {name: scalar(frame.FindRegister(name)) for name in
                         ['x0', 'x1', 'x2', 'x3', 'x4', 'x19', 'x20', 'x21', 'x22', 'x23', 'sp', 'lr']}}
    try:
        reg = lambda name: frame.FindRegister(name).GetValueAsUnsigned()
        if kind == 'bind_entry':
            row['arguments'] = {'address': reg('x0'), 'bytes': reg('x1'), 'VkImage': reg('x2'),
                                'needs_pointer': reg('x3'), 'memory_output_pointer': reg('x4')}
            needs = pointed(target, reg('x3'), 'VkMemoryRequirements')
            row['requirements'] = fields(needs, ['size', 'alignment', 'memoryTypeBits'])
            row['caller_images'] = caller_images(thread)
        else:
            # These exact callee-saved registers are derived from the frozen
            # bb4 AArch64 instructions, not guessed from optimized source locals.
            token, owner = reg('x23'), reg('x22')
            row['arguments'] = {'token': token, 'address': reg('x21'), 'bytes': reg('x20'),
                                'stages': reg('w19'), 'owner_register': owner}
            a = pointed(target, token, 'native_image_allocation')
            row['allocation'] = fields(a, ['owner', 'identity', 'begin', 'end', 'address', 'bytes',
                'released', 'storage.handle', 'storage.memory', 'storage.map', 'storage.size',
                'storage.properties', 'storage.shared_import', 'storage.shared_memory_type'])
            p = pointed(target, owner, 'pool')
            row['owner'] = fields(p, ['guest', 'size', 'identity', 'nvn_flags', 'nvn_flags_known',
                'shared', 'shared_closing', 'native_image_references', 'backings', 'references'])
            row['caller_images'] = caller_images(thread, token)
            row['success_boundary'] = 'All use lifetime/identity/span guards and record_pool_use returned; resources lock still held'
        row['backtrace'] = stack(thread)
        events.append(row)
    except BaseException as e:
        row['read_error'] = repr(e)
        events.append(row)
        errors.append(repr(e))
    save()
    bp_loc.GetBreakpoint().SetEnabled(False)
    if kind == 'use_validated' or errors:
        for bp in target.breakpoint_iter():
            bp.SetEnabled(False)
        return True  # batch commands detach immediately, not stop for analysis
    return False


def install(debugger):
    target = debugger.GetSelectedTarget()
    if target.GetModuleAtIndex(0).GetUUIDString().replace('-', '').lower() != '243f00c4491b21bc3bde4a7a582c163d2cf876b7':
        raise ValueError('exact frozen bb4 target BuildID required')
    process = target.GetProcess()
    if process.IsValid() and process.GetProcessID() != int(os.environ['DM_NATIVE_BIND_PID']):
        raise ValueError('attached PID differs from caller-owned first PID')
    slide = int(os.environ['DM_NATIVE_BIND_SLIDE'], 16)
    for address in [BIND, USE_OK]:
        bp = target.BreakpointCreateByAddress(slide + address)
        bp.SetScriptCallbackFunction(__name__ + '.capture')
    save()
