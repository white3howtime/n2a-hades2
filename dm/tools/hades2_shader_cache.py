#!/usr/bin/env python3
"""Hades II: BNSH shaders from the patched RomFS -> validated Android Vulkan shader cache.

    hades2_shader_cache.py --shaders <dir with the *.frag / *.vert BNSH files> --xlate <dmsh_xlate.dll> \
        --dotnet <dotnet.exe> --validator <spirv-val.exe> --out <fresh directory outside the checkout>

Steps (all existing dodola tools): manifest -> bnsh_inventory.py -> dmsh_xlate (Ryujinx translator) -> spirv-val -> bnsh_vulkan_cache.py.
Exits non-zero unless every shader translated, validated and was exported. Validation proves SPIR-V structure, not rendering equivalence.
"""
import argparse, glob, hashlib, json, os, subprocess, sys
from pathlib import Path

here = Path(__file__).resolve().parent


def run(cmd, what):
    p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if p.returncode:
        sys.exit(f"{what} failed ({p.returncode}):\n{(p.stdout + p.stderr)[-2000:]}")
    return p.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shaders", type=Path, required=True)
    ap.add_argument("--xlate", type=Path, required=True)
    ap.add_argument("--dotnet", type=Path, required=True)
    ap.add_argument("--validator", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    out = a.out.resolve()
    repo = here.parents[1]
    if repo == out or repo in out.parents or (out.exists() and any(out.iterdir())):
        sys.exit("--out must be a fresh directory outside the checkout")
    out.mkdir(parents=True, exist_ok=True)

    files = sorted(glob.glob(str(a.shaders / "*.frag")) + glob.glob(str(a.shaders / "*.vert")))
    if not files:
        sys.exit("no .frag/.vert files found")
    entries = []
    for f in files:
        data = Path(f).read_bytes()
        if not data.startswith(b"BNSH\0\0\0\0"):
            sys.exit(f"not a BNSH file: {f}")
        entries.append({"path": "Shaders/NVN/bin/" + os.path.basename(f) + ".bnsh", "extracted": f,
                        "size": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    resources = out / "resources.json"
    resources.write_text(json.dumps({"metadata_validated": True, "payload_sha256_complete": True, "files": entries}, indent=1))

    run([sys.executable, "-B", here / "bnsh_inventory.py", "--resources", resources, "--out", out / "inv"], "inventory")
    jobs = json.loads((out / "inv" / "jobs.json").read_text(encoding="utf8"))
    spv = out / "spv"; spv.mkdir()
    log = run([a.dotnet, a.xlate, out / "inv" / "jobs.json", spv], "translation")
    spvs = sorted(spv.glob("*.spv"))
    if len(spvs) != len(jobs):
        sys.exit(f"translated {len(spvs)} of {len(jobs)} programs:\n{log[-1500:]}")
    for f in spvs:
        run([a.validator, "--target-env", "vulkan1.1", f], f"spirv-val {f.name}")
    cache = out / "cache"
    msg = run([sys.executable, "-B", here / "bnsh_vulkan_cache.py", "--inventory", out / "inv", "--resources", resources,
               "--spv", spv, "--validator", a.validator, "--out", cache], "cache packing").strip()
    exported = sum(1 for _ in cache.glob("*.spv.gz"))
    if exported != len(jobs) or " blocked 0 " not in msg + " ":
        sys.exit(f"cache has {exported} of {len(jobs)} programs; {msg}")
    stages = {s: sum(1 for j in jobs if j["stage"] == s) for s in sorted({j["stage"] for j in jobs})}
    print(f"{len(files)} BNSH files -> {len(jobs)} programs {stages}; all translated, spirv-val passed, cache exported ({msg})")


if __name__ == "__main__":
    main()
