#!/usr/bin/env python3
"""Assemble a debug-signed Hades II APK from the compiled pieces (no Gradle).

    hades2_apk.py --sdk D:/app/sdk --jdk D:/app/jdk --lib libd2r_mobile_android.so --src dm/android/src --out hades2-debug.apk [--keystore ks.jks]

Steps: javac (Java 8 bytecode) -> d8 -> aapt2 link of dm/android/hades/AndroidManifest.xml -> add classes.dex and the .so ->
zipalign (16 KiB pages) -> apksigner with a debug key (created with keytool if missing) -> verify.
"""
import argparse, os, shutil, subprocess, sys, tempfile, zipfile
from pathlib import Path

here = Path(__file__).resolve().parent
MANIFEST = here.parent / "android" / "hades" / "AndroidManifest.xml"


def run(cmd, env=None):
    p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, encoding="utf-8", errors="replace", env=env)
    if p.returncode:
        sys.exit(f"{Path(str(cmd[0])).name} failed ({p.returncode}):\n{(p.stdout + p.stderr)[-2500:]}")
    return p.stdout + p.stderr


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sdk", type=Path, required=True)
    ap.add_argument("--jdk", type=Path, required=True)
    ap.add_argument("--lib", type=Path, required=True)
    ap.add_argument("--src", type=Path, required=True, help="dm/android/src")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--build-tools", default="35.0.0")
    ap.add_argument("--platform", default="android-34")
    ap.add_argument("--keystore", type=Path)
    ap.add_argument("--work", type=Path, help="scratch directory (default: next to --out)")
    a = ap.parse_args()
    exe = ".exe" if os.name == "nt" else ""
    bat = ".bat" if os.name == "nt" else ""
    bt = a.sdk / "build-tools" / a.build_tools
    android_jar = a.sdk / "platforms" / a.platform / "android.jar"
    for need in (android_jar, bt / f"aapt2{exe}", bt / f"d8{bat}", bt / f"zipalign{exe}", bt / f"apksigner{bat}", a.lib, MANIFEST):
        if not Path(need).exists():
            sys.exit(f"missing: {need}")
    if a.out.exists():
        sys.exit(f"{a.out} exists; refusing to overwrite")
    work = a.work or a.out.parent / (a.out.name + ".work")
    if work.exists():
        shutil.rmtree(work)
    classes, dex = work / "classes", work / "dex"
    classes.mkdir(parents=True); dex.mkdir()
    env = dict(os.environ, JAVA_HOME=str(a.jdk), TEMP=str(work), TMP=str(work))
    env["PATH"] = str(a.jdk / "bin") + os.pathsep + env["PATH"]
    sources = sorted(a.src.rglob("*.java"))
    if not sources:
        sys.exit("no Java sources")
    run([a.jdk / "bin" / f"javac{exe}", "--release", "8", "-encoding", "UTF-8", "-Xlint:-options", "-cp", android_jar, "-d", classes, *sources], env)
    run([bt / f"d8{bat}", "--min-api", "29", "--lib", android_jar, "--output", dex, *sorted(classes.rglob("*.class"))], env)
    base = work / "base.apk"
    run([bt / f"aapt2{exe}", "link", "-o", base, "--manifest", MANIFEST, "-I", android_jar, "--min-sdk-version", "29", "--target-sdk-version", "33"], env)
    unaligned = work / "unaligned.apk"
    with zipfile.ZipFile(base) as zin, zipfile.ZipFile(unaligned, "w") as zout:
        for item in zin.infolist():
            zout.writestr(item, zin.read(item.filename))
        zout.write(dex / "classes.dex", "classes.dex", zipfile.ZIP_DEFLATED)
        info = zipfile.ZipInfo("lib/arm64-v8a/" + a.lib.name, (2026, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_STORED          # stored, so zipalign can page-align it
        zout.writestr(info, a.lib.read_bytes())
    aligned = work / "aligned.apk"
    run([bt / f"zipalign{exe}", "-f", "-P", "16", "4", unaligned, aligned], env)
    ks = a.keystore or work / "debug.jks"
    if not ks.exists():
        run([a.jdk / "bin" / f"keytool{exe}", "-genkeypair", "-keystore", ks, "-storepass", "android", "-keypass", "android", "-alias", "androiddebugkey",
             "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-dname", "CN=Android Debug,O=Android,C=US"], env)
    run([bt / f"apksigner{bat}", "sign", "--ks", ks, "--ks-pass", "pass:android", "--key-pass", "pass:android", "--ks-key-alias", "androiddebugkey",
         "--out", a.out, aligned], env)
    run([bt / f"apksigner{bat}", "verify", "--verbose", a.out], env)
    run([bt / f"zipalign{exe}", "-c", "-P", "16", "4", a.out], env)
    badging = run([bt / f"aapt2{exe}", "dump", "badging", a.out], env)
    print(badging.splitlines()[0])
    print(f"{a.out} ({a.out.stat().st_size} bytes): signed, verified, 16 KiB aligned")


if __name__ == "__main__":
    main()
