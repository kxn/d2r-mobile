"""把不可变 c1 的静态读取变成逐位相同的常量；动态/未知布局仍保留原读取。
Fold static immutable-c1 loads to bit-identical constants. Dynamic accesses and
unrecognized layouts retain their original loads; no precision changes.
"""
import struct


def inline_c1(code, binding, data, descriptor_set):
    words = list(struct.unpack('<%dI' % (len(code) // 4), code))
    if len(words) < 5 or words[0] != 0x07230203:
        raise ValueError('invalid SPIR-V header')
    instructions = []
    at = 5
    while at < len(words):
        count, op = words[at] >> 16, words[at] & 0xffff
        if not count or at + count > len(words):
            raise ValueError('invalid SPIR-V instruction')
        instructions.append((op, words[at:at + count]))
        at += count
    types, literals, decorations, offsets, variables = {}, {}, {}, {}, {}
    constants = {}
    for op, w in instructions:
        if 19 <= op <= 39:
            types[w[1]] = (op, w[2:])
        elif op == 43 and len(w) == 4:
            literals[w[2]] = (w[1], w[3])
            constants[(w[1], w[3])] = w[2]
        elif op == 59 and len(w) >= 4:
            variables[w[2]] = (w[1], w[3])
        elif op == 71 and len(w) == 4:
            decorations[(w[1], w[2])] = w[3]
        elif op == 72 and len(w) == 5:
            offsets[(w[1], w[2], w[3])] = w[4]
    stats = {'scalar_loads': 0, 'vector_loads': 0}
    if binding is None:
        return code, stats
    roots = [v for v in variables if decorations.get((v, 33)) == binding
             and decorations.get((v, 34)) == descriptor_set]
    if len(roots) != 1:
        return code, stats
    root = roots[0]
    pointer, storage = variables[root]
    # 原翻译器的 c1：Uniform { vec4[4096] }，std140 的 16 字节步长、成员零偏移。
    # Prove the original translator's Uniform { vec4[4096] } std140 layout.
    pointer_type = types.get(pointer)
    if storage != 2 or not pointer_type or pointer_type[0] != 32:
        return code, stats
    structure = pointer_type[1][1]
    structure_type = types.get(structure)
    if (not structure_type or structure_type[0] != 30 or len(structure_type[1]) != 1
            or offsets.get((structure, 0, 35)) != 0):
        return code, stats
    array = structure_type[1][0]
    array_type = types.get(array)
    if (not array_type or array_type[0] != 28
            or decorations.get((array, 6)) != 16):
        return code, stats
    vector, length_id = array_type[1]
    vector_type = types.get(vector)
    if not vector_type or vector_type[0] != 23 or vector_type[1][1] != 4:
        return code, stats
    scalar = vector_type[1][0]
    scalar_type = types.get(scalar)
    if not scalar_type or scalar_type[0] not in (21, 22) or scalar_type[1][0] != 32:
        return code, stats
    array_length = literals.get(length_id)
    if not array_length or array_length[1] != 4096 or len(data) > 65536:
        return code, stats
    # 与 constant() 的零填充完全一致，包括负零、NaN 和整数位模式。
    # Match constant()'s zero padding, including -0, NaN and integer bit patterns.
    payload = data.ljust(65536, b'\0')
    additions, paths, composites = [], {root: ()}, {}
    bound = words[3]

    def scalar_constant(bits):
        nonlocal bound
        key = (scalar, bits)
        if key not in constants:
            constants[key] = bound
            additions.extend([(4 << 16) | 43, scalar, bound, bits])
            bound += 1
        return constants[key]

    def integer(identifier):
        value = literals.get(identifier)
        if value is None:
            return None
        kind = types.get(value[0])
        if not kind or kind[0] != 21 or kind[1][0] != 32:
            return None
        return value[1]

    result = []
    for op, w in instructions:
        if op in (65, 66) and w[3] in paths:
            indices = tuple(integer(i) for i in w[4:])
            if all(i is not None for i in indices):
                paths[w[2]] = paths[w[3]] + indices
        elif op == 83 and len(w) == 4 and w[3] in paths:
            paths[w[2]] = paths[w[3]]
        elif op == 61 and len(w) == 4 and w[3] in paths:
            path = paths[w[3]]
            if (len(path) in (2, 3) and path[0] == 0 and path[1] < 4096
                    and (len(path) == 2 and w[1] == vector
                         or len(path) == 3 and path[2] < 4 and w[1] == scalar)):
                offset = path[1] * 16 + (path[2] * 4 if len(path) == 3 else 0)
                values = struct.unpack_from('<%dI' % (4 if len(path) == 2 else 1),
                                            payload, offset)
                ids = tuple(scalar_constant(value) for value in values)
                value = ids[0]
                if len(ids) == 4:
                    if ids not in composites:
                        composites[ids] = bound
                        additions.extend([(7 << 16) | 44, vector, bound, *ids])
                        bound += 1
                    value = composites[ids]
                stats['vector_loads' if len(ids) == 4 else 'scalar_loads'] += 1
                w = [(4 << 16) | 83, w[1], w[2], value]
        result.append((op, w))
    if not additions and not any(stats.values()):
        return code, stats
    output = words[:5]
    output[3] = bound
    inserted = False
    for op, w in result:
        if op == 54 and not inserted:
            output.extend(additions)
            inserted = True
        output.extend(w)
    if not inserted:
        raise ValueError('SPIR-V has no function')
    return struct.pack('<%dI' % len(output), *output), stats
