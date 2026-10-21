#!/bin/bash
# Smoke test: launch, then press the Cast button (confirm in menus) every ~10 s for N rounds (default 8) or until the process dies.
# The tap position is in landscape pixels of a 2640x1216 screen; adjust CAST_X/CAST_Y for other phones.
. "$(dirname "$0")/env.sh"
CAST_X=${CAST_X:-1850}; CAST_Y=${CAST_Y:-763}
"$(dirname "$0")/run_game.sh" 1 >/dev/null
sleep 25
for i in $(seq 1 ${1:-8}); do
  [ -z "$("$ADB" shell pidof $PKG | tr -d '\r')" ] && { echo "died at round $i"; break; }
  "$ADB" shell input swipe $CAST_X $CAST_Y $CAST_X $CAST_Y 150; sleep 10
done
"$ADB" shell "run-as $PKG cat files/d2r-mobile/diagnostics/game-startup.log" | tr -d '\r' > "$N2A_LOCAL/logs/drive.log"
"$ADB" exec-out screencap -p > "$N2A_LOCAL/logs/drive.png"
grep -n -E "FATAL|STOP|UNIMPL|KIND_MISMATCH" "$N2A_LOCAL/logs/drive.log" | tail -5 | cut -c1-260
