# HistoricRallyMeter

A precision timing and speed measurement application for historic car regulation rallies, built for Raspberry Pi 5 with dual-display support.

## Introduction

In historic car regulation rallies, drivers must maintain set average speeds over multi-segment stages, with timing accuracy measured to within 0.1 seconds over distances ranging from 2 km to hundreds of kilometres. HistoricRallyMeter provides real-time speed, distance, and timing calculations by reading pulse counters connected to the vehicle's gearbox or wheels via I2C, displaying the information across dedicated screens — one for the driver and one for the co-pilot.

The driver display shows current speed, target speed, total and trip averages, and a semicircular rally gauge indicating how many seconds ahead or behind the target pace. The co-pilot display provides stage setup, segment management, calibration, and detailed timing controls.

When only one 1280×400 monitor is available (or when forced via config), **single-display mode** shows the co-pilot TwinMaster screen with the compact driver gauge embedded in the right panel.

## Features

- **Dual displays** — driver and co-pilot windows on 1280×400 (or 800×480 driver) panels; auto-detected and fullscreened on matching monitors
- **Single-display mode** — one 1280×400 co-pilot screen with embedded compact driver gauge (configurable via `force_single_display`)
- **Rally gauge** — semicircular ahead/behind timing indicator with auto-scaling (green/yellow/red) and needle
- **Real-time speed calculation** — rolling 10-second average from hardware counters polled at up to 200 Hz
- **Segment management** — multi-segment stages with target speeds, automatic or manual progression, next/prev segment correction
- **Ahead/behind timing** — high-precision calculation with speed adjustment arrows and audible tone feedback
- **Dual counter support** — single gearbox counter or averaged dual wheel counters
- **Calibration** — stored as millimetres per 1000 counts for integer-precision distance calculation
- **Persistent state** — all settings, segments, and window positions saved to JSON
- **Remote web access** — phones on the same WiFi can view live telemetry and control segments via a browser (see below)

## Remote Web Access

The application embeds an HTTP/WebSocket server so co-drivers and crew can use ordinary phone browsers on the same subnet.

**On the device:** open **date/time** on the co-pilot screen. When the web server is enabled, the connection URL and a QR code are shown — scan with a phone camera to open the web client.

**On a phone:** the browser shows:

- **Live view** — rally clock, ahead/behind, current/target speed, trip/total distances and averages, segment info, next/prev correction, reset trip
- **Setup view** — edit segments, store/recall memory slots, reset trip/total

Telemetry updates at ~10 Hz over WebSocket. Commands (reset, segment edits, next/prev) use the same logic as the on-screen co-pilot controls.

| Setting | Default | Description |
|---------|---------|-------------|
| `web_enabled` | `true` | Enable the embedded web server |
| `web_port` | `8080` | TCP port for HTTP and WebSocket |

The server listens on all interfaces; the URL shown uses the device's LAN IP (e.g. `http://192.168.1.42:8080/`). It is intended for a private in-car network only — there is no authentication or encryption.

## Project Layout

```
HistoricRallyMeter/     Main GTK application (C++20)
webserver/                Embedded HTTP/WebSocket server and QR display
webclient/                Static web app served to phones (HTML/CSS/JS)
update                    Script to fetch and build the latest stable release
Design.md                 Full design specification
rally_config.json         Per-installation state (created on first run)
```

Run the binary from the project directory so it can find `webclient/` and `rally_config.json`.

## Hardware Requirements

- **Raspberry Pi 5** (4GB+ recommended)
- **LSI ls7866c 32-bit counters** — two devices on I2C bus 1 at addresses 0x70 and 0x71
- **Displays** — co-pilot: 1280×400; driver: 1280×400 or 800×480 (or single 1280×400 in single-display mode); the app also works with a standard monitor during development
- **Audio output** — for speed adjustment tone feedback (ALSA default device)
- **Network** (optional) — WiFi or Ethernet for phone web access

## Installation on Raspberry Pi 5

### 1. Install build dependencies

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk-3-dev libasound2-dev libqrencode4 git
```

`libqrencode4` is used at runtime for the connection QR code on the Date/Time screen (loaded dynamically; no dev package required).

### 2. Enable I2C

```bash
sudo raspi-config
```

Navigate to **Interface Options > I2C > Enable**, then add your user to the i2c group:

```bash
sudo usermod -aG i2c $USER
```

Reboot for changes to take effect. Verify with:

```bash
ls /dev/i2c-1
```

### 3. Set I2C baud rate to 400 kHz

The default I2C baud rate (100 kHz) is too slow for reliable high-frequency counter polling. Increase it to 400 kHz by editing the boot config:

```bash
sudo nano /boot/firmware/config.txt
```

Find the line:

```
dtparam=i2c_arm=on
```

And change it to (or add if not present):

```
dtparam=i2c_arm=on,i2c_arm_baudrate=400000
```

Reboot for the change to take effect. Verify the baud rate with:

```bash
python3 -c "import struct; f=open('/sys/bus/i2c/devices/i2c-1/of_node/clock-frequency','rb'); print(struct.unpack('>I',f.read())[0],'Hz')"
```

This should output `400000 Hz`.

### 4. Clone and build

```bash
git clone https://github.com/simonpapworth6742/HistoricRallyMeter.git
cd HistoricRallyMeter
make all
```

### 5. Prevent inactive window dimming (recommended)

GTK3 dims unfocused windows by default, which makes the second display hard to read. To fix this:

```bash
mkdir -p ~/.config/gtk-3.0
cp gtk-example-gtk.css ~/.config/gtk-3.0/gtk.css
```

### 6. Add desktop shortcut

Copy the included `.desktop` file to your desktop for easy launching:

```bash
cp HistoricRallyMeter.desktop ~/Desktop/
chmod +x ~/Desktop/HistoricRallyMeter.desktop
```

The shortcut assumes the project is cloned to `~/HistoricRallyMeter`. If you cloned to a different location, edit the `Exec=` line in the `.desktop` file to match.

### 7. Run

```bash
./HistoricRallyMeter
```

Run from the project directory — the application reads and writes `rally_config.json` relative to the current working directory and serves the web client from `webclient/`. On first run, default values are used and saved on exit. You can also launch from the desktop shortcut created in step 6.

## Updating to the Latest Version

If you already have HistoricRallyMeter installed, run the included `update` script from
the project directory to move to the latest **stable release**:

```bash
cd ~/HistoricRallyMeter
./update
```

The script fetches the newest tagged release (`vX.Y.Z`), checks it out, and rebuilds
(`make clean && make all`). Your `rally_config.json` settings are preserved automatically —
it is listed in `.gitignore` and is never overwritten by the update.

> The update leaves the repository on a detached checkout at the release tag. To go back
> to tracking the development line, run `git checkout main`.

If the desktop shortcut has changed, re-copy it:

```bash
cp HistoricRallyMeter.desktop ~/Desktop/
chmod +x ~/Desktop/HistoricRallyMeter.desktop
```

## Installing the Rallybox version alongside the original

The Rallybox version (the `box` branch of the Neros-ai fork) installs in a
folder of its own, next to an original `~/HistoricRallyMeter` install and
never over it. On the Pi, paste:

```bash
curl -fsSL https://raw.githubusercontent.com/Neros-ai/HistoricRallyMeter/box/install-rallybox | bash
```

It asks which folder to use (default `~/Rallybox`), builds Rallybox there,
offers to copy the original's calibration and settings, and adds a desktop
icon called **Rallybox**. Update it later with `./update` in its folder — the
script follows the Rallybox line there, and release tags in an original
install as before. Run only one of the two at a time: they share the
counters, the screens and the phone page's port.

## Build Commands

| Command | Description |
|---------|-------------|
| `make all` | Release build with `-O2` optimisation |
| `make debug` | Debug build with symbols (`-g -O0`), produces `HistoricRallyMeter_debug` |
| `make test` | Build and run unit tests |
| `make clean` | Remove all build artifacts |

## Configuration

Application state is stored in `rally_config.json` (created automatically on first exit):

- **units** — KPH (default) or MPH
- **calibration** — millimetres per 1000 counter increments (set via the co-pilot calibration screen)
- **counters** — single gearbox counter or dual wheel counters
- **segments** — target speed and distance for each stage segment
- **force_single_display** — force single-display mode even when multiple monitors are connected
- **web_enabled** — enable embedded web server for phone browsers (default true)
- **web_port** — TCP port for web access (default 8080)
- **window state** — driver window size and monitor assignment

This file is in `.gitignore` as it contains per-installation state.

## Development

- **Design specification**: see [Design.md](Design.md) for the full application design and requirements (including remote web access protocol and UI)
- **IDE support**: VS Code / Cursor debug configurations are provided in `.vscode/` (launch, tasks, IntelliSense)
- **C++ standard**: C++20
- **GUI framework**: GTK3 with Cairo for custom gauge rendering
- **Web server**: GIO sockets on the GLib main loop; static client in `webclient/`
- **Dependencies**: GTK3, ALSA (libasound), libqrencode (runtime), pthreads, dl
