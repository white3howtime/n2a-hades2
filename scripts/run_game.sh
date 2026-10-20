#!/bin/bash
# Force-stop and launch the game, wait up to N*5 s (default 60 s), then pull the startup log to $N2A_LOCAL/logs/last-startup.log.
. "$(dirname "$0")/env.sh"
mkdir -p "$N2A_LOCAL/logs"
"$ADB" shell am force-stop $PKG
"$ADB" logcat -c
"$ADB" shell input keyevent KEYCODE_WAKEUP
"$ADB" shell am start -n $PKG/org.d2r.binaryprobe.GameActivity > /dev/null
for i in $(seq 1 ${1:-12}); do
  sleep 5; pid=$("$ADB" shell pidof $PKG | tr -d '\r')
  [ -z "$pid" ] && { echo "process gone after $((i*5))s"; break; }
done
echo "pid now: $("$ADB" shell pidof $PKG | tr -d '\r')"
"$ADB" shell "run-as $PKG cat files/d2r-mobile/diagnostics/game-startup.log" | tr -d '\r' > "$N2A_LOCAL/logs/last-startup.log"
