#!/usr/bin/env bash
# Run the GTK GUI exactly as it appears on the bespoke box — Co-Pilot (1280x400)
# + Driver (800x480, compact gauge layout), side by side — served via noVNC.
# The app source is UNCHANGED; only the X environment is shaped. The fake sensor
# feed is the built-in sim backend (RALLY_SIM_I2C=1).
#
# Display strategy (verified empirically, see docs/HANDOFF.md "box display"):
#   The box has two physical 1280x400 panels and the app fullscreens one window
#   on each. Reproducing two SEPARATE GTK monitors off-target is the hard part:
#     * Xvfb has a single X output. `xrandr --setmonitor` can split the screen
#       but GTK (3.24, XRRGetMonitors active=True) only surfaces monitors backed
#       by an active output, so the second is dropped -> 1 GDK monitor.
#     * If GTK sees ONE 1280x400 monitor the app enters single-display mode
#       (co-pilot only, driver gauge embedded) — the WRONG box variant.
#     * Xephyr +xinerama exposes two heads but GTK ignores Xinerama and still
#       reports one monitor; multi-output needs Xorg+dummy (VT/privilege pain).
#   The robust, container-friendly path: give the app ONE 2080x480 screen (wider
#   than 1280x400 so single-display mode does NOT trigger), and the app creates
#   BOTH windows. We then place each borderless window — Co-Pilot 1280x400 and
#   Driver 800x480 — side by side. No window manager => no decorations =>
#   edge-to-edge panels, like the box.
#
#   Driver size matters: ui_driver.cpp picks the layout by ASPECT RATIO (<2.2 =>
#   compact gauge-centric layout with values inside the gauge; >=2.2 => wide
#   speeds-left/gauge-right). The box driver panel is 800x480 (aspect 1.67 =>
#   compact), per upstream Design.md which lists the driver as "1280x400 OR
#   800x480 ... compact layout on 800x480". The uploaded box photo shows the
#   compact layout, confirming 800x480. The co-pilot is always 1280x400.
#
# Executed INSIDE the container. /build is the mounted tree. Stays foreground.
set -uo pipefail

export DEBIAN_FRONTEND=noninteractive
echo "Installing Xvfb + x11vnc + noVNC…"
apt-get update -qq >/tmp/apt.log 2>&1
apt-get install -y -qq \
        xvfb x11-utils xdotool x11vnc novnc websockify imagemagick \
        gnome-themes-extra librsvg2-common libqrencode4 qrencode \
    >>/tmp/apt.log 2>&1 \
    || { echo "apt-get failed:"; tail -25 /tmp/apt.log; exit 1; }
# gnome-themes-extra ships /usr/share/themes/Adwaita-dark, which the app requests
#   via gtk-theme-name="Adwaita-dark" (main.cpp). Without it GTK silently falls
#   back to LIGHT Adwaita, so the app's dark buttons (#333333) get the theme's
#   light-grey gradient instead — the box renders them dark. librsvg2-common is
#   the SVG gdk-pixbuf loader so Adwaita's symbolic icons/assets load (otherwise
#   "Could not load a pixbuf from .../check-symbolic.svg" + missing checkmarks).
# libqrencode4 provides libqrencode.so.4, which the app dlopen()s at draw time to
#   render the phone-access QR code on the date/time screen (webserver/qr_display.cpp).
#   Without it on_qr_draw silently falls back to a grey placeholder — no QR.

# --- 2160x520 screen: Co-Pilot 1280x400 (left) + Driver 800x480 (right), each
#     framed with a white border so it reads as its own separate screen --------
export DISPLAY=:99
Xvfb :99 -screen 0 2160x940x24 -ac >/tmp/xvfb.log 2>&1 &
for i in $(seq 1 30); do xdpyinfo >/dev/null 2>&1 && break; sleep 0.3; done

# --- Launch the unmodified app with the fake sensor feed --------------------
export RALLY_SIM_I2C=1
export HOME=/build
cd /build

# Install the project's anti-dimming GTK CSS so the (unfocused, no-WM) windows
# render at FULL brightness — buttons/keypad bright white like the box, not the
# greyed "backdrop" state GTK applies to unfocused windows. Ships as
# gtk-example-gtk.css; Design.md "Preventing Inactive Window Dimming".
mkdir -p "$HOME/.config/gtk-3.0"
cp /build/gtk-example-gtk.css "$HOME/.config/gtk-3.0/gtk.css" 2>/dev/null \
    && echo "Installed anti-dimming gtk.css" || echo "WARN: gtk-example-gtk.css not found"

# Match the box's number rendering. The app draws all numeric readouts in the
# generic "monospace" family (CSS .dist-value/.speed-value + cairo gauge text).
# On Raspberry Pi OS "monospace" resolves to DejaVu Sans Mono; this bookworm
# image bumps Noto Sans Mono ahead, whose narrower advance + different digit
# glyphs shift the right-aligned (xalign=1.0) columns and change digit shapes.
# Force fontconfig to prefer DejaVu Sans Mono so digit alignment/shape matches
# the box. Environment-only (no app source change).
mkdir -p "$HOME/.config/fontconfig"
cat > "$HOME/.config/fontconfig/fonts.conf" <<'EOF'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <alias>
    <family>monospace</family>
    <prefer><family>DejaVu Sans Mono</family></prefer>
  </alias>
</fontconfig>
EOF
echo "monospace now resolves to: $(fc-match monospace)"

echo "Launching HistoricRallyMeter (RALLY_SIM_I2C=1)…"
./HistoricRallyMeter >/build/app.log 2>&1 &
APP=$!
sleep 5
if ! kill -0 "$APP" 2>/dev/null; then
    echo "!!! App exited early:"; tail -40 /build/app.log; exit 2
fi

echo "--- app display detection (single 2560x400 monitor; both windows expected) ---"
grep -E "GDK monitors|small display|Single-display|Positioning|assigned" /build/app.log || true

# --- Place each window side by side, inset to leave room for its frame -------
# No WM is running, so xdotool ConfigureWindow requests are honored verbatim and
# the windows stay borderless. Re-assert a few times in case GTK re-requests its
# natural (taller) size after the first content paint.
echo "Placing windows: Co-Pilot 1280x400 @ (20,20) [left], Driver 800x480 @ (1340,20) [right], Control Panel 480x400 @ (20,460) [bottom-left]…"
for pass in 1 2 3 4; do
    cp="$(xdotool search --onlyvisible --name 'Co-Pilot Display' 2>/dev/null | head -1)"
    dr="$(xdotool search --onlyvisible --name 'Driver Display'   2>/dev/null | head -1)"
    ct="$(xdotool search --onlyvisible --name 'Control Panel'    2>/dev/null | head -1)"
    # Side by side, TOP-ALIGNED (both y=20), each inset so its white frame fits.
    # Co-pilot 1280x400 beside the taller Driver 800x480. 800x480 -> aspect 1.67
    # -> the compact gauge-centric layout, like the box.
    [ -n "$cp" ] && { xdotool windowsize "$cp" 1280 400; xdotool windowmove "$cp" 20   20; }
    [ -n "$dr" ] && { xdotool windowsize "$dr" 800  480; xdotool windowmove "$dr" 1340 20; }
    # Sim Control Panel (dev/testing only): below Co-Pilot, green-framed.
    [ -n "$ct" ] && { xdotool windowsize "$ct" 480  400; xdotool windowmove "$ct" 20   460; }
    sleep 1
done

# Draw a thin white frame around each panel on the ROOT background (in the margin
# the windows don't cover) so each reads as its own separate screen. Frame rects
# sit ~8px outside each window; the differing frame sizes show the real 1280x400
# vs 800x480 difference. `display -window root` is backgrounded so it can't block.
convert -size 2160x940 xc:black -stroke white -strokewidth 3 -fill none \
    -draw "rectangle 12,12 1308,428" \
    -draw "rectangle 1332,12 2148,508" \
    -stroke "#00FF00" \
    -draw "rectangle 12,452 500,868" /build/frame.png 2>/dev/null

# Phone test-rig QR: encode the Mac's LAN URL (passed in by run-box.sh as
# PHONE_QR_URL) and place it on the root background to the RIGHT of the Sim Control
# Panel, so a phone on the same LAN can scan it directly. NB: the app's own
# on-screen QR (date/time screen) points at the container-internal Docker IP and is
# unreachable from a phone in this test rig — this one is the reachable address.
if [ -n "${PHONE_QR_URL:-}" ]; then
    qrencode -s 8 -m 2 -o /build/phone_qr.png "$PHONE_QR_URL" 2>/dev/null
    convert /build/frame.png \
        -fill white -stroke none -pointsize 24 -gravity NorthWest \
        -annotate +560+500 "Scan with phone (same Wi-Fi):" \
        \( /build/phone_qr.png -bordercolor white -border 12 \) \
            -gravity NorthWest -geometry +560+536 -composite \
        -fill "#33FF33" -pointsize 26 -gravity NorthWest \
        -annotate +560+880 "$PHONE_QR_URL" \
        /build/frame_qr.png 2>/dev/null \
        && mv /build/frame_qr.png /build/frame.png \
        && echo "Placed phone test-rig QR on root background ($PHONE_QR_URL)"
fi

display -window root /build/frame.png >/dev/null 2>&1 &
sleep 1
echo "Drew per-screen white frames on the root background"
echo "--- final window geometry ---"
for id in $(xdotool search --onlyvisible --name '.+' 2>/dev/null); do
    echo "  $(xdotool getwindowname "$id"): $(xdotool getwindowgeometry "$id" | tr '\n' ' ')"
done

# Snapshot for headless verification (proves both panels rendered side by side)
import -display :99 -window root /build/box_screenshot.png 2>/dev/null \
    && echo "Saved /build/box_screenshot.png" || true

# --- Serve to the browser ---------------------------------------------------
x11vnc -display :99 -forever -shared -nopw -localhost -rfbport 5900 -quiet \
    >/tmp/x11vnc.log 2>&1 &
sleep 2

echo "=================================================="
echo " OPEN IN BROWSER (Co-Pilot 1280x400 + Driver 800x480 + Sim Control Panel, each framed, 1:1):"
echo "   http://localhost:6080/vnc.html?autoconnect=true"
echo " (scaled to fit window:  add &resize=scale )"
echo " Top-left = Co-Pilot, Top-right = Driver (each in its own white frame)."
echo " Bottom-left (green frame) = Driving Controls (SIM) — START/STOP + 25-50 km/h"
echo " buttons driving the simulated counters."
echo "=================================================="
exec websockify --web=/usr/share/novnc 6080 localhost:5900
