# tab5-os

<p>
  <img src="https://img.shields.io/badge/version-v0.3.0-blue" alt="Version">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
  <img src="https://img.shields.io/badge/platform-ESP32--P4-blue" alt="Platform">
  <img src="https://img.shields.io/badge/ESP--IDF-v5.5.5-blue" alt="ESP-IDF">
  <img src="https://img.shields.io/badge/LVGL-9-blue" alt="LVGL">
</p>

**Languages:** [Português](README.md) | [English](README.en.md)

Proof-of-concept operating system for the **M5Stack Tab5** (ESP32-P4): virtual keyboard, IMU-based auto-rotation, OS-style top bar with RTC clock and light/dark theme.

- **Monthly Calendar App & Top Bar Popup** — quick monthly calendar view triggered by tapping the date/time on the status bar (modal overlay popup on `lv_layer_top()`) or dedicated full-screen app on the Desktop with custom styled vector icon, structured grid view with vertical and horizontal separators, automatic highlighting of today's date in accent color, interactive date selection, fluid month/year navigation with December/January wrap-around, "Hoje" (Today) button, and full light/dark theme support
- **System Bar Screenshot Capture** — camera icon in the top bar that captures the screen exactly as displayed (apps, system bar, virtual keyboard and modals) via LVGL snapshot of the active screen composed with the top layer (alpha blending), writing 24-bit BMP asynchronously to the SD card (`/sdcard/screenshots/print_YYYYMMDD_HHMMSS.bmp`), with a white confirmation flash, result toast, correct orientation across all 4 IMU rotations, and direct viewing in the Tab5 built-in viewer
- **Voice Recorder & Audio Player App** — native voice recording via integrated microphones using the ES7210 ADC codec in standard WAV PCM 16-bit 16 kHz Mono format (`/sdcard/gravacoes/REC_YYYYMMDD_HHMMSS.wav`), automatic 5-minute safety timeout, audio player powered by the ES8388 DAC codec with real-time progress bar, chronological audio list with deletion modal, and direct association with `.wav` and `.pcm` files
- **Camera App & V4L2/ISP Pipeline on ESP32-P4** — real-time 640×480 camera preview directly from SC202CS sensor over MIPI-CSI with hardware ISP acceleration, persistent V4L2 streaming architecture (`VIDIOC_STREAMON`/`VIDIOC_STREAMOFF`), ISP color correction matrix (CCM) clamping protection, and asynchronous JPEG photo capture via FreeRTOS task on SD card
- **Photo Gallery App** — native photo viewer for JPEG images saved on the SD card (`/sdcard/photos/`), powered by high-performance Tiny JPEG Decompressor (TJpgDec) directly to PSRAM canvas buffer on LVGL 9, image navigation, and seamless integration with Camera and File Manager
- **Web Server & System Control Panel App (HTTP)** — embedded full-featured HTTP server for remote management via web browser: complete microSD file and directory explorer with direct downloads and breadcrumbs navigation, real-time system settings control panel (Wi-Fi with network scanner, Bluetooth, display brightness, manual/auto IMU rotation, timezone, screensaver timeout, and light/dark theme), AI Chat configuration (OpenAI/OpenCode Go), and quick photo gallery
- **Responsive Flexbox Desktop Grid** — left-to-right, top-to-bottom layout (`LV_FLEX_FLOW_ROW_WRAP`) with dynamic column centering, left-aligned row wraps across orientations, and styled vector app icons (Camera 📷, Notepad 🗒️, Gallery 🖼️, Server 💽, Recorder 🎙️)
- **Automatic Screen-Off** — power saving with a configurable inactivity timeout in the settings menu (Disabled, 30 seconds, 1, 2, 5 and 10 minutes) persisted in NVS: the backlight turns off (PWM 0%) while background apps (e.g., Music Player) keep running, and the screen wakes restoring the previous brightness on **double tap** (400 ms window; a single tap is swallowed with no ghost click) or immediately via Bluetooth mouse/keyboard
- **Top Bar Power Button (Power Menu)** — power icon at the far left of the top bar opening a panel with three actions: **Screen Off** (same path as the automatic screen-off, double tap to wake), **Reboot** (`esp_restart`) and **Shut Down** (ultra-low-power *deep sleep*, woken by the physical button), with a confirmation modal before destructive actions
- **INA226 Battery Monitor in the Top Bar** — icon with percentage between Wi-Fi and the clock tracking the NP-F550 (2S) battery in real time via INA226 (I2C 0x41, 5 mΩ shunt): **Charging** (bolt), **External power** (charged, accent color), **On battery** (level symbol) and **Cable only (no battery)**; red alert below 15%, tap popup with State, Voltage, Current and Level (refreshed every second), and automatic hiding when the sensor does not respond
- **Battery Charging Protection** — "Battery protection" option persisted in NVS in the Settings menu: cuts charging at **90%** even with the cable connected (disables the IP2326 charger `CHG_EN` via I2C expander), keeping the device powered exclusively by the cable, and automatically resumes charging when the level drops to **85%**
- **Persistent Master Volume** — volume control (0–100%) available in the Settings menu and the Music app, automatically saved when the slider is released to NVS (`tab5/volume`) and the SD card (`/sdcard/tab5_os/audio.cfg`), restored at boot by the player before applying it to the DAC codec
- **Anti-Burn-in Screensaver** — MIPI-DSI panel protection against image sticking, featuring a pure black background (`#000000`), prominent digital clock (`HH:MM:SS`), full date in Portuguese, OS version, intelligent random relocation every 30 seconds with safe bounding box across all 4 orientations (0°, 90°, 180°, 270°), temporary mouse cursor hiding, and instant wake-up on touch, keyboard, or mouse events
- **Terminal App & Remote SSH Client** — Linux-style console shell integrated into the OS, with interactive prompt (`/sdcard $`), command history, support for core commands (`ls`, `cd`, `pwd`, `mkdir`, `rm`, `rmdir`, `touch`, `cat`, `echo`, `clear`, `whoami`, `uname`, `help`) and **full SSH Client** (`ssh [user@]host [-p port]`) running on a dedicated asynchronous FreeRTOS task powered by `libssh`, VT100/xterm terminal emulation, masked password prompt, and robust ANSI/OSC sequence stripping
- **Bluetooth Manager & Physical Keyboard (BLE HID)** — connection, pairing, and automatic reconnection with Bluetooth Low Energy peripherals (HOGP) like physical keyboards and combo touchpad/mice, direct keystroke injection into apps (e.g., Notes and Terminal), dynamic virtual keyboard hiding, and top-bar connection indicator
- **Mouse & Touchpad BLE HID with Visual Pointer** — automatic detection of BLE mice and touchpads (including Logitech Lift composite reports), high-visibility visual cursor on LVGL 9 shown as soon as the HID transport is ready, navigation and clicks adapted to all screen orientations (0°, 90°, 180°, 270°), and tap-to-click gesture recognition
- **File Manager ("Arquivos")** — SD card directory and file browser supporting interactive folder navigation, two view modes (Grid icons or Detailed list), and automatic opening of associated file types
- **Notes App & File Associations** — integrated text editor with note creation, save modal with name suggestion/editing, and native opening/editing of `.txt` and `.cfg` files
- **Advanced Wi-Fi Manager** — multi-network saved profiles in SD (`wifi.cfg`), intelligent mesh BSSID deduplication (keeping the highest RSSI), connected and saved visual badges, and connect/disconnect/forget network actions
- **PT-BR virtual keyboard** — LVGL native keyboard + textarea, works in portrait and landscape, with symbols (`*`, `@`, `#`, etc.) and an accents page (ç, accented lowercase and uppercase vowels) reachable through the "1#" key
- **Reactive window resizing** — application windows and modals automatically adapt their height and position when the virtual keyboard opens and closes
- **Orientation Persistence** — display rotation is automatically saved to the SD card and restored upon boot
- **OS-style top bar** — gear button, Wi-Fi status indicator, Bluetooth connection indicator, battery icon with percentage, and live clock in a monospaced font (`dd/mm/yyyy hh:mm`), with fixed width and right alignment that eliminate any sideways shifting when values change
- **Auto-rotation by IMU** — BMI270 gravity vector drives `lv_display_set_rotation` (0/90/180/270) with debounce
- **Settings menu** — quick panel integrated into the top bar with theme toggle (light/dark), screensaver timeout selector (Disabled, 1 min, 2 min, 5 min), automatic screen-off timeout (Disabled, 30 s, 1, 2, 5 and 10 min), IMU auto-rotation switch, independent power switches for Wi-Fi and Bluetooth with NVS persistence, battery charging protection (90% cutoff), persistent master volume control, radio control, and smart auto-reconnection
- **RTC clock** — RX8130CE seeds the system clock at boot (`settimeofday`)
- **Latin-1 font** — custom Montserrat 14px with the full Latin-1 supplement, so accented characters render correctly

## Hardware

- M5Stack Tab5 (SKU K145)
- ESP32-P4 (RISC-V dual 360 MHz) + ESP32-C6
- 5" IPS TFT MIPI-DSI, 720×1280
- Touch: GT911 / ST7123
- IMU: BMI270 · RTC: RX8130CE
- 16 MB flash · 32 MB PSRAM

## Prerequisites

- ESP-IDF **v5.5.5** (older versions fail to build with `espressif/usb`)
- Python 3.12
- Linux host (tested on Ubuntu)

## Build & Flash

```bash
. ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor --no-reset
```

> **Cold boot required**: unplug and replug the USB cable after flashing — a warm reset leaves the DSI display without image.

## Code quality & CI

Automated checks keep the firmware consistent and secure:

- **pre-commit** — local hooks that run on every commit: `clang-format` (project C/C++ style), `cmake-lint` (CMakeLists) and `codespell` (typos, with PT-BR ignore list).
- **Host-native unit tests** (`tools/ci/run_host_tests.sh`) — GoogleTest suite with 84 tests over the logic and persistence modules, gcov/lcov coverage with an ≥80% gate (currently 92.4%) and `/sdcard` redirection to a tmpdir via linker `--wrap`.
- **Visual regression** (`tools/ci/run_sim_tests.sh`) — SDL simulator that runs the real UI in a 720×1280 window and compares screenshots against deterministic golden images for 15 scenarios; see [tests/simulator/README.md](tests/simulator/README.md).
- **GitHub Actions**:
  - `ci.yml` — build with **ESP-IDF v5.5.5** (target `esp32p4`) + **clang-tidy** and **cppcheck** via `compile_commands.json`.
  - `codeql.yml` — static security analysis (SAST) with **CodeQL** (C/C++), manual `idf.py` build.

To enable the hooks locally:

```bash
pipx install pre-commit   # or: python3 -m venv ~/.local/share/precommit-venv
pre-commit install
```

Useful commands:

```bash
pre-commit run --all-files   # run all hooks on all files
```

## Usage

- **Type** on the virtual keyboard (touch)
- **"1#"** opens the accents page (à á â ã ç é ê í ó ô õ ú and uppercase); **"abc"** returns to the QWERTY
- **Tilt** the device to rotate the UI (portrait/landscape)
- **Gear** (top-left) opens the settings menu: Configuração → Tema → Claro/Escuro
- The **clock** shows the real time read from the RTC

## Project Structure

```
tab5-os/
├── main/
│   └── app_main.cpp          # Boot: display, RTC, IMU, UI, subsystem registrations
├── components/
│   ├── os/                   # Operating System Core & Shell Subsystem
│   │   ├── core/             # System managers (app_registry, timezone, wifi, bt, imu)
│   │   ├── shell/            # OS graphical interface (ui_shell, desktop, bar, screensaver, keyboard, theme)
│   │   └── fonts/            # Compiled Latin-1 fonts
│   ├── apps/                 # User Applications (Package by Feature)
│   │   ├── calendar/         # Monthly calendar and grid view
│   │   ├── camera/           # Camera app with MIPI-CSI preview & JPEG capture
│   │   ├── gallery/          # JPEG Photo Viewer
│   │   ├── notas/            # Text & Notepad editor
│   │   ├── recorder/         # Voice Recorder & Audio Player
│   │   ├── chat/             # AI LLM Chat Client
│   │   ├── terminal/         # Interactive Shell & SSH Client
│   │   ├── fileserver/       # HTTP Web File Server
│   │   ├── files/            # MicroSD File Explorer
│   │   ├── wifi/             # Wi-Fi Settings
│   │   └── bluetooth/        # Bluetooth Settings
│   ├── m5stack_tab5/         # Local BSP (override of the official one)
│   └── rtc_rx8130/           # RX8130CE RTC driver
├── tools/
│   └── ci/                   # Local quality check scripts (clang-tidy, cppcheck, idf.py, host tests, visual regression)
├── tests/
│   ├── host/                 # Host-native GoogleTest unit tests (≥80% coverage)
│   └── simulator/            # SDL UI simulator + golden-image visual regression
├── docs/                     # Technical documentation & developer guides
├── .github/workflows/        # CI: Quality Gate (build + lint) and CodeQL
├── .clang-format             # Project C/C++ style
├── .clang-tidy               # Static lint checkers
├── .pre-commit-config.yaml   # Local quality hooks
├── .codespellrc              # codespell PT-BR ignore list
└── sdkconfig.defaults
```

## Notes

- Rotation uses a ~27° tilt threshold (0.45 G plane magnitude) to avoid oscillation; decisive tilts always rotate.
- `sw_rotate=true` is required (LVGL 9 + DSI).
- The custom font is generated from the same `Montserrat-Medium.ttf` used by the LVGL built-ins, adding the Latin-1 supplement range (`0xA0–0xFF`).

## Developing New Applications

To learn how to create, register, and integrate new applications into the operating system, with support for app manifests, desktop icons, standardized title bars, and file extension associations, see the [Application Development Guide](docs/APP_DEVELOPMENT.md).

## Planning & Roadmap

To check the development timeline, engineering decisions, detailed architecture, and specifications of all implemented and planned phases of the operating system, see the [Architecture & Implementation Plan (PLANO.md)](PLANO.md).

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.
