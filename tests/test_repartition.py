"""Exercise USB migration against a byte-addressed flash; no hardware access."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('repartition', ROOT / 'scripts/build/repartition.py')
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


def table(layout):
    data = b''.join(struct.pack('<HBBII16sI', 0x50AA, t, st, start, size, name.encode(), flags)
                    for name, (t, st, start, size, flags) in layout.items())
    data += b'\xeb\xeb' + b'\xff' * 14 + hashlib.md5(data).digest()
    return data.ljust(4096, b'\xff')


def bundle(path):
    path.mkdir()
    app = bytearray(b'\x00' * 8192)
    app[0] = 0xE9
    app[12:14] = (9).to_bytes(2, 'little')
    app[32:36] = (0xABCD5432).to_bytes(4, 'little')
    app[48:54] = b'0.1.11'
    app[80:94] = b'MILESTONE-NOVA'
    # Slice replacement above must preserve descriptor widths.
    (path / r.FILES[0]).write_bytes(app)
    (path / r.FILES[1]).write_bytes(table(r.TARGET))
    (path / r.FILES[2]).write_bytes(b'\xff' * 8192)
    (path / 'manifest.json').write_text(json.dumps({'format': 1, 'version': '0.1.11',
        'sha256': {name: r.sha((path / name).read_bytes()) for name in r.FILES}}))


class Flash:
    def __init__(self, layout):
        self.data = bytearray(b'\xA5' * r.FLASH_SIZE)
        self.data[0x8000:0x9000] = table(layout)
        self.calls = []
        self.fail_verify = False

    def run(self, command, *args):
        self.calls.append((command, *args))
        start = int(args[0], 0)
        if command == 'read-flash':
            size = int(args[1], 0)
            Path(args[2]).write_bytes(self.data[start:start + size])
        elif command == 'verify-flash':
            value = Path(args[1]).read_bytes()
            if self.fail_verify or self.data[start:start + len(value)] != value:
                raise ValueError('Injected verify failure')
        elif command == 'write-flash':
            value = Path(args[1]).read_bytes()
            self.data[start:start + len(value)] = value
        elif command == 'erase-region':
            size = int(args[1], 0)
            self.data[start:start + size] = b'\xff' * size
        else:
            raise AssertionError(command)


class RepartitionTests(unittest.TestCase):
    def test_known_layouts_preserve_and_switch_last(self):
        for layout in (r.OLD20, r.OLD64):
            with self.subTest(size=layout['nvs'][3]), tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                bundle(base / 'payload')
                flash = Flash(layout)
                original_nvs = bytes(flash.data[0x9000:0x9000 + layout['nvs'][3]])
                self.assertTrue(r.migrate(base / 'payload', base, flash.run, lambda _: 'MIGRATE'))
                self.assertEqual(r.partitions(bytes(flash.data[0x8000:0x9000])), r.TARGET)
                self.assertEqual(flash.data[0x9000:0x29000], original_nvs +
                                 b'\xff' * (0x20000 - len(original_nvs)))
                self.assertEqual((base / 'flash-before.bin').stat().st_size, r.FLASH_SIZE)
                writes = [c for c in flash.calls if c[0] in ('write-flash', 'erase-region')]
                self.assertEqual(writes[-1][1], '0x8000')
                self.assertTrue((base / 'completed.json').exists())

    def test_cancel_or_bad_backup_never_writes(self):
        for fail in (False, True):
            with tempfile.TemporaryDirectory() as tmp:
                base = Path(tmp)
                bundle(base / 'payload')
                flash = Flash(r.OLD20)
                flash.fail_verify = fail
                if fail:
                    with self.assertRaises(ValueError):
                        r.migrate(base / 'payload', base, flash.run, lambda _: 'MIGRATE')
                else:
                    self.assertFalse(r.migrate(base / 'payload', base, flash.run, lambda _: ''))
                self.assertFalse(any(c[0] in ('write-flash', 'erase-region') for c in flash.calls))

    def test_reject_unknown_repeated_and_corrupt_table(self):
        for layout in (r.TARGET, {**r.OLD20, 'extra': (1, 0x81, 0xD00000, 4096, 0)}):
            with self.assertRaises(ValueError):
                r.plan(layout)
        corrupt = bytearray(table(r.OLD20))
        corrupt[20] ^= 1
        with self.assertRaises(ValueError):
            r.partitions(bytes(corrupt))

    def test_bad_payload_rejected_before_device_contact(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            bundle(base / 'payload')
            (base / 'payload' / r.FILES[0]).write_bytes(b'broken')
            flash = Flash(r.OLD20)
            with self.assertRaises(ValueError):
                r.migrate(base / 'payload', base, flash.run, lambda _: 'MIGRATE')
            self.assertEqual(flash.calls, [])

    def test_app_verify_failure_does_not_switch_table(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            bundle(base / 'payload')
            flash = Flash(r.OLD20)
            def run(command, *args):
                if command == 'verify-flash' and args[0] == '0x30000':
                    raise ValueError('App verification failed')
                flash.run(command, *args)
            with self.assertRaises(ValueError):
                r.migrate(base / 'payload', base, run, lambda _: 'MIGRATE')
            self.assertEqual(r.partitions(bytes(flash.data[0x8000:0x9000])), r.OLD20)
            self.assertFalse((base / 'completed.json').exists())


if __name__ == '__main__':
    unittest.main()
