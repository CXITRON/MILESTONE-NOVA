import importlib.util
import pathlib
import struct
import tempfile
import unittest
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts" / "media" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


key, media, helper = load("track_key"), load("media_pack"), load("ble_helper")


class ToolsTests(unittest.TestCase):
    def test_keys(self):
        self.assertEqual(key.track_key("한글 Artist", "NOVA", "앨범", 231000), "51877402f0e5f146")
        self.assertNotEqual(key.track_key("ab", "c", "", 1000), key.track_key("a", "bc", "", 1000))
        self.assertEqual(len(key.track_key("한글", "test", "", 0)), 16)

    def test_media_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            base = pathlib.Path(directory)
            source = base / "red.ppm"
            source.write_bytes(b"P6\n# test\n160 160\n255\n" + bytes((255, 0, 0)) * 25600)
            image = base / "red.nvi"
            media.pack([source], image)
            data = image.read_bytes()
            magic, width, height, length, crc = struct.unpack("<4sHHII", data[:16])
            self.assertEqual((magic, width, height, length), (b"NVI1", 160, 160, 51200))
            self.assertEqual(data[16:18], b"\x00\xf8")
            self.assertEqual(crc, zlib.crc32(data[16:]))
            with self.assertRaises(FileExistsError):
                media.pack([source], image)
            video = base / "video.nvv"
            media.pack([source, source], video, 5)
            data = video.read_bytes()
            self.assertEqual(struct.unpack("<4sHHHHI", data[:16]), (b"NVV1", 160, 160, 5, 0, 2))
            self.assertEqual(len(data), 16 + 51204 * 2)

    def test_invalid_media(self):
        with self.assertRaises(ValueError):
            media.pack([], "unused.nvi")
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "bad.ppm"
            source.write_bytes(b"P6\n160 160\n255\nshort")
            output = pathlib.Path(directory) / "bad.nvi"
            with self.assertRaises(ValueError):
                media.pack([source], output)
            self.assertFalse(output.exists())

    def test_helper_small_mtu(self):
        snapshot = dict(title="긴 한글 제목 English " * 15, artist="", album="", duration_ms=10000, position_ms=1234, playing=True)
        packets = list(helper.packets(snapshot, 55))
        self.assertTrue(all(len(p) <= 20 for p in packets))
        title = b"".join(p[6:] for p in packets if p[1] == 2)
        self.assertLessEqual(len(title), 240)
        title.decode("utf-8")
        for packet in packets:
            if packet[1] in (2, 3, 4):
                packet[6:].decode("utf-8")
        self.assertEqual(packets[-1][1], 6)
        with self.assertRaises(ValueError):
            list(helper.packets({"title": "missing data"}, 1))

    def test_partitions(self):
        partitions = []
        for line in (ROOT / "partitions.csv").read_text().splitlines():
            if not line or line.startswith("#"):
                continue
            fields = [field.strip() for field in line.split(",")]
            start, size = int(fields[3], 0), int(fields[4], 0)
            self.assertLessEqual(start + size, 16 * 1024 * 1024)
            self.assertTrue(all(start >= old_start + old_size or start + size <= old_start for old_start, old_size in partitions))
            partitions.append((start, size))
        self.assertEqual(partitions[2][1], partitions[3][1])


if __name__ == "__main__":
    unittest.main()
