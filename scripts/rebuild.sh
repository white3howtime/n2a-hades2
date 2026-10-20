#!/bin/bash
# Build the native library, assemble a debug-signed APK (no Gradle) and install it on the connected phone.
. "$(dirname "$0")/env.sh"
[ -f "$BUILD/native/build.ninja" ] || "$(dirname "$0")/configure.sh" || exit 1
cmake --build "$(winpath "$BUILD/native")" 2>&1 | grep -E "error|FAILED" | head -5
mkdir -p "$BUILD/apk"; rm -f "$BUILD/apk/hades2-debug.apk"
python "$N2A_ROOT/dm/tools/hades2_apk.py" --sdk "$(winpath "$N2A_TOOLS/sdk")" --jdk "$(winpath "$N2A_TOOLS/jdk")" \
  --lib "$(winpath "$BUILD/native/libd2r_mobile_android.so")" --src "$(winpath "$N2A_ROOT/dm/android/src")" \
  --out "$(winpath "$BUILD/apk/hades2-debug.apk")" --keystore "$(winpath "$BUILD/apk/debug.jks")" $([ -d "$N2A_ROOT/.local/icon-res" ] && echo --res "$(winpath "$N2A_ROOT/.local/icon-res")") 2>&1 | tail -1
"$ADB" install -r "$(winpath "$BUILD/apk/hades2-debug.apk")" 2>&1 | tail -1
