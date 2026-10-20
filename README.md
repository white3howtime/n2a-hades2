# n2a-hades — Hades II on Android (native, no emulator)

[简体中文](README.zh-CN.md)

A native Android port of the **Nintendo Switch** version of *Hades II* (title ID `0100A00019DE0000`, update v9).
The Switch binaries are rewritten ahead of time and run directly on the phone's CPU; the Switch OS services and the NVN graphics API are
re-implemented on top of Android and Vulkan. There is no emulator, no JIT and no virtual machine in between.

> **This repository contains only the low-level adaptation layer. It does not contain the game, and never will.**
> No game code, assets, shaders, keys or firmware are included. You must dump the game from a Switch you own and prepare the files yourself
> (see [Preparing your own game files](#preparing-your-own-game-files)). This project is not affiliated with or endorsed by Supergiant Games or Nintendo.

## Based on

This project is built on top of **[dodola/d2r-mobile](https://github.com/dodola/d2r-mobile)** (MIT licence), a native Android/iOS port of another Switch title.
Its NSO rewriter, Switch-OS HLE layer, NVN→Vulkan layer, shader-translation pipeline and APK tooling are what made this possible; the full upstream history is kept in this repository.
Original files that had to be changed are listed as patches against upstream commit `62c6db8` in [`dm/android/hades_patches/`](dm/android/hades_patches/README.md).
The upstream README and design documents are kept under [`docs/upstream/`](docs/upstream/). The upstream licence is in [`LICENSE`](LICENSE).

## What is implemented

- **NSO rewriting for Hades II** (`main` + `sdk`, no EOS/online modules): the upstream `patch_nso.py` was extended to handle this build (`mov sp, x18` alloca pattern) and to cut the Epic Online Services dependency, so the game starts without EOS and uses local saves.
- **Hades-specific Switch-service HLE** (`dm/android/hades_hle.c`, with a host-side test): `nn::ro` stubs, `nn::codec` (hardware Opus reported as unsupported so FMOD falls back to its software decoder), `nn::fs` directory counting, a handful of `svc` results, title ID registration. 48 forced overrides, 1386 host test checks.
- **NVN → Vulkan additions** (`dm/gfx/nvn/hades_nvn.inc`, `nvn_vk.c`): `DrawElements`, viewport/depth ranges, integer clears, texture/sampler builder getters, BGRA8/BGR565/ASTC (all block sizes, native upload), linear (pitch) textures, per-pool texture descriptor tables, 32-bit integer vertex attributes fed to float shader inputs, and bounded vertex-buffer reads (the game binds vertex streams up to the end of an 8 MiB ring; reading only what a draw fetches took gameplay from ~29 to ~60 FPS).
- **Phone-friendly touch controls** (`hades_pad.c`, `HadesPadView.java`): floating joystick on the left; Attack / Special / Cast / Dash / Interact / Call / Lock on the right; Menu / Info / Pause at the top; a menu mode with D-pad, Confirm, Back and page buttons. No console button names (ZL/ZR/L/R) in the UI.
- **Native wide-screen rendering**: the game's hard-coded 1280×720 window size is rewritten at load time (only for the exact tested `main` build, guarded by instruction checks) to the phone's aspect ratio at 720 lines (e.g. 1562×720 on a 2640×1216 screen), so the image is rendered wide instead of stretched; touch controls use the full width. The system menu has a "fill screen" switch (needs a restart) to go back to 16:9 with black bars.
- **Tooling** (`dm/tools/`, `scripts/`): RomFS image builder and verifier, shader-cache builder, resumable adb push of the game data into the app's private storage, Gradle-free debug APK assembly, build/run/smoke-test scripts.

### Verified on a real phone

Boot → title → new save → Crossroads → free movement → room transition → combat (damage taken, effects, HUD) → death and return to the Crossroads → save written → app restarted → save listed and loaded.
Gameplay runs at about 60 FPS in the scenes tested (~640 draws per frame).

### Not done / not verified

- Wide-screen mode: world and menus verified at 2.17:1; every HUD/dialog/boss scene at that ratio is not (UI layout is engine-driven). Ratios wider than ~3.5:1 are clamped.
- Long play sessions (memory growth, stability over hours) and later biomes/bosses have not been tested; other NVN functions the game may call are still unimplemented and abort with `Android NVN unsupported function`.
- In-game button prompts still show Switch glyphs (e.g. "R"), which map to the on-screen buttons described above.
- Audio is wired (FMOD software Opus path, 48 kHz stereo) but has not been checked by ear.
- Only one sampler descriptor pool is supported; texture swizzle, primitive restart and sample mask are ignored (logged once); `QueueWaitSync` is a no-op.
- Debug-signed APK only; the app expects the prepared game data in its private storage.

## Tested environment

| | |
|---|---|
| Game dump | Hades II base `[0100A00019DE0000][v0].nsp` + update `[0100A00019DE0800][v589824].nsp` (= update **v9**; in-game version string `v1.137848`). Modules used: `main` (build ID `8ce1ac48e9fa74479051f3520e632df4`) and `sdk` (build ID `190030518b5d64e5d87b84bc54661af9`) |
| Host OS | Windows 11 (Git Bash for the shell scripts) |
| Android NDK | r27c (27.2.12479018), target `arm64-v8a`, `android-29` |
| Android SDK | platform `android-35`, build-tools `35.0.0` |
| JDK | OpenJDK 17.0.20 |
| CMake / Ninja | 3.31.6 / 1.12.1 |
| Python | 3.13 (`lz4`, `capstone` for the NSO tools) |
| .NET SDK | 10.0.402 (shader translator `dmsh_xlate`) |
| SPIR-V validator | `spirv-val` from the Vulkan SDK |
| Phone | Snapdragon 8 Elite (SM8750), Adreno 830, 16 GB RAM, Android 16 (API 36), Vulkan 1.4, 2640×1216 display |

## Repository layout

```
dm/android/            Android host: JNI, audio, input, GameActivity, Hades HLE (hades_hle.c), touch pad (hades_pad*.c)
dm/android/hades_patches/  patches for upstream files we had to change (against upstream 62c6db8)
dm/android/hades_test/ host-side unit test for the Hades HLE
dm/gfx/nvn/            NVN → Vulkan layer (+ hades_nvn.inc, Hades-only NVN entry points)
dm/host/               Switch OS HLE (svc, fs, ...)
dm/tools/              NSO patcher, RomFS / shader-cache / APK / adb-push tools
scripts/               configure / rebuild / run / smoke-test helpers (set N2A_TOOLS, ADB; see scripts/env.sh)
docs/upstream/         upstream README, design and contribution docs
.local/                git-ignored: your game data, keys, build output, logs
```

## Preparing your own game files

Everything below uses **your own** dump. Keep keys, NSPs, extracted files and the prepared data outside version control (`.local/` is git-ignored).

1. Dump the base game and the v9 update from your Switch, and your `prod.keys`. Extract the update's *Program* NCA (using the base NCA for the RomFS) with `nstool`/`hactool` to get the `main` and `sdk` NSOs and the merged RomFS tree.
2. Rewrite the modules: `python dm/tools/patch_nso.py <nso> <out dir> main` and the same for `sdk`; check with `dm/tools/check_patched_nso.py <out dir> main sdk`.
3. Build the RomFS image: `python dm/tools/romfs_build.py <romfs tree> game.romfs` (verify with `romfs_verify_tree.py`).
4. Translate the shaders: `python dm/tools/hades2_shader_cache.py --shaders <dir> --xlate <dmsh_xlate.dll> --dotnet <dotnet> --validator <spirv-val> --out <cache dir>`.
5. Build and install the APK: `bash scripts/rebuild.sh` (set `N2A_TOOLS` to the folder with `ndk/ sdk/ jdk/ cmake/ ninja/`).
6. Push the data: `python dm/tools/hades2_push.py --modules <out dir> --romfs game.romfs --shaders <cache dir> --package org.hades2.mobile`.
7. Start: `bash scripts/run_game.sh` (or open the app). `bash scripts/drive.sh` is a small smoke test.

Host-side check of the Hades HLE (needs a C compiler, e.g. `python -m ziglang cc`): see the header of `dm/android/hades_test/hades_hle_test.c`.

## Licence

MIT, as upstream (see [`LICENSE`](LICENSE)); the upstream copyright notice is kept. *Hades II* and all game content belong to their respective owners and are not distributed here.
