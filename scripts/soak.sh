#!/bin/bash
# Soak test: launch, load the first save slot, then wander and fight for N minutes (default 6, wall clock) while sampling memory.
# Output: $N2A_LOCAL/logs/soak.log (game log), soak-mem.txt (PSS/RSS per sample), soak-end.png. Prints FATAL/STOP/UNIMPL lines and the FPS range.
# Coordinates are landscape pixels of a 2640x1216 screen (joystick area left, buttons right); adjust the variables for other phones.
. "$(dirname "$0")/env.sh"
MIN=${1:-6}
CAST="1938 658"; ATTACK="2078 924"; DASH="2078 644"; SPECIAL="1798 945"   # fill-screen layout (default)
mkdir -p "$N2A_LOCAL/logs"; : > "$N2A_LOCAL/logs/soak-mem.txt"
"$(dirname "$0")/run_game.sh" 5 >/dev/null
for i in 1 2 3; do "$ADB" shell "input swipe $CAST $CAST 150"; sleep 6; done   # title -> slot -> load
sleep 6; for i in 1 2 3; do "$ADB" shell "input swipe 450 800 450 560 6000"; done   # walk out of the Crossroads into the first combat room
end=$((SECONDS + MIN*60)); n=0
while [ $SECONDS -lt $end ]; do
  [ -z "$("$ADB" shell pidof $PKG | tr -d '\r')" ] && { echo "PROCESS DIED at ${SECONDS}s"; break; }
  dx=$(( (RANDOM % 600) - 300 )); dy=$(( (RANDOM % 500) - 250 ))
  "$ADB" shell "input swipe 450 800 $((450+dx)) $((800+dy)) 1500" &
  for k in 1 2 3; do "$ADB" shell "input swipe $ATTACK $ATTACK 100"; sleep 0.3; done
  "$ADB" shell "input swipe $DASH $DASH 100"; "$ADB" shell "input swipe $SPECIAL $SPECIAL 100"; "$ADB" shell "input swipe $CAST $CAST 100"
  wait; n=$((n+1))
  if [ $((n % 6)) -eq 0 ]; then
    echo "t=${SECONDS}s $("$ADB" shell dumpsys meminfo $PKG | grep -E "TOTAL PSS|TOTAL RSS" | tr -s ' ' | tr '\n' ' ')" >> "$N2A_LOCAL/logs/soak-mem.txt"
  fi
done
"$ADB" exec-out screencap -p > "$N2A_LOCAL/logs/soak-end.png"
"$ADB" shell "run-as $PKG cat files/d2r-mobile/diagnostics/game-startup.log" | tr -d '\r' > "$N2A_LOCAL/logs/soak.log"
grep -a -n -E "FATAL|STOP|UNIMPL|Android NVN unsupported" "$N2A_LOCAL/logs/soak.log" | tail -5 | cut -c1-240
grep -a -o "fps=[0-9.]*" "$N2A_LOCAL/logs/soak.log" | cut -d= -f2 | sort -n | sed -n '1p;$p' | tr '\n' ' '; echo "(min/max fps)"
cat "$N2A_LOCAL/logs/soak-mem.txt"
