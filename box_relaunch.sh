#!/usr/bin/env bash
# Restart JUST the app inside an already-running box container, and re-place
# its windows. Executed INSIDE the container by scripts/reload.sh.
#
# Everything box_display.sh set up on first launch survives: Xvfb on :99, the
# apt packages, x11vnc, websockify, the gtk.css and fontconfig tweaks, and the
# root-background frames. So the noVNC session in the browser STAYS CONNECTED —
# the windows vanish for a second and come back rebuilt. No container teardown,
# no apt reinstall, no reopening the browser tab.
set -uo pipefail

export DISPLAY=:99
export RALLY_SIM_I2C=1
export HOME=/build
cd /build

echo "Stopping the running app…"
pkill -f '\./HistoricRallyMeter' 2>/dev/null
# Wait for it to actually go; SIGKILL only if it will not.
for i in $(seq 1 20); do
    pgrep -f '\./HistoricRallyMeter' >/dev/null 2>&1 || break
    sleep 0.25
done
pgrep -f '\./HistoricRallyMeter' >/dev/null 2>&1 && pkill -9 -f '\./HistoricRallyMeter'

echo "Launching HistoricRallyMeter (RALLY_SIM_I2C=1)…"
./HistoricRallyMeter >/build/app.log 2>&1 &
APP=$!
sleep 5
if ! kill -0 "$APP" 2>/dev/null; then
    echo "!!! App exited early:"; tail -40 /build/app.log; exit 2
fi

# Same placement pass as box_display.sh: no WM, so xdotool requests are honored
# verbatim and the windows stay borderless. Repeated because GTK re-requests its
# natural (taller) size after the first content paint.
echo "Placing windows: Co-Pilot 1280x400 @ (20,20), Driver 800x480 @ (1340,20), Control Panel 480x400 @ (20,460)…"
for pass in 1 2 3 4; do
    cp="$(xdotool search --onlyvisible --name 'Co-Pilot Display' 2>/dev/null | head -1)"
    dr="$(xdotool search --onlyvisible --name 'Driver Display'   2>/dev/null | head -1)"
    ct="$(xdotool search --onlyvisible --name 'Control Panel'    2>/dev/null | head -1)"
    [ -n "$cp" ] && { xdotool windowsize "$cp" 1280 400; xdotool windowmove "$cp" 20   20; }
    [ -n "$dr" ] && { xdotool windowsize "$dr" 800  480; xdotool windowmove "$dr" 1340 20; }
    [ -n "$ct" ] && { xdotool windowsize "$ct" 480  400; xdotool windowmove "$ct" 20   460; }
    sleep 1
done

# The frames live on the root background and are untouched by the restart, but
# the app's own windows were destroyed and recreated, so re-assert the backdrop
# in case the new windows painted over part of it.
[ -f /build/frame.png ] && { display -window root /build/frame.png >/dev/null 2>&1 & }

echo "--- final window geometry ---"
for id in $(xdotool search --onlyvisible --name '.+' 2>/dev/null); do
    echo "  $(xdotool getwindowname "$id"): $(xdotool getwindowgeometry "$id" | tr '\n' ' ')"
done

import -display :99 -window root /build/box_screenshot.png 2>/dev/null \
    && echo "Saved /build/box_screenshot.png" || true

echo "RELOAD COMPLETE — refresh the noVNC tab if it looks stale."
