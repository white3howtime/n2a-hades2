#!/bin/bash
# Configure the native library (arm64-v8a, Android API 29, Release) into $N2A_LOCAL/build/native.
. "$(dirname "$0")/env.sh"
cmake -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$(winpath "$N2A_TOOLS/ndk/build/cmake/android.toolchain.cmake")" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release \
  -S "$(winpath "$N2A_ROOT/dm/android")" -B "$(winpath "$BUILD/native")"
