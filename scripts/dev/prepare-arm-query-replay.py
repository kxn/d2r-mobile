#!/usr/bin/env python3
"""Prepare diagnostic GFXR replay for the reproduced Mali r32 query-state crash.
仅处理工具生成的加载状态；原捕获、真实帧命令和游戏文件均不修改。

The input must be a GFXReconstruct 0.1 LZ4 trace with the exact reset-only /
fresh-query-activation sequence verified on Mali-G710 r32. Unknown layouts fail
closed. This is a replay workaround, not a runtime optimization or FPS result.
"""

import argparse
import ctypes
import hashlib
import json
import shutil
import struct
from pathlib import Path

CHUNK = 1 << 20
CREATE_QUERY_POOL = 0x1102F
BEGIN_QUERY = 0x1107F
RESET_QUERY_POOL = 0x11081
BEGIN_COMMAND_BUFFER = 0x1105A
ACTIVATION_SEQUENCE = [
    BEGIN_COMMAND_BUFFER,
    RESET_QUERY_POOL,
    0x1105B,  # vkEndCommandBuffer
    0x11012,  # vkQueueSubmit
    0x11013,  # vkQueueWaitIdle
    0x11056,  # vkDestroyCommandPool
    0x11011,  # vkGetDeviceQueue
    0x11055,  # vkCreateCommandPool
    0x11058,  # vkAllocateCommandBuffers
    BEGIN_COMMAND_BUFFER,
    BEGIN_QUERY,
]


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(CHUNK):
            h.update(block)
    return h.hexdigest()


def find_activation(source):
    lz4 = ctypes.CDLL("liblz4.so.1")
    lz4.LZ4_decompress_safe.argtypes = [
        ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int, ctypes.c_int
    ]
    resets, query_pools, calls = {}, {}, []
    in_state = False
    with source.open("rb") as stream:
        fourcc, major, minor, count = struct.unpack("<4I", stream.read(16))
        if (fourcc, major, minor) != (0x52584647, 0, 1):
            raise ValueError("unsupported GFXR header")
        options = stream.read(count * 8)
        pairs = [options[i:i + 8] for i in range(0, len(options), 8)]
        if struct.pack("<II", 1, 1) not in pairs:
            raise ValueError("requires an LZ4 capture")
        index = 0
        while header := stream.read(12):
            offset = stream.tell() - 12
            size, kind = struct.unpack("<QI", header)
            payload = stream.read(size)
            index += 1
            if len(payload) != size:
                raise ValueError("truncated capture")
            if kind == 2:
                marker, frame = struct.unpack("<IQ", payload)
                in_state = marker == 1
                if not in_state:
                    break
            if kind & 0x7FFFFFFF != 4 or not in_state:
                continue
            call, thread = struct.unpack_from("<IQ", payload)
            calls.append((index, call, thread))
            if call not in (CREATE_QUERY_POOL, RESET_QUERY_POOL, BEGIN_QUERY):
                continue
            data = payload[12:]
            if kind & 0x80000000:
                length = struct.unpack_from("<Q", data)[0]
                output = ctypes.create_string_buffer(length)
                compressed = data[8:]
                result = lz4.LZ4_decompress_safe(
                    compressed, output, len(compressed), length
                )
                if result != length:
                    raise ValueError("invalid LZ4 payload")
                data = output.raw
            if call == CREATE_QUERY_POOL:
                # Only the no-pNext structure layout seen in the verified trace.
                # 只接受已验证捕获中的无 pNext 结构编码。
                if (len(data) != 72
                        or struct.unpack_from("<I", data, 20)[0] != 11
                        or struct.unpack_from("<I", data, 24)[0] != 35):
                    raise ValueError("unsupported query-pool encoding")
                pool = struct.unpack_from("<Q", data, 60)[0]
                query_pools[pool] = struct.unpack_from("<II", data, 32)
            elif call == RESET_QUERY_POOL:
                cmd, pool, first, count = struct.unpack("<QQII", data)
                resets[pool] = (offset, header + payload, index, cmd, first, count)
            else:
                cmd, pool, query, flags = struct.unpack("<QQII", data)
                if query_pools.get(pool, (None,))[0] != 0:
                    raise ValueError("first synthetic query is not occlusion")
                recent = calls[-len(ACTIVATION_SEQUENCE):]
                if ([c for _, c, _ in recent] != ACTIVATION_SEQUENCE
                        or len({t for _, _, t in recent}) != 1):
                    raise ValueError("unsupported loading-state command sequence")
                reset = resets.get(pool)
                if (not reset or reset[3] != cmd or reset[4] != 0
                        or reset[5] != query_pools[pool][1]
                        or index - reset[2] != 9):
                    raise ValueError("no matching adjacent full pool reset")
                return reset, offset, index, pool, cmd, frame
    raise ValueError("no supported loading-state activation")


def prepare(source, output):
    if output.exists():
        raise FileExistsError("output already exists")
    reset, query_offset, query_index, pool, cmd, frame = find_activation(source)
    reset_offset, reset_block, reset_index, *_ = reset
    # Move one existing reset, preserving block count and every actual frame byte.
    # 只移动已有 reset，保持块数与真实帧全部字节不变。
    with source.open("rb") as original, output.open("xb") as target:
        remaining = reset_offset
        while remaining:
            block = original.read(min(CHUNK, remaining))
            target.write(block)
            remaining -= len(block)
        original.seek(reset_offset + len(reset_block))
        target.write(original.read(query_offset - reset_offset - len(reset_block)))
        target.write(reset_block)
        original.seek(query_offset)
        shutil.copyfileobj(original, target, CHUNK)
    if output.stat().st_size != source.stat().st_size:
        raise ValueError("file size changed")
    with source.open("rb") as original, output.open("rb") as target:
        original.seek(query_offset)
        target.seek(query_offset)
        while block := original.read(CHUNK):
            if block != target.read(len(block)):
                raise ValueError("captured-frame suffix changed")
    record = {
        "purpose": "Mali r32 diagnostic replay only; original capture and game unchanged",
        "source": str(source.resolve()),
        "output": str(output.resolve()),
        "source_sha256": digest(source),
        "output_sha256": digest(output),
        "script_sha256": digest(Path(__file__)),
        "rule": "move existing full occlusion reset into fresh loading activation buffer",
        "frame": frame,
        "reset_source_block": reset_index,
        "activation_query_block": query_index,
        "pool": pool,
        "state_command_buffer": cmd,
        "unchanged_suffix_offset": query_offset,
        "captured_frame_suffix_byte_identical": True,
        "size_unchanged": True,
    }
    output.with_suffix(".provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prepare(args.source, args.output)
