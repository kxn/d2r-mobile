"""Preserve NVN word-buffer addresses with aligned Vulkan descriptors.

Only proven single-member runtime uint arrays are transformed. Unsupported
pointer flows retain the original module and the existing alignment-copy path.
No shader math, control flow, draw, or game address is changed.
"""
import re
import struct
import subprocess


def bias_storage_offsets(code, bindings, stage):
    """Return (SPIR-V, push-slot mask, explanation); slots are uint word biases.

    VS slots 0..15 occupy push bytes 0..63; FS occupies bytes 64..127.
    CS uses slots 0..31 (SSBO plus original compute UBO pointers) at bytes 0..127.
    Descriptor ranges include the aligned-down prefix. Unsigned-index wrap is
    kept out of bounds instead of exposing bytes before the original binding.
    """
    if not bindings or stage not in (0, 1, 5):
        return code, 0, 'no supported storage bindings'
    slots = 32 if stage == 5 else 16
    if any(not 0 <= slot < slots for slot in bindings.values()):
        raise ValueError('storage bias push slot outside stage layout')
    if len(code) % 4 or len(code) < 20:
        raise ValueError('invalid SPIR-V module')
    header = struct.unpack_from('<5I', code)
    if header[0] != 0x07230203:
        raise ValueError('invalid SPIR-V magic')
    text = subprocess.run(['spirv-dis', '--raw-id', '-o', '-', '-'], input=code,
                          capture_output=True, check=True, timeout=30).stdout.decode()
    entries = []
    definitions, types, constants, binding_ids, strides, offsets = {}, {}, {}, {}, {}, {}
    for line in text.splitlines():
        words = line.split()
        if not words or words[0].startswith(';'):
            entries.append((line, None, '', []))
            continue
        result = words[0] if len(words) > 2 and words[1] == '=' else None
        op, args = (words[2], words[3:]) if result else (words[0], words[1:])
        entries.append((line, result, op, args))
        if result:
            definitions[result] = (op, args)
            if args and args[0].startswith('%'):
                types[result] = args[0]
        if op == 'OpConstant' and len(args) == 2:
            try:
                constants[result] = int(args[1], 0)
            except ValueError:
                pass
        if op == 'OpConstantNull':
            constants[result] = 0
        if op == 'OpDecorate' and len(args) == 3:
            if args[1] == 'Binding':
                binding_ids[int(args[2])] = args[0]
            elif args[1] == 'ArrayStride':
                strides[args[0]] = int(args[2])
        if op == 'OpMemberDecorate' and len(args) == 4 and args[2] == 'Offset':
            offsets[(args[0], int(args[1]))] = int(args[3])

    resources = {}
    uint_type = None
    for binding, slot in bindings.items():
        variable = binding_ids.get(binding)
        if variable is None:
            continue  # A previous dead-code pass may have removed the binding.
        variable_op, variable_args = definitions.get(variable, ('', []))
        if variable_op != 'OpVariable' or len(variable_args) != 2:
            return code, 0, 'unsupported storage variable'
        ptr_op, ptr_args = definitions.get(variable_args[0], ('', []))
        if ptr_op != 'OpTypePointer' or len(ptr_args) != 2 or ptr_args[0] not in ('Uniform', 'StorageBuffer'):
            return code, 0, 'unsupported storage pointer'
        block_op, members = definitions.get(ptr_args[1], ('', []))
        if block_op != 'OpTypeStruct' or len(members) != 1 or offsets.get((ptr_args[1], 0)) != 0:
            return code, 0, 'storage block is not a single zero-offset member'
        array_op, element = definitions.get(members[0], ('', []))
        if array_op != 'OpTypeRuntimeArray' or len(element) != 1 or strides.get(members[0]) != 4:
            return code, 0, 'storage member is not a tightly packed runtime array'
        if definitions.get(element[0]) != ('OpTypeInt', ['32', '0']):
            return code, 0, 'storage array is not uint32'
        if uint_type is not None and uint_type != element[0]:
            return code, 0, 'multiple uint32 type identities'
        uint_type = element[0]
        resources[variable] = slot
    if not resources:
        return code, 0, 'no active storage variable'

    # Prove that every descriptor-pointer use is covered by this transformation.
    # Element pointers may be loaded/stored/atomically accessed, but cannot be
    # further offset through an unrecognized pointer-arithmetic path.
    element_pointers = set()
    for _, result, op, args in entries:
        used = set(args) & resources.keys()
        if not used:
            continue
        if op in ('OpName', 'OpDecorate', 'OpEntryPoint'):
            continue
        if op in ('OpAccessChain', 'OpInBoundsAccessChain') and len(args) == 4 and args[1] in resources and constants.get(args[2]) == 0:
            index_type = definitions.get(types.get(args[3]), ('', []))
            if index_type[0] != 'OpTypeInt' or index_type[1] not in (['32', '0'], ['32', '1']):
                return code, 0, 'storage index is not a 32-bit integer'
            element_pointers.add(result)
            continue
        if op == 'OpArrayLength' and len(args) == 3 and args[1] in resources and args[2] == '0' and args[0] == uint_type:
            continue
        return code, 0, 'unrecognized descriptor pointer use: ' + op
    for _, _, op, args in entries:
        used = set(args) & element_pointers
        if not used or op in ('OpName', 'OpDecorate'):
            continue
        if op == 'OpLoad' and len(args) >= 2 and used == {args[1]}:
            continue
        if op == 'OpStore' and args and used == {args[0]}:
            continue
        if op.startswith('OpAtomic'):
            pointer = args[0] if op in ('OpAtomicStore', 'OpAtomicFlagClear') else args[1]
            if used == {pointer}:
                continue
        return code, 0, 'unrecognized storage element pointer use: ' + op

    next_id = header[3]

    def new_id():
        nonlocal next_id
        value = '%' + str(next_id)
        next_id += 1
        return value

    extra_constants = []
    uint_constants = {value: key for key, value in constants.items() if types.get(key) == uint_type}

    def uint_constant(value):
        if value not in uint_constants:
            result = new_id()
            uint_constants[value] = result
            extra_constants.append(f'{result} = OpConstant {uint_type} {value}')
        return uint_constants[value]

    array, block, block_ptr, word_ptr, variable = [new_id() for _ in range(5)]
    bool_type = next((result for _, result, op, _ in entries if op == 'OpTypeBool'), None)
    extra_types = []
    if bool_type is None:
        bool_type = new_id()
        extra_types.append(f'{bool_type} = OpTypeBool')
    array_length, zero, maximum = uint_constant(slots), uint_constant(0), uint_constant(0xffffffff)
    for slot in resources.values():
        uint_constant(slot)
    annotations = [f'OpDecorate {array} ArrayStride 4', f'OpDecorate {block} Block',
                   f'OpMemberDecorate {block} 0 Offset {64 if stage == 1 else 0}']
    declarations = extra_types + [f'{array} = OpTypeArray {uint_type} {array_length}',
        f'{block} = OpTypeStruct {array}', f'{block_ptr} = OpTypePointer PushConstant {block}',
        f'{word_ptr} = OpTypePointer PushConstant {uint_type}',
        f'{variable} = OpVariable {block_ptr} PushConstant']

    def load_bias(slot):
        pointer, value = new_id(), new_id()
        return [f'{pointer} = OpAccessChain {word_ptr} {variable} {zero} {uint_constant(slot)}',
                f'{value} = OpLoad {uint_type} {pointer}'], value

    output, annotated, declared = [], False, False
    changed_results = set()
    for line, result, op, args in entries:
        if not annotated and op.startswith('OpType'):
            output.extend(annotations)
            annotated = True
        if op == 'OpFunction' and not declared:
            output.extend(extra_constants + declarations)
            declared = True
        if op in ('OpAccessChain', 'OpInBoundsAccessChain') and len(args) == 4 and args[1] in resources:
            changed_results.add(int(result[1:]))
            prefix, bias = load_bias(resources[args[1]])
            output.extend(prefix)
            index = args[3]
            if types[index] != uint_type:
                cast = new_id()
                output.append(f'{cast} = OpBitcast {uint_type} {index}')
                index = cast
            added, overflow, adjusted = new_id(), new_id(), new_id()
            output.extend([f'{added} = OpIAdd {uint_type} {index} {bias}',
                           f'{overflow} = OpULessThan {bool_type} {added} {index}',
                           f'{adjusted} = OpSelect {uint_type} {overflow} {maximum} {added}',
                           f'{result} = {op} {args[0]} {args[1]} {args[2]} {adjusted}'])
        elif op == 'OpArrayLength' and len(args) == 3 and args[1] in resources:
            changed_results.add(int(result[1:]))
            length = new_id()
            output.append(f'{length} = OpArrayLength {args[0]} {args[1]} {args[2]}')
            prefix, bias = load_bias(resources[args[1]])
            output.extend(prefix)
            output.append(f'{result} = OpISub {uint_type} {length} {bias}')
        else:
            output.append(line)
    if not declared:
        raise ValueError('SPIR-V module has no function')
    # Preserve IDs for reproducible provenance and support the original 1.3 path.
    assembled = subprocess.run(['spirv-as', '--preserve-numeric-ids', '--target-env', 'spv1.3', '-o', '-', '-'],
                               input=('\n'.join(output)+'\n').encode(), capture_output=True,
                               check=True, timeout=30).stdout
    def instructions(blob):
        words = struct.unpack('<%dI' % (len(blob)//4), blob)
        i, result = 5, []
        while i < len(words):
            count = words[i] >> 16
            if not count or i + count > len(words):
                raise ValueError('malformed SPIR-V instruction')
            result.append(words[i:i+count])
            i += count
        return result
    # The text round trip must not change any original instruction apart from
    # the explicitly rebased addresses/lengths. This also protects float literals.
    new_instructions, cursor = instructions(assembled), 0
    for instruction in instructions(code):
        if instruction[0] & 0xffff in (65, 66, 68) and instruction[2] in changed_results:
            continue
        while cursor < len(new_instructions) and new_instructions[cursor] != instruction:
            cursor += 1
        if cursor == len(new_instructions):
            raise ValueError('storage bias altered an unrelated SPIR-V instruction')
        cursor += 1
    mask = sum(1 << slot for slot in set(resources.values()))
    return assembled, mask, 'aligned-down descriptor with uint32 word bias'
