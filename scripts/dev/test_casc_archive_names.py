#!/usr/bin/env python3
"""Owned BLTE/TVFS fixtures; no game files, GPU, or Android device required."""
import concurrent.futures
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import warnings
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "dm/tools"))
sys.path.insert(0, str(ROOT / "dm/ios/tools"))
import casc_overlay as casc
import dmx
import touch_layout


def tvfs(key, encoded_size, decoded_size):
    data = bytearray(46)
    data[:4] = b"TVFS"
    struct.pack_into(">II", data, 28, 46, 25)
    struct.pack_into(">II", data, 38, 71, 4)
    return data + key + struct.pack(">IBI", encoded_size, 0, decoded_size) + b"b:z\0"


def source_fixture(root):
    data = root / "data"
    data.mkdir(parents=True)
    content = b'{"owned": "CASC test", "value": 17}\n'
    encoded = casc.blte_encode(content)
    file_key = casc.ekey_for(encoded, 0, 1, 0)
    vfs = tvfs(file_key, len(encoded), len(content))
    vfs_encoded = casc.blte_encode(vfs)
    vfs_key = casc.ekey_for(vfs_encoded, 0, 1, len(encoded))
    root_vfs = tvfs(vfs_key, len(vfs_encoded), len(vfs))
    root_encoded = casc.blte_encode(root_vfs)
    root_key = casc.ekey_for(root_encoded, 0, 1, len(encoded) + len(vfs_encoded))
    (data / "00-00000001.data").write_bytes(encoded + vfs_encoded + root_encoded)
    lines = []
    for name, body, packed, key in [("vfs-1", vfs, vfs_encoded, vfs_key),
                                    ("vfs-root", root_vfs, root_encoded, root_key),
                                    ("vfs-2", root_vfs, root_encoded, root_key)]:
        lines.extend([f"{name} = {hashlib.md5(body).hexdigest()} {key.hex()}\n",
                      f"{name}-size = {len(body)} {len(packed)}\n"])
    (data / ".build.config").write_text("".join(lines))
    row = ["1", "0", "0", "1", hashlib.md5(content).hexdigest(), str(len(content)),
           file_key.hex(), str(len(encoded)), "b:z", "data:data/own.json", "0"]
    (data / "build_manifest.psv").write_text("# owned fixture\n" + "|".join(row) + "\n")
    for archive in range(2, 10):
        (data / f"00-{archive:08d}.data").write_bytes(b"owned occupied slot")
    return content, row


class ArchiveNames(unittest.TestCase):
    def test_declared_resource_encodings_preserve_default_BLTE(self):
        with tempfile.TemporaryDirectory(prefix="casc-resource-encoding-") as directory:
            data=Path(directory)
            content=b"owned original encoding contract\0" * 37
            for archive, encoding in enumerate(("n", "z", "b:z"), 10):
                encoded=casc.encode_by_espec(content,encoding)
                key=casc.ekey_for(encoded,0,archive,0)
                (data/casc.data_filename(0,archive)).write_bytes(encoded)
                self.assertEqual(casc.read_data_file(data,key,len(encoded),encoding=encoding),content)
                if encoding=="b:z":
                    self.assertEqual(casc.read_data_file(data,key,len(encoded)),content)
            with self.assertRaises(SystemExit):
                casc.read_data_file(data,key,len(encoded),encoding="unknown")

    def test_format_and_cli_compatibility(self):
        for chunk in [0, 9, 10, 17, 99, 100, 255]:
            for archive in range(10):
                self.assertEqual(casc.data_filename(chunk, archive), f"{chunk:02d}-{archive:08d}.data")
        self.assertEqual(casc.data_filename(10, 11), "10-0000000b.data")
        self.assertEqual(casc.data_filename(17, 16), "17-00000010.data")
        for text in ["0-10", "00-00000010", "0x0-0xa"]:
            self.assertEqual(casc.parse_pack_location(text), (0, 10))
        for text in ["0-256", "256-1", "-1-2", "1", "1-0b"]:
            with self.assertRaises(SystemExit):
                casc.parse_pack_location(text)

    def test_available_location_hex_and_ekey_bounds(self):
        with tempfile.TemporaryDirectory() as tmp:
            data = Path(tmp)
            for archive in range(1, 10):
                (data / f"00-{archive:08d}.data").touch()
            self.assertEqual(casc.available_location(data), (0, 10))
            (data / "00-0000000A.data").touch()
            self.assertEqual(casc.available_location(data), (0, 11))
            (data / "00-00000010.data").touch()  # Hex16, not decimal10.
            self.assertEqual(casc.available_location(data), (0, 11))
            for archive in range(11, 256):
                (data / f"00-{archive:08x}.data").touch()
            self.assertEqual(casc.available_location(data), (1, 1))
        entries = [Path(f"{chunk:02d}-{archive:08x}.data")
                   for chunk in range(101) for archive in range(1, 256)]
        directory = mock.Mock()
        directory.glob.return_value = entries
        self.assertEqual(casc.available_location(directory), (101, 1))
        directory.glob.return_value = [Path(f"{chunk:02d}-{archive:08x}.data")
                                       for chunk in range(256) for archive in range(1, 256)]
        with self.assertRaisesRegex(SystemExit, "no unused"):
            casc.available_location(directory)

    def test_real_pack_and_all_readers(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            content, row = source_fixture(source)
            replacement = root / "own.json"
            replacement.write_bytes(content)
            output = root / "pack"
            replacements = root / "replacements.tsv"
            replacements.write_text(f"data/own.json\t{replacement}\n")
            executor = concurrent.futures.ProcessPoolExecutor
            with mock.patch("concurrent.futures.ProcessPoolExecutor",
                            side_effect=lambda: executor(max_workers=2)):
                casc.main(["casc_overlay.py", "pack", str(source), str(output),
                           str(replacements), "10-11"])
            packed_file = output / "10-0000000b.data"
            self.assertTrue(packed_file.is_file())
            self.assertFalse((output / "10-00000011.data").exists())
            entry = casc.read_prebuilt(output / "pack_index.tsv")["data/own.json"]
            key, encoded_size, decoded_size = entry
            self.assertEqual((key[9], key[10]), (10, 11))
            self.assertEqual(casc.read_data_file(output, key, encoded_size), content)
            self.assertEqual(decoded_size, len(content))
            row[2], row[3], row[6], row[7] = "10", "11", key.hex(), str(encoded_size)
            (output / "build_manifest.psv").write_text("# owned fixture\n" + "|".join(row) + "\n")
            with mock.patch.object(dmx, "ROOT", str(output)):
                try:
                    self.assertEqual(dmx.read_row(row), content)
                finally:
                    for stream in dmx._FH.values():
                        stream.close()
                    dmx._FH.clear()
            romfs = root / "packed-romfs"
            romfs.mkdir()
            (romfs / "data").symlink_to(output)
            self.assertEqual(touch_layout._read_entry(str(romfs), row), content)
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", ResourceWarning)  # Existing reader returns inside open iteration.
                self.assertEqual(touch_layout.read_layout(str(romfs), "", suffix="own.json"),
                                 ("data/own.json", content.decode()))
            packed = packed_file.read_bytes()
            block_offset = struct.unpack_from(">I", packed, 4)[0] + 1
            block = packed[block_offset:]
            trace = root / "trace.tsv"
            trace.write_text(f"{block[:16].hex()}\t{len(block)}\t{len(content)}\t{row[4]}\t0x1\n")
            nso = root / "owned-header"
            nso.write_bytes(bytes(80))  # Only the existing bootlist's build-ID header read.
            bootlist = root / "bootlist.tsv"
            subprocess.run([sys.executable, str(ROOT / "dm/tools/tact_bootlist.py"),
                            str(trace), str(bootlist), str(romfs), str(nso)], check=True)
            self.assertIn(f"10-0000000b.data\t{block_offset}\t", bootlist.read_text())

    def test_actual_overlay_crosses_nine(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / "source"
            source_fixture(source)
            replacement = root / "replacement"
            content = bytes(range(256)) * 2200 + b"tail"  # Multiple BLTE blocks.
            replacement.write_bytes(content)
            output = root / "overlay"
            casc.make_overlay(source, output, {"data/own.json": replacement})
            self.assertTrue((output / "data/00-0000000a.data").is_file())
            self.assertFalse((output / "data/00-00000010.data").exists())
            vfs = casc.load_vfs1(output)
            key = bytes(vfs[46:62])
            encoded_size = struct.unpack_from(">I", vfs, 62)[0]
            self.assertEqual((key[9], key[10]), (0, 10))
            self.assertEqual(casc.read_data_file(output / "data", key, encoded_size), content)
            _, values = casc.config_values(output / "data/.build.config")
            root_key = bytes.fromhex(values["vfs-root"].split()[1])
            root_size = int(values["vfs-root-size"].split()[1])
            decoded_root = casc.read_data_file(output / "data", root_key, root_size)
            self.assertEqual(decoded_root[46:62], bytes.fromhex(values["vfs-1"].split()[1]))
            self.assertEqual(casc.available_location(output / "data"), (0, 11))


if __name__ == "__main__":
    unittest.main()
