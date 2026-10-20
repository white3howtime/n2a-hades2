import hashlib, json, os, shlex, sys, tempfile, unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hades2_push as hp


class FakeDevice:
    """Interprets the adb command lines hades2_push.py sends. The 'device' is a directory; a few faults can be injected."""

    def __init__(self, root: Path, package="org.test.app", free_kib=10_000_000):
        self.root, self.package, self.free_kib = root, package, free_kib
        self.log, self.truncate_next, self.corrupt_next, self.fail_next_transfer = [], False, False, False

    def path(self, rel):
        assert not rel.startswith("/") and ".." not in rel, rel
        return self.root / rel

    def __call__(self, argv, stdin_path=None, stdin_bytes=None):
        kind = argv[-2]
        inner = shlex.split(argv[-1].split(f"run-as {self.package} ", 1)[1])   # ['sh', '-c', '<command>']
        command = inner[2]
        self.log.append((kind, command))
        if kind == "exec-in":
            target = command.split("cat > ", 1)[1]
            if self.fail_next_transfer:
                self.fail_next_transfer = False
                return 1, b"closed"
            data = Path(stdin_path).read_bytes() if stdin_path else stdin_bytes
            if self.truncate_next:
                data, self.truncate_next = data[:-1], False
            if self.corrupt_next:
                data, self.corrupt_next = bytes([data[0] ^ 1]) + data[1:], False
            self.path(target).parent.mkdir(parents=True, exist_ok=True)
            self.path(target).write_bytes(data)
            return 0, b""
        words = command.split()
        if command.startswith("df -k"):
            return 0, f"/dev/block 100 50 {self.free_kib} 50% /data\n".encode()
        if words[0] == "mkdir":
            for d in words[2:]: self.path(d).mkdir(parents=True, exist_ok=True)
            return 0, b""
        if words[0] == "stat":
            p = self.path(words[3])
            return (0, str(p.stat().st_size).encode()) if p.is_file() else (1, b"stat: No such file")
        if words[0] == "sha256sum":
            p = self.path(words[1])
            return (0, f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {words[1]}".encode()) if p.is_file() else (1, b"No such file")
        if words[0] == "mv":
            self.path(words[2]).parent.mkdir(parents=True, exist_ok=True)
            os.replace(self.path(words[1]), self.path(words[2]))
            return 0, b""
        if words[0] == "test":
            return (0 if self.path(words[2]).exists() else 1), b""
        raise AssertionError("unexpected device command: " + command)


def make_inputs(root: Path, build="8ce1ac48e9fa74479051f3520e632df4"):
    modules = root / "modules"; modules.mkdir()
    for name in hp.MODULES:
        (modules / name).write_bytes(name.encode() * 50)
    (modules / "main.fixups.tsv").write_text(f"# BUILD_ID\t{build}\n# PATCH_OFF\t0x10000\n")
    romfs = root / "game.romfs"; romfs.write_bytes(bytes(range(256)) * 300)
    shaders = root / "shaders"; shaders.mkdir()
    (shaders / "shader_index.bin").write_bytes(b"index")
    for key in ("0123456789abcdef", "fedcba9876543210"):
        (shaders / f"{key}.spv.gz").write_bytes(key.encode() * 3); (shaders / f"{key}.vkmeta").write_bytes(b"meta" + key.encode())
    return modules, romfs, shaders


class PushTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp()); self.inputs = make_inputs(self.tmp)
        self.dev = FakeDevice(self.tmp / "device")
        self.adb = hp.Adb("org.test.app", runner=self.dev)

    def plan(self): return hp.build_plan(*self.inputs)

    def push(self, **kw):
        manifest, items = self.plan()
        return hp.push(self.adb, items, manifest, log=lambda m: None, **kw), manifest, items

    def test_manifest_contents(self):
        manifest, items = self.plan()
        self.assertEqual((manifest["format"], manifest["title_id"], manifest["build_id"]), (1, "0100a00019de0000", "8ce1ac48e9fa74479051f3520e632df4"))
        self.assertEqual(sorted(e["name"] for e in manifest["files"]), sorted(hp.MODULES))
        self.assertEqual(manifest["romfs"]["bytes"], self.inputs[1].stat().st_size)
        self.assertEqual(manifest["shaders"]["programs"], 2)
        self.assertEqual(len(items), 6 + 1 + 4 + 1)
        for e in manifest["files"]:
            self.assertEqual(e["sha256"], hashlib.sha256((self.inputs[0] / e["name"]).read_bytes()).hexdigest())

    def test_push_places_every_file(self):
        (sent, skipped), manifest, items = self.push()
        self.assertEqual((sent, skipped), (len(items), 0))
        for local, dev, size, digest in items:
            self.assertEqual(self.dev.path(dev).read_bytes(), local.read_bytes())
        on_device = json.loads(self.dev.path("files/d2r-mobile/hades-manifest.json").read_text())
        self.assertEqual(on_device, manifest)
        self.assertFalse(list((self.tmp / "device").rglob("*.new")))

    def test_manifest_is_the_last_write(self):
        self.push()
        writes = [c for k, c in self.dev.log if k == "exec-in"]
        self.assertTrue(writes[-1].endswith("hades-manifest.json.new"))
        self.assertEqual(sum("hades-manifest" in w for w in writes), 1)

    def test_second_push_skips_everything(self):
        self.push()
        before = len([1 for k, _ in self.dev.log if k == "exec-in"])
        (sent, skipped), _, items = self.push()
        self.assertEqual((sent, skipped), (0, len(items)))
        after = len([1 for k, _ in self.dev.log if k == "exec-in"])
        self.assertEqual(after - before, 1)       # only the manifest is rewritten

    def test_changed_file_is_replaced(self):
        self.push()
        (self.inputs[0] / "sdk.gates.tsv").write_bytes(b"new gates")
        (sent, skipped), _, items = self.push()
        self.assertEqual((sent, skipped), (1, len(items) - 1))
        self.assertEqual(self.dev.path("files/d2r-mobile/modules/sdk.gates.tsv").read_bytes(), b"new gates")

    def test_same_size_different_content_is_replaced(self):
        self.push()
        target = self.inputs[0] / "sdk.fixups.tsv"
        old = target.read_bytes(); target.write_bytes(bytes([old[0] ^ 1]) + old[1:])       # same length, one bit different
        (sent, _), _, _ = self.push()
        self.assertEqual(sent, 1)
        self.assertEqual(self.dev.path("files/d2r-mobile/modules/sdk.fixups.tsv").read_bytes(), target.read_bytes())

    def test_empty_file_is_pushed(self):
        (self.inputs[0] / "main.gates.tsv").write_bytes(b"")
        self.push()
        self.assertEqual(self.dev.path("files/d2r-mobile/modules/main.gates.tsv").read_bytes(), b"")

    def test_truncated_transfer_is_caught_and_leaves_no_final_file(self):
        self.dev.truncate_next = True
        with self.assertRaises(SystemExit) as c: self.push()
        self.assertIn("bytes after transfer", str(c.exception))
        self.assertFalse(self.dev.path("files/d2r-mobile/hades-manifest.json").exists())

    def test_corrupted_transfer_is_caught(self):
        self.dev.corrupt_next = True
        with self.assertRaises(SystemExit) as c: self.push()
        self.assertIn("SHA-256 differs", str(c.exception))

    def test_quick_mode_checks_size_only(self):
        self.dev.corrupt_next = True      # same length, wrong content: only a hash check would notice
        (sent, _), _, _ = self.push(quick=True)
        self.assertGreater(sent, 0)

    def test_adb_daemon_start_noise_is_ignored(self):
        real = self.dev.__call__
        noisy = {"left": 0}

        def runner(argv, stdin_path=None, stdin_bytes=None):
            rc, out = real(argv, stdin_path, stdin_bytes)
            if argv[-2] == "shell":                      # every query answer gets the noise appended
                noisy["left"] += 1
                out = out + b"\r\n* daemon not running; starting now at tcp:5037\r\n* daemon started successfully"
            return rc, out
        self.adb.runner = runner
        (sent, _), _, items = self.push()
        self.assertEqual(sent, len(items))
        self.assertGreater(noisy["left"], 10)

    def test_adb_failure_aborts(self):
        self.dev.fail_next_transfer = True
        with self.assertRaises(SystemExit): self.push()

    def test_interrupted_push_resumes(self):
        self.dev.truncate_next = True
        with self.assertRaises(SystemExit): self.push()
        (sent, skipped), _, items = self.push()
        self.assertEqual(sent + skipped, len(items))
        self.assertTrue(self.dev.path("files/d2r-mobile/hades-manifest.json").exists())

    def test_not_enough_space(self):
        self.dev.free_kib = 1
        with self.assertRaises(SystemExit) as c: self.push()
        self.assertIn("not enough free space", str(c.exception))

    def test_input_validation(self):
        modules, romfs, shaders = self.inputs
        (modules / "main.fixups.tsv").write_text("nothing here\n")
        with self.assertRaises(SystemExit): hp.build_plan(modules, romfs, shaders)
        (modules / "main.fixups.tsv").write_text("# BUILD_ID\t8ce1ac48e9fa74479051f3520e632df4\n")
        (shaders / "fedcba9876543210.vkmeta").unlink()
        with self.assertRaises(SystemExit): hp.build_plan(modules, romfs, shaders)
        (shaders / "fedcba9876543210.vkmeta").write_bytes(b"m")
        (modules / "sdk.patched.bin").unlink()
        with self.assertRaises(SystemExit): hp.build_plan(modules, romfs, shaders)


if __name__ == "__main__":
    unittest.main()
