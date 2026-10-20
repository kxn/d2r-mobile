"""Bind plus first TRANSFER/FRAGMENT uses; official LLDB metadata only."""
import json
import os
from pathlib import Path

import lldb

BIND = 0x32A954
USE_OK = 0x32AFAC  # after record_pool_use, before unlock/register restoration
OUT = Path(os.environ['DM_NATIVE_BIND_OUTPUT'])
events = []
errors = []
use_roles = set()
filtered_use_hits = 0  # debugger trap accounting only, never runtime profiling


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
                                  'native_acquired', 'native_use_serial',
                                  'initialized', 'levels', 'layers'])}
        row['NVN_packaged_layout_bytes'] = raw_field(thread.GetProcess(), child(i, 'tex.layout'), 8)
        row['native_write_scope_pointers'] = raw_field(thread.GetProcess(), child(i, 'native_write_scopes'), 128)
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


def b2t_context(thread):
    for frame in thread:
        if frame.GetFunctionName() != 'vk_texture_copy_b2t':
            continue
        p = frame.FindVariable('p')
        row = {'frame': frame.GetFrameID(), 'outer_SP': hex(frame.GetSP()),
               'p_pointer': scalar(p), 'row': scalar(frame.FindVariable('row')),
               'img': scalar(frame.FindVariable('img'))}
        if p.IsValid() and p.GetType().IsPointerType():
            value = p.Dereference()
            row['parameters'] = fields(value, ['src', 'tex', 'has_view', 'flags',
                'view.set_levels', 'view.base_level', 'view.nlevels', 'view.set_layers',
                'view.min_layer', 'view.nlayers', 'view.set_format', 'view.format',
                'r.xoffset', 'r.yoffset', 'r.zoffset', 'r.width', 'r.height', 'r.depth'])
            tex = child(value, 'tex')
            if tex.IsValid() and tex.GetValueAsUnsigned():
                row['destination_tex'] = fields(tex.Dereference(), ['serial', 'mem', 'L.size',
                    'd.width', 'd.height', 'd.depth', 'd.levels', 'd.format'])
        row['actual_copy'] = fields(frame.FindVariable('c'), ['bufferOffset', 'bufferRowLength',
            'bufferImageHeight', 'imageSubresource.aspectMask', 'imageSubresource.mipLevel',
            'imageSubresource.baseArrayLayer', 'imageSubresource.layerCount',
            'imageOffset.x', 'imageOffset.y', 'imageOffset.z',
            'imageExtent.width', 'imageExtent.height', 'imageExtent.depth'])
        # At this O3 success-PC, row/img may be optimized out. The unchanged
        # outer SP + p snapshot ties the earlier live arguments to THIS call.
        for event in events:
            original = event.get('typed_B2T')
            if (event['kind'] == 'bind_entry' and original and
                    original['outer_SP'] == row['outer_SP'] and
                    original['p_pointer'].get('unsigned') == row['p_pointer'].get('unsigned') and
                    original.get('parameters') == row.get('parameters') and
                    'error' not in original['row'] and 'error' not in original['img']):
                row['row_img_from_matching_bind'] = {'row': original['row'], 'img': original['img'],
                    'source': 'same immutable p snapshot and same outer SP at bind entry'}
                break
        return row
    return None


def shader_context(thread):
    rows = []
    target = thread.GetProcess().GetTarget()
    for frame in thread:
        function = frame.GetFunctionName() or ''
        if function not in ['shader_bindings_selected_impl', 'vk_shader_prepare_images']:
            continue
        row = {'frame': frame.GetFrameID(), 'function': function,
            'locals': {name: scalar(frame.FindVariable(name))
                       for name in ['stage', 'k', 'n', 'handle', 'h', 's', 'list', 'b']}}
        s = frame.FindVariable('s')
        if (not s.IsValid() or s.GetError().Fail()) and function == 'vk_shader_prepare_images':
            v = frame.FindVariable('list')
            if v.IsValid() and not v.GetError().Fail() and v.GetValueAsUnsigned():
                error = lldb.SBError()
                raw = thread.GetProcess().ReadMemory(v.GetValueAsUnsigned() + 8, 8, error)
                if error.Success() and len(raw) == 8:
                    address = int.from_bytes(raw, 'little')
                    if address:
                        s = pointed(target, address, 'vk_shader')
                        row['shader_source'] = 'actual list[1] from caller; NVN FS slot'
        if s.IsValid() and not s.GetError().Fail():
            if s.GetType().IsPointerType():
                s = s.Dereference()
            row['shader'] = fields(s, ['stage', 'nb', 'module'])
            row['key'] = raw_field(thread.GetProcess(), child(s, 'key'), 17)
        b = frame.FindVariable('b')
        if b.IsValid() and not b.GetError().Fail() and b.GetType().IsPointerType():
            row['binding'] = fields(b.Dereference(), ['kind', 'reg', 'sampler', 'type', 'count', 'binding', 'flags'])
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
        'capture_protocol': 'native-bind-transfer-fragment-v2',
        'filtered_use_hits': filtered_use_hits,
        'scope': 'At most bind entry + first TRANSFER + first FRAGMENT typed snapshots; other official breakpoint traps continue without typed traversal, not FPS/timing evidence'
    }, indent=2) + '\n')


def capture(frame, bp_loc, _dict):
    global filtered_use_hits
    thread = frame.GetThread()
    process = thread.GetProcess()
    target = process.GetTarget()
    slide = int(os.environ['DM_NATIVE_BIND_SLIDE'], 16)
    kind = 'bind_entry' if frame.GetPC() == slide + BIND else 'use_validated'
    role = None
    if kind == 'use_validated':
        stages = frame.FindRegister('w19').GetValueAsUnsigned()
        role = 'fragment' if stages & 0x80 and 'fragment' not in use_roles else \
            'transfer' if stages & 0x1000 and 'transfer' not in use_roles else None
        if role is None:
            filtered_use_hits += 1
            return False
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
            row['typed_B2T'] = b2t_context(thread)
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
            row['use_stage_role'] = role
            if role == 'transfer':
                row['typed_B2T'] = b2t_context(thread)
            else:
                row['shader_binding_callers'] = shader_context(thread)
            row['success_boundary'] = 'All use lifetime/identity/span guards and record_pool_use returned; resources lock still held'
        row['backtrace'] = stack(thread)
        events.append(row)
    except BaseException as e:
        row['read_error'] = repr(e)
        events.append(row)
        errors.append(repr(e))
    save()
    if kind == 'bind_entry':
        bp_loc.GetBreakpoint().SetEnabled(False)
    elif role:
        use_roles.add(role)
    if use_roles == {'transfer', 'fragment'} or errors:
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
