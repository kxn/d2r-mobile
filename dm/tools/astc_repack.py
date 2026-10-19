"""Bounded offline BC3 -> ASTC6x6 repack; original NVN layout C is authoritative.

No game binary patches, runtime hooks, resource deployment, GPU profiling or
replacement tiling/codec implementation. astc_pack's default4x4 path is separate.
"""
import base64
import ctypes as C
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess

import numpy as np
from PIL import Image
import romfs_art
import touch_layout

GAME_FORMAT = {61: 99, 62: 85}  # original game table: -> NVN0x7d/0x8b
GAME_FORMAT4 = {61: 95, 62: 81}
NVN_BC3 = {61: 0x44, 62: 0x48}
NVN_ASTC6 = {61: 0x7d, 62: 0x8b}
NVN_ASTC4 = {61: 0x79, 62: 0x87}
MAX_INPUT_BYTES = 32 << 20


class Unsupported(ValueError):
    """A representation for which this experiment provides no conversion."""


def sha(data):
    return hashlib.sha256(data).hexdigest()


def dimensions(w, h, level):
    return max(1, w >> level), max(1, h >> level)


def block_bytes(w, h, block):
    return ((w + block - 1) // block) * ((h + block - 1) // block) * 16


def desc(entry, fmt):
    if (entry.get('type') != 2 or entry.get('depth') != 1 or
            entry.get('layoutType') != 3 or entry.get('format') not in GAME_FORMAT):
        raise Unsupported('only type2/depth1/layoutType3 BC3 textures are supported')
    w, h, n, layout = (entry[k] for k in ('width', 'height', 'levels', 'layoutData'))
    if (not all(isinstance(v, int) for v in (w, h, n, layout)) or
            not 1 <= w <= 4096 or not 1 <= h <= 4096 or
            not 1 <= n <= max(w, h).bit_length() or
            not 0 <= layout < (1 << 64) or (layout & 255) > 5):
        raise Unsupported('unsupported dimensions/mip count/packaged block height')
    return romfs_art.Desc(1, fmt, 0, 0, w, h, 1, n, 0, (layout & 255) + 1)


def geometry(d):
    lib = romfs_art.lib()
    lib.nvn_tex_level_offset.argtypes = [C.POINTER(romfs_art.Desc), C.c_uint]
    lib.nvn_tex_level_offset.restype = C.c_uint64
    layout = romfs_art.Layout()
    lib.nvn_tex_layout(C.byref(d), C.byref(layout))
    offsets = [lib.nvn_tex_level_offset(C.byref(d), i) for i in range(d.levels)] + [layout.size]
    if not 0 < layout.size <= MAX_INPUT_BYTES or offsets[0] != 0:
        raise Unsupported('NVN layout is outside sample storage budget')
    return offsets


def upload_geometry(d, level):
    """Existing C geometry: useful compressed blocks, not VkImage allocation."""
    lib = romfs_art.lib()
    lib.nvn_deswizzle_geom.argtypes = [C.POINTER(romfs_art.Desc), C.c_uint, C.c_uint,
        C.POINTER(C.c_uint32), C.POINTER(C.c_uint32), C.POINTER(C.c_uint32),
        C.POINTER(C.c_uint64), C.POINTER(C.c_uint), C.POINTER(C.c_uint), C.POINTER(C.c_uint32)]
    row, rows, slices, base, th, td, bpb = (C.c_uint32(), C.c_uint32(), C.c_uint32(),
        C.c_uint64(), C.c_uint(), C.c_uint(), C.c_uint32())
    if lib.nvn_deswizzle_geom(C.byref(d), level, 0, C.byref(row), C.byref(rows),
            C.byref(slices), C.byref(base), C.byref(th), C.byref(td), C.byref(bpb)):
        raise ValueError('original NVN geometry rejected mip')
    return {'row_bytes': row.value, 'block_rows': rows.value, 'slices': slices.value,
            'linear_upload_content_bytes': row.value * rows.value * slices.value,
            'NVN_level_offset': base.value, 'tileH': th.value, 'tileD': td.value}


def mip_table(data, n, tab):
    if not 1 <= n <= 16 or tab < 0 or tab + 8 * n > len(data):
        raise ValueError('invalid mip table extent')
    cursor = tab + 8 * n
    spans = []
    for i in range(n):
        size = struct.unpack_from('<I', data, tab + 8 * i)[0]
        rel_field = tab + 8 * i + 4
        start = rel_field + struct.unpack_from('<i', data, rel_field)[0]
        if not size or size % 16 or start != cursor or size > len(data) - start:
            raise ValueError('mips must be contiguous complete blocks after their table')
        spans.append((start, size))
        cursor = start + size
    if cursor != len(data):
        raise ValueError('unexpected trailing mip payload')
    return spans


def full_texture(data, entry):
    if len(data) < 36 or len(data) > MAX_INPUT_BYTES or data[:4] != b'<DE(':
        raise Unsupported('unsupported full texture header/size')
    if (data[4] != entry['format'] or data[6] != entry['type'] or
            struct.unpack_from('<II', data, 8) != (entry['width'], entry['height']) or
            struct.unpack_from('<H', data, 16)[0] != entry['depth'] or
            data[18] != entry['layoutType'] or
            struct.unpack_from('<Q', data, 19)[0] != entry['layoutData'] or
            struct.unpack_from('<I', data, 28)[0] != entry['levels']):
        raise ValueError('full header and original descriptor cache disagree')
    tab = 32 + struct.unpack_from('<I', data, 32)[0]
    spans = mip_table(data, entry['levels'], tab)
    old = desc(entry, NVN_BC3[entry['format']])
    offsets = geometry(old)
    first = spans[0][0]
    for i, (start, size) in enumerate(spans):
        if start - first != offsets[i] or size != offsets[i + 1] - offsets[i]:
            raise Unsupported('serialized mip sizes do not match original NVN packaged layout')
    return tab, first, old, offsets


def deswizzle(d, storage, level, block):
    w, h = dimensions(d.width, d.height, level)
    pitch = ((w + block - 1) // block) * 16
    output = C.create_string_buffer(block_bytes(w, h, block))
    source = C.create_string_buffer(storage)
    if romfs_art.lib().nvn_deswizzle(C.byref(d), level, 0, source, output, pitch):
        raise ValueError('original NVN deswizzle rejected mip')
    return output.raw


def swizzle(d, storage, level, raw, block):
    w, h = dimensions(d.width, d.height, level)
    if len(raw) != block_bytes(w, h, block):
        raise ValueError('linear compressed mip size mismatch')
    source = C.create_string_buffer(raw)
    pitch = ((w + block - 1) // block) * 16
    if romfs_art.lib().nvn_swizzle(C.byref(d), level, 0, source, pitch, storage):
        raise ValueError('original NVN swizzle rejected mip')


def decode_bc3(raw, w, h):
    """Use Pillow's existing DDS codec; this writes only the DDS container."""
    if len(raw) != block_bytes(w, h, 4):
        raise ValueError('invalid linear BC3 dimensions')
    # Same DDS header recipe as romfs_art.dxt5(), with an already linear mip.
    hdr = [124, 0x81007, h, w, len(raw), 0, 1] + [0] * 11
    hdr += [32, 4, struct.unpack('<I', b'DXT5')[0], 0, 0, 0, 0, 0] + [0x1000, 0, 0, 0, 0]
    return np.array(Image.open(io.BytesIO(b'DDS ' + struct.pack('<31I', *hdr) + raw)).convert('RGBA'))


def channel_diagnostics(original, decoded, normal):
    """Gaps in astcenc's metrics: source G/A normal convention and alpha edges.

    PSNR is supplied by official astcenc test mode, not reimplemented here.
    """
    delta = np.abs(original.astype(np.int16) - decoded.astype(np.int16))
    result = {'RGBA_max_abs': delta.max(axis=(0, 1)).tolist(),
              'RGBA_mean_abs': delta.mean(axis=(0, 1)).tolist(),
              'alpha_coverage': {str(k): {'before': int((original[:, :, 3] >= k).sum()),
                                         'after': int((decoded[:, :, 3] >= k).sum())}
                                 for k in (1, 128, 255)}}
    if normal:
        # Upstream _NRM convention X=A/Y=G, R/B left in their original slots.
        # astcenc -normal would repack to a different convention, so do not use it.
        def vectors(a):
            xy = a[:, :, [3, 1]].astype(np.float64) * (2 / 255) - 1
            z = np.sqrt(np.maximum(0, 1 - np.square(xy).sum(axis=2)))
            v = np.dstack((xy, z))
            return v / np.linalg.norm(v, axis=2)[:, :, None]
        angles = np.degrees(np.arccos(np.clip((vectors(original) * vectors(decoded)).sum(axis=2), -1, 1)))
        result['normal_XA_YG_estimated_degrees'] = {
            'mean': float(angles.mean()), 'p95': float(np.percentile(angles, 95)),
            'max': float(angles.max()), 'assumption': 'upstream X=A,Y=G,positive reconstructed Z; not shader execution proof'}
    return result


class Encoder:
    def __init__(self, executable, preset, threads):
        self.path = str(Path(executable).resolve())
        if Path(self.path).read_bytes()[:2] == b'#!':
            raise ValueError('6x6 samples require the actual astcenc executable, not a thread-overriding wrapper')
        self.preset, self.threads = preset, threads
        self.version = subprocess.check_output([self.path, '-version'], text=True).strip()
        self.commands = []

    def invoke(self, args, log):
        command = [self.path, *args, '-j', str(self.threads), '-silent']
        p = subprocess.run(command, capture_output=True, text=True)
        log.write_text(p.stdout + p.stderr)
        self.commands.append({'argv': command, 'returncode': p.returncode, 'log': str(log),
                              'log_sha256': sha(log.read_bytes())})
        p.check_returncode()

    def mip(self, rgba, directory, srgb, normal, block=6):
        directory.mkdir(parents=True)
        source, astc = directory / 'source.png', directory / 'encoded.astc'
        decoded, actual_float, tested_float = (directory / p for p in ('decoded.png', 'actual.exr', 'tested.exr'))
        Image.fromarray(rgba, 'RGBA').save(source)
        profile = 's' if srgb else 'l'
        options = [f'{block}x{block}', '-' + self.preset]
        self.invoke(['-c' + profile, str(source), str(astc), *options], directory / 'encode.log')
        encoded = astc.read_bytes()
        h, w = rgba.shape[:2]
        if (encoded[:7] != bytes.fromhex('13aba15c') + bytes((block, block, 1)) or
                [int.from_bytes(encoded[k:k + 3], 'little') for k in (7, 10, 13)] != [w, h, 1] or
                len(encoded) != 16 + block_bytes(w, h, block)):
            raise ValueError('astcenc produced an unexpected block grid/header')
        self.invoke(['-d' + profile, str(astc), str(decoded)], directory / 'decode.log')
        # Existing astcenc supplies PSNR. Float EXR output retains its default
        # float16 decode mode, unlike an 8bit test PNG which changes encoder mode.
        self.invoke(['-t' + profile, str(source), str(tested_float), *options], directory / 'quality.log')
        self.invoke(['-d' + profile, str(astc), str(actual_float)], directory / 'float-decode.log')
        if actual_float.read_bytes() != tested_float.read_bytes():
            raise ValueError('official test-mode metrics do not match the actual compressed/decompressed image')
        back = np.array(Image.open(decoded).convert('RGBA'))
        if back.shape != rgba.shape:
            raise ValueError('astcenc changed image dimensions')
        report = {'dimensions': [w, h], 'linear_bytes': len(encoded) - 16,
                  'official_quality_log': str(directory / 'quality.log'),
                  'official_astcenc_metrics': {line.partition(':')[0].strip(): line.partition(':')[2].strip()
                      for line in (directory / 'quality.log').read_text().splitlines()
                      if 'PSNR' in line and ':' in line},
                  'official_test_matches_actual_float_decode': True,
                  'channels': channel_diagnostics(rgba, back, normal),
                  'files': {p.name: sha(p.read_bytes()) for p in directory.iterdir() if p.is_file()}}
        return encoded[16:], encoded[:16], report


def rebuild_table(prefix, tab, blocks):
    result = bytearray(prefix)
    start = len(result)
    for i, block in enumerate(blocks):
        field = tab + 8 * i + 4
        struct.pack_into('<Ii', result, tab + 8 * i, len(block), start - field)
        result += block
        start += len(block)
    return bytes(result)


def convert(data, entry, encoder, directory, block=6):
    tab, first, old, _ = full_texture(data, entry)
    if block not in (4, 6):
        raise ValueError('unsupported ASTC sample block size')
    formats = GAME_FORMAT4 if block == 4 else GAME_FORMAT
    new = desc(entry, (NVN_ASTC4 if block == 4 else NVN_ASTC6)[entry['format']])
    offsets = geometry(new)
    storage = C.create_string_buffer(offsets[-1])
    original_mips, encoded_mips, reports = [], [], []
    normal, srgb = entry['filename'].lower().endswith('_nrm.texture'), entry['format'] == 62
    for level in range(entry['levels']):
        w, h = dimensions(old.width, old.height, level)
        linear = deswizzle(old, data[first:], level, 4)
        original_mips.append(linear)
        rgba = decode_bc3(linear, w, h)
        packed, astc_header, report = encoder.mip(rgba, directory / f'mip-{level}', srgb, normal, block)
        swizzle(new, storage, level, packed, block)
        extracted = deswizzle(new, storage.raw, level, block)
        if extracted != packed:
            raise ValueError('ASTC compressed blocks changed during NVN retile roundtrip')
        rt = directory / f'mip-{level}' / 'retiled.astc'
        rt.write_bytes(astc_header + extracted)
        encoder.invoke(['-d' + ('s' if srgb else 'l'), str(rt), str(rt.with_suffix('.png'))], rt.with_suffix('.log'))
        if np.any(np.array(Image.open(rt.with_suffix('.png')).convert('RGBA')) !=
                  np.array(Image.open(rt.parent / 'decoded.png').convert('RGBA'))):
            raise ValueError('retiled ASTC decoded pixels differ')
        report.update({'level': level, 'old_linear_sha256': sha(linear),
                       'new_linear_sha256': sha(packed), 'retiled_sha256': sha(rt.read_bytes()),
                       'original_C_upload_geometry': upload_geometry(old, level),
                       'converted_C_upload_geometry': upload_geometry(new, level),
                       'retile_block_and_pixel_roundtrip': True})
        encoded_mips.append(packed)
        reports.append(report)
    prefix = bytearray(data[:first]);prefix[4] = formats[entry['format']]
    # Full .texture table sizes describe NVN padded mips, unlike thumbnails.
    tiled = storage.raw
    output = rebuild_table(prefix, tab, [tiled[offsets[i]:offsets[i + 1]] for i in range(new.levels)])
    changed = dict(entry);changed['format'] = formats[entry['format']]
    thumbnail = base64.b64decode(entry.get('thumbnailBase64', ''), validate=True)
    thumb_report = None
    if thumbnail:
        if len(thumbnail) < 8:
            raise ValueError('invalid thumbnail header')
        n = struct.unpack_from('<I', thumbnail, 0)[0]
        ttab = 4 + struct.unpack_from('<i', thumbnail, 4)[0]
        spans = mip_table(thumbnail, n, ttab)
        base = entry['thumbnailMip']
        if not isinstance(base, int) or base < 0 or base + n != entry['levels']:
            raise Unsupported('thumbnailMip/count does not describe the actual remaining 2D mips')
        for i, (start, size) in enumerate(spans):
            w, h = dimensions(old.width, old.height, base + i)
            if size != block_bytes(w, h, 4) or thumbnail[start:start + size] != original_mips[base + i]:
                raise Unsupported('thumbnail is not the exact compact linear original mip chain')
        replacement = rebuild_table(thumbnail[:ttab + 8 * n], ttab, encoded_mips[base:])
        changed['thumbnailBase64'] = base64.b64encode(replacement).decode('ascii')
        thumb_report = {'thumbnailMip': base, 'dimensions': list(dimensions(old.width, old.height, base)),
                        'levels': n, 'old_bytes': len(thumbnail), 'new_bytes': len(replacement),
                        'old_sha256': sha(thumbnail), 'new_sha256': sha(replacement),
                        'source_matches_full_original_linear_mips': True}
        spans = mip_table(replacement, n, ttab)
        for i, (start, size) in enumerate(spans):
            if replacement[start:start + size] != encoded_mips[base + i]:
                raise ValueError('thumbnail rebuilt offset/block mismatch')
    # Parse the output with its new descriptor and original C geometry too.
    spans = mip_table(output, changed['levels'], tab)
    for i, (start, size) in enumerate(spans):
        if start != first + offsets[i] or size != offsets[i + 1] - offsets[i]:
            raise ValueError('rebuilt full mip table mismatch')
    allowed = {4, *range(tab, first)}
    if any(data[i] != output[i] for i in range(first) if i not in allowed):
        raise ValueError('repack changed non-format, non-mip-table header data')
    report = {'filename': entry['filename'], 'dimensions': [old.width, old.height],
              'ASTC_block_size': f'{block}x{block}',
              'levels': old.levels, 'format': [entry['format'], changed['format']],
              'input_sha256': sha(data), 'output_sha256': sha(output),
              'file_bytes': [len(data), len(output)], 'full_padded_payload_bytes': [len(data) - first, len(tiled)],
              'linear_sampled_block_bytes': [sum(len(x) for x in original_mips), sum(len(x) for x in encoded_mips)],
              'C_NVN_storage_bytes': [geometry(old)[-1], offsets[-1]],
              'actual_VkImage_memory_requirements': 'not measured; driver/device-dependent, no GPU used',
              'layoutData_unchanged': changed['layoutData'], 'thumbnail': thumb_report, 'mips': reports}
    return output, changed, report


def tool_inputs(encoder):
    """Fingerprint existing tools/layout implementations rather than copy them."""
    import PIL
    from PIL import _imaging
    here = Path(__file__).resolve().parent
    dm = here.parent
    paths = [Path(__file__), here / 'astc_pack.py', Path(romfs_art.__file__), Path(touch_layout.__file__),
             dm / 'gfx/nvn/nvn_tex.c', dm / 'gfx/nvn/nvn_tex.h',
             dm / 'gfx/tex/nvn_deswizzle.c', dm / 'gfx/tex/nvn_deswizzle.h',
             Path(_imaging.__file__), Path(romfs_art.lib()._name)]
    dependencies = subprocess.run(['ldd', encoder.path], capture_output=True, text=True)
    for line in dependencies.stdout.splitlines():
        candidates = line.replace('=>', ' ').split()
        for text in candidates:
            if text.startswith('/') and Path(text).is_file():
                paths.append(Path(text).resolve())
    return {'Pillow': PIL.__version__, 'numpy': np.__version__,
            'clang': subprocess.check_output(['clang', '--version'], text=True).splitlines()[0],
            'files': {str(p): sha(p.read_bytes()) for p in paths},
            'ldd_returncode': dependencies.returncode, 'ldd_output': dependencies.stdout}


def run(options):
    if not options.only or not 1 <= options.limit <= 16:
        raise SystemExit('ASTC6x6 is sample-only: require --only and --limit1..16; no complete replacement/deployment')
    root, out = Path(options.romfs), Path(options.out)
    out.mkdir(parents=True, exist_ok=True)
    if (out / 'repack-report.json').exists() or (out / 'repl.tsv').exists():
        raise SystemExit('ASTC6x6 sample output must be new (never reuse stale success artifacts)')
    encoder = Encoder(options.astcenc, options.preset, options.threads)
    rows = [line.rstrip('\n').split('|') for line in (root / 'data/build_manifest.psv').open(encoding='utf-8')]
    logical_cache = 'data/hd/texture_desc_cache.json'
    cache_row = next(f for f in rows if len(f) >= 11 and f[9].split(':', 1)[-1].lower() == logical_cache)
    cache_bytes = touch_layout._read_entry(str(root), cache_row)
    cache = json.loads(cache_bytes)
    entries = {e['filename'].lower(): (i, e) for i, e in enumerate(cache)}
    if len(entries) != len(cache):
        raise ValueError('duplicate case-insensitive texture descriptor names')
    selected = {}
    for f in rows:
        if len(f) >= 11:
            name = f[9].split(':', 1)[-1].lower()
            if name.endswith('.texture') and options.only.lower() in name:
                selected.setdefault(name, f)
    if not selected:
        raise ValueError('no textures matched the bounded sample selection')
    report = {'complete': False, 'experimental': 'ASTC6x6 offline 2D bounded samples',
              'normal_recipe': options.normal_block_size,
              'not_deployed': True, 'GPU_bandwidth_not_measured': True,
              'encoder': {'path': encoder.path, 'sha256': sha(Path(encoder.path).read_bytes()),
                          'version': encoder.version, 'preset': options.preset, 'threads': options.threads},
              'manifest_sha256': sha((root / 'data/build_manifest.psv').read_bytes()),
              'original_desc_cache_sha256': sha(cache_bytes), 'converted': [], 'unsupported': []}
    report_path = out / 'repack-report.json'
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    replacements = []
    for name, row in list(selected.items())[:options.limit]:
        data = touch_layout._read_entry(str(root), row)
        if name not in entries:
            raise ValueError('selected texture is absent from original descriptor cache: ' + name)
        index, entry = entries[name]
        try:
            desc(entry, NVN_BC3.get(entry.get('format'), 0))
            block = 4 if name.endswith('_nrm.texture') and options.normal_block_size == '4x4' else 6
            output, changed, item = convert(data, entry, encoder, out / 'quality' / name, block)
        except Unsupported as error:
            report['unsupported'].append({'filename': name, 'reason': str(error), 'input_sha256': sha(data)})
            continue
        target = out / 'files' / name
        target.parent.mkdir(parents=True, exist_ok=True);target.write_bytes(output)
        cache[index] = changed
        replacements.append((name, target))
        report['converted'].append(item)
    destination = out / 'files' / logical_cache
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(cache, ensure_ascii=False, separators=(',', ':')))
    replacements.append((logical_cache, destination))
    (out / 'repl.tsv').write_text(''.join(f'{name}\t{path}\n' for name, path in replacements))
    report['commands'] = encoder.commands
    report['tool_inputs'] = tool_inputs(encoder)
    report['output_files'] = {str(p.relative_to(out)): sha(p.read_bytes()) for p in out.rglob('*')
                              if p.is_file() and p != report_path}
    report['complete'] = True
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    print(f'ASTC6x6 samples: {len(report["converted"])} converted, {len(report["unsupported"])} unsupported; {report_path}')
