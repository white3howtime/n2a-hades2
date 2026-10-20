#!/bin/bash
# Shared settings for the helper scripts. Source it; override any variable from the environment.
#   N2A_TOOLS  folder holding the toolchain: ndk/ sdk/ jdk/ cmake/ ninja/ (see README, "Toolchain")
#   N2A_LOCAL  local-only workspace (game data, keys, build output, logs); git-ignored
#   ADB        adb executable
N2A_ROOT=${N2A_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}
N2A_TOOLS=${N2A_TOOLS:-/d/app}
N2A_LOCAL=${N2A_LOCAL:-$N2A_ROOT/.local}
ADB=${ADB:-adb}
PKG=org.hades2.mobile
BUILD=$N2A_LOCAL/build
export PATH="$N2A_TOOLS/cmake/bin:$N2A_TOOLS/ninja:$PATH"
# Windows tools (cmake, python, adb) want drive-letter paths; Git Bash's cygpath converts when present.
winpath() { if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else echo "$1"; fi; }
