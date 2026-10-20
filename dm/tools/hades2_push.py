#!/usr/bin/env python3
"""Stage Hades II game data into the app's private storage over adb (debuggable APK, `run-as`).

    hades2_push.py --modules <dir> --romfs <game.romfs> --shaders <cache dir> --package <id> [--serial S] [--adb adb] [--quick]
    hades2_push.py ... --dry-run --manifest-out hades-manifest.json     # checks the inputs and writes the manifest; touches no device

Device layout (relative to the app's data directory):
    files/game.romfs
    files/d2r-mobile/hades-manifest.json
    files/d2r-mobile/modules/{main,sdk}.{patched.bin,fixups.tsv,gates.tsv}
    files/d2r-mobile/shaders/{<key>.spv.gz,<key>.vkmeta,shader_index.bin}
Files that are already on the device with the right size and SHA-256 are skipped, so an interrupted push can simply be repeated.
Every file is written to a ".new" name, checked, then renamed; the manifest goes last, so the app never sees a half-pushed set.
"""
import argparse, hashlib, json, re, shlex, subprocess, sys
from pathlib import Path

TITLE_ID = "0100a00019de0000"
MODULES = [f"{m}.{s}" for m in ("main", "sdk") for s in ("patched.bin", "fixups.tsv", "gates.tsv")]
BASE = "files/d2r-mobile"
CHUNK = 32 << 20     # files larger than this are sent chunk by chunk


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(8 << 20), b""):
            h.update(block)
    return h.hexdigest()


def build_plan(modules: Path, romfs: Path, shaders: Path):
    """Validate the inputs and return (manifest dict, [(local path, device path, size, sha256)])."""
    items, entries = [], []
    for name in MODULES:
        p = modules / name
        if not p.is_file() or p.stat().st_size == 0 and name.endswith(".patched.bin"):
            raise SystemExit(f"missing or empty module file: {p}")
        digest = sha256_file(p)
        entries.append({"name": name, "bytes": p.stat().st_size, "sha256": digest})
        items.append((p, f"{BASE}/modules/{name}", p.stat().st_size, digest))
    m = re.search(r"^# BUILD_ID\t([0-9a-f]{32})$", (modules / "main.fixups.tsv").read_text(encoding="utf-8"), re.M)
    if not m:
        raise SystemExit("main.fixups.tsv has no BUILD_ID line")
    if not romfs.is_file() or romfs.stat().st_size < 0x50:
        raise SystemExit(f"missing RomFS image: {romfs}")
    if not shaders.is_dir() or not (shaders / "shader_index.bin").is_file():
        raise SystemExit(f"shader cache needs shader_index.bin: {shaders}")
    keys = sorted(p.name[:-7] for p in shaders.glob("*.spv.gz"))
    if not keys:
        raise SystemExit("shader cache has no *.spv.gz")
    for key in keys:
        if not (shaders / f"{key}.vkmeta").is_file():
            raise SystemExit(f"shader {key} has no .vkmeta")
    shader_files = ["shader_index.bin"] + [f"{k}{ext}" for k in keys for ext in (".spv.gz", ".vkmeta")]
    for name in shader_files:
        p = shaders / name
        items.append((p, f"{BASE}/shaders/{name}", p.stat().st_size, sha256_file(p)))
    rom_hash = sha256_file(romfs)
    items.append((romfs, "files/game.romfs", romfs.stat().st_size, rom_hash))
    manifest = {"format": 1, "title_id": TITLE_ID, "build_id": m.group(1), "files": entries,
                "romfs": {"bytes": romfs.stat().st_size, "sha256": rom_hash},
                "shaders": {"programs": len(keys)}}
    return manifest, items


class Adb:
    """Thin wrapper; `runner(argv, stdin_path, stdin_bytes) -> (returncode, output)` is replaceable so the push logic can be tested without a device."""

    def __init__(self, package, adb="adb", serial=None, runner=None):
        self.package, self.base = package, [adb] + (["-s", serial] if serial else [])
        self.runner = runner or self._real

    @staticmethod
    def _real(argv, stdin_path=None, stdin_bytes=None):
        if stdin_path:
            with open(stdin_path, "rb") as f:
                p = subprocess.run(argv, stdin=f, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        else:
            p = subprocess.run(argv, input=stdin_bytes or b"", stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return p.returncode, p.stdout + p.stderr

    def sh(self, command, check=True):
        rc, out = self.runner(self.base + ["shell", f"run-as {self.package} sh -c {shlex.quote(command)}"], None, None)
        if check and rc:
            raise SystemExit(f"device command failed ({rc}): {command}\n{out.decode(errors='replace')[-500:]}")
        # adb prints "* daemon not running..." on stdout when it has to start its server; that is not part of the command's output.
        lines = [l for l in out.decode(errors="replace").splitlines() if not l.startswith("* daemon")]
        return rc, chr(10).join(lines).strip()

    def stream(self, remote_tmp: str, local: Path = None, data: bytes = None):
        rc, out = self.runner(self.base + ["exec-in", f"run-as {self.package} sh -c {shlex.quote('cat > ' + remote_tmp)}"],
                              str(local) if local else None, data)
        if rc:
            raise SystemExit(f"adb transfer failed ({rc}) for {local or remote_tmp}\n{out.decode(errors='replace')[-500:]}")

    def stream_chunked(self, remote_tmp: str, local: Path, size: int, chunk: int = None, tries: int = 4, log=print):
        """Resumable transfer of a large file: one adb call per chunk, so a dropped connection costs at most one chunk.
        Whatever is already in remote_tmp is kept up to the last whole chunk and the transfer continues from there."""
        chunk = chunk or CHUNK
        rc, out = self.sh(f"stat -c %s {remote_tmp}", check=False)
        have = int(out) if rc == 0 and out.isdigit() else 0
        have = min(have, size) // chunk * chunk
        if have:
            self.sh(f"truncate -s {have} {remote_tmp}")
            log(f"  resuming at {have >> 20} MiB")
        with open(local, "rb") as f:
            for offset in range(have, size, chunk):
                f.seek(offset)
                data = f.read(min(chunk, size - offset))
                for attempt in range(tries):
                    redirect = ">>" if offset else ">"
                    rc, out = self.runner(self.base + ["exec-in", f"run-as {self.package} sh -c {shlex.quote('cat ' + redirect + ' ' + remote_tmp)}"], None, data)
                    rc2, now = self.sh(f"stat -c %s {remote_tmp}", check=False)
                    if rc == 0 and rc2 == 0 and now.isdigit() and int(now) == offset + len(data):
                        break
                    log(f"  chunk at {offset >> 20} MiB failed (adb rc {rc}, device has {now or '?'}), retry {attempt + 1}/{tries}")
                    self.sh(f"truncate -s {offset} {remote_tmp}" if offset else f"rm -f {remote_tmp}", check=False)
                else:
                    raise SystemExit(f"giving up at {offset >> 20} MiB of {local.name}")

    def free_bytes(self):
        rc, out = self.sh("df -k files | tail -1", check=False)
        fields = out.split()
        return int(fields[3]) * 1024 if rc == 0 and len(fields) >= 4 and fields[3].isdigit() else None


def push(adb: Adb, items, manifest, quick=False, log=print):
    need = sum(size for _, dev, size, _ in items) + (1 << 28)
    free = adb.free_bytes()
    # files already on the device do not need space again, so only complain when even a fresh push could not fit
    has_romfs = adb.sh("test -e files/game.romfs", check=False)[0] == 0
    if free is not None and free < need and not has_romfs:
        raise SystemExit(f"not enough free space on the device: {free >> 20} MiB free, about {need >> 20} MiB needed")
    adb.sh(f"mkdir -p {BASE}/modules {BASE}/shaders")
    sent = skipped = 0
    for local, dev, size, digest in items:
        rc, out = adb.sh(f"stat -c %s {dev}", check=False)
        if rc == 0 and out.isdigit() and int(out) == size:
            rc, out = adb.sh(f"sha256sum {dev}", check=False)
            if rc == 0 and out.split()[:1] == [digest]:
                skipped += 1; continue
        log(f"push {dev} ({size >> 20} MiB)")
        if size > CHUNK:
            adb.stream_chunked(dev + ".new", local, size, log=log)
        else:
            adb.stream(dev + ".new", local=local)
        rc, out = adb.sh(f"stat -c %s {dev}.new")
        if int(out) != size:
            raise SystemExit(f"{dev}: device has {out} bytes after transfer, expected {size}")
        if not quick:
            rc, out = adb.sh(f"sha256sum {dev}.new")
            if out.split()[:1] != [digest]:
                raise SystemExit(f"{dev}: SHA-256 differs after transfer")
        adb.sh(f"mv {dev}.new {dev}")
        sent += 1
    adb.stream(f"{BASE}/hades-manifest.json.new", data=json.dumps(manifest, indent=1).encode("utf-8"))
    adb.sh(f"mv {BASE}/hades-manifest.json.new {BASE}/hades-manifest.json")
    log(f"done: {sent} files sent, {skipped} already up to date; manifest written last")
    return sent, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--modules", type=Path, required=True)
    ap.add_argument("--romfs", type=Path, required=True)
    ap.add_argument("--shaders", type=Path, required=True)
    ap.add_argument("--package", default="org.hades2.mobile")
    ap.add_argument("--serial")
    ap.add_argument("--adb", default="adb")
    ap.add_argument("--quick", action="store_true", help="after a transfer check only the size, not the SHA-256")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--manifest-out", type=Path)
    a = ap.parse_args()
    manifest, items = build_plan(a.modules, a.romfs, a.shaders)
    total = sum(i[2] for i in items)
    print(f"build {manifest['build_id']}: {len(items)} files, {total >> 20} MiB, {manifest['shaders']['programs']} shader programs, RomFS sha256 {manifest['romfs']['sha256'][:16]}...")
    if a.manifest_out:
        a.manifest_out.write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    if a.dry_run:
        return
    push(Adb(a.package, a.adb, a.serial), items, manifest, a.quick)


if __name__ == "__main__":
    main()
