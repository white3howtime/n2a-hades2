import hashlib, os, struct, sys, tempfile, unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import romfs_build
from romfs_inventory import ImageReader


def make_tree(root: Path, spec: dict):
    for rel, data in spec.items():
        p = root / rel
        if data is None:
            p.mkdir(parents=True, exist_ok=True)
        else:
            p.parent.mkdir(parents=True, exist_ok=True); p.write_bytes(data)


def check_image(test, src: Path, image: Path):
    """Parse with the independent reader, then compare every file byte for byte."""
    reader = ImageReader(image)
    try:
        expected = {}
        for r, ds, fs in os.walk(src):
            for f in fs:
                path = Path(r) / f
                expected[path.relative_to(src).as_posix()] = path.read_bytes()
        test.assertEqual(set(reader.paths), set(expected))
        for name, data in expected.items():
            entry = reader.find_file(name)
            test.assertIsNotNone(entry, name)
            test.assertEqual(entry["size"], len(data))
            test.assertEqual(reader.file_digest(entry), hashlib.sha256(data).hexdigest(), name)
        dirs = {p for p in reader.directory_paths if p}
        want = set()
        for r, ds, fs in os.walk(src):
            for d in ds: want.add((Path(r) / d).relative_to(src).as_posix())
        test.assertEqual(dirs, want)
    finally:
        reader.close()


class RomfsBuildTest(unittest.TestCase):
    def build(self, spec):
        d = Path(tempfile.mkdtemp())
        src = d / "src"; src.mkdir(); make_tree(src, spec)
        image = d / "image"
        info = romfs_build.build(src, image)
        self.assertEqual(info["bytes"], image.stat().st_size)
        self.assertEqual(info["sha256"], hashlib.sha256(image.read_bytes()).hexdigest())
        return src, image, info

    def test_small_tree(self):
        src, image, info = self.build({"a.txt": b"hello", "d/b.bin": bytes(range(256)) * 9, "d/e/f": b"x", "empty": b"", "emptydir": None})
        check_image(self, src, image)
        self.assertEqual((info["files"], info["directories"]), (4, 4))

    # An image with no files at all is not tested: romfs_inventory.py's reader rejects an empty file table, and no game ships one.

    def test_many_entries_force_hash_chains(self):
        spec = {f"dir{i % 7}/file_{i:04d}.dat": f"payload {i}".encode() * (i % 5) for i in range(400)}
        src, image, info = self.build(spec)
        check_image(self, src, image)
        self.assertEqual(info["files"], 400)
        self.assertGreater(romfs_build.bucket_count(400), 3)

    def test_names(self):
        spec = {"same/x": b"1", "other/same/x": b"2", "unicode/\u4e2d\u6587.txt": b"u", "n" * 250 + "/f": b"long", ".nrr/game.nrr": b"nrr", "UPPER/lower": b"c"}
        src, image, _ = self.build(spec)
        check_image(self, src, image)

    def test_deterministic(self):
        spec = {f"d{i}/f{j}": bytes([i, j]) * 30 for i in range(5) for j in range(6)}
        _, a, ia = self.build(spec)
        _, b, ib = self.build(spec)
        self.assertEqual(ia["sha256"], ib["sha256"])

    def test_payload_alignment(self):
        src, image, _ = self.build({"a": b"1" * 17, "b": b"2" * 5, "c": b"3"})
        reader = ImageReader(image)
        try:
            for name in ("a", "b", "c"):
                self.assertEqual(reader.find_file(name)["data"] % romfs_build.DATA_ALIGN, 0)
        finally:
            reader.close()

    def test_rejects_unusable_names(self):
        d = Path(tempfile.mkdtemp()); src = d / "s"; src.mkdir()
        (src / "ninechars").write_bytes(b"")
        old, romfs_build.MAX_NAME = romfs_build.MAX_NAME, 8
        try:
            with self.assertRaises(ValueError):
                romfs_build.build(src, d / "i")
        finally:
            romfs_build.MAX_NAME = old

    def test_reader_catches_corruption(self):
        """The reader must notice a broken image, otherwise check_image proves nothing."""
        src, image, _ = self.build({"a": b"1", "b": b"2", "c/d": b"3"})
        raw = bytearray(image.read_bytes())
        o1 = struct.unpack_from("<Q", raw, 8)[0]     # directory hash table
        struct.pack_into("<I", raw, o1, 0x1234)       # point a bucket at a bogus entry
        bad = image.with_suffix(".bad"); bad.write_bytes(raw)
        with self.assertRaises(ValueError):
            ImageReader(bad)


if __name__ == "__main__":
    unittest.main()
