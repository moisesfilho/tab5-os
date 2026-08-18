# tab5-os

<p>
  <img src="https://img.shields.io/badge/version-v0.1.0-blue" alt="Version">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
  <img src="https://img.shields.io/badge/platform-ESP32--P4-blue" alt="Platform">
  <img src="https://img.shields.io/badge/ESP--IDF-v5.5.5-blue" alt="ESP-IDF">
  <img src="https://img.shields.io/badge/LVGL-9-blue" alt="LVGL">
</p>

**Languages:** [Português](README.md) | [English](README.en.md)

Proof-of-concept operating system for the **M5Stack Tab5** (ESP32-P4): virtual keyboard, IMU-based auto-rotation, OS-style top bar with RTC clock and light/dark theme.

## Features

- **Voice Recorder & Audio Player App** — native voice recording via integrated microphones using the ES7210 ADC codec in standard WAV PCM 16-bit 16 kHz Mono format (`/sdcard/gravacoes/REC_YYYYMMDD_HHMMSS.wav`), automatic 5-minute safety timeout, audio player powered by the ES8388 DAC codec with real-time progress bar, chronological audio list with deletion modal, and direct association with `.wav` and `.pcm` files
- **Camera App & V4L2/ISP Pipeline on ESP32-P4** — real-time 640×480 camera preview directly from SC202CS sensor over MIPI-CSI with hardware ISP acceleration, persistent V4L2 streaming architecture (`VIDIOC_STREAMON`/`VIDIOC_STREAMOFF`), ISP color correction matrix (CCM) clamping protection, and asynchronous JPEG photo capture via FreeRTOS task on SD card
- **Photo Gallery App** — native photo viewer for JPEG images saved on the SD card (`/sdcard/photos/`), powered by high-performance Tiny JPEG Decompressor (TJpgDec) directly to PSRAM canvas buffer on LVGL 9, image navigation, and seamless integration with Camera and File Manager
- **HTTP File Server App** — built-in embedded HTTP file server for downloading and sharing captured photos over local Wi-Fi via any web browser, with on-demand startup for power efficiency and a dedicated desktop status screen
- **Responsive Flexbox Desktop Grid** — left-to-right, top-to-bottom layout (`LV_FLEX_FLOW_ROW_WRAP`) with dynamic column centering, left-aligned row wraps across orientations, and styled vector app icons (Camera 📷, Notepad 🗒️, Gallery 🖼️, Server 💽, Recorder 🎙️)
- **Anti-Burn-in Screensaver** — MIPI-DSI panel protection against image sticking, featuring a pure black background (`#000000`), prominent digital clock (`HH:MM:SS`), full date in Portuguese, OS version, intelligent random relocation every 30 seconds with safe bounding box across all 4 orientations (0°, 90°, 180°, 270°), temporary mouse cursor hiding, and instant wake-up on touch, keyboard, or mouse events
- **Terminal App & Remote SSH Client** — Linux-style console shell integrated into the OS, with interactive prompt (`/sdcard $`), command history, support for core commands (`ls`, `cd`, `pwd`, `mkdir`, `rm`, `rmdir`, `touch`, `cat`, `echo`, `clear`, `whoami`, `uname`, `help`) and **full SSH Client** (`ssh [user@]host [-p port]`) running on a dedicated asynchronous FreeRTOS task powered by `libssh`, VT100/xterm terminal emulation, masked password prompt, and robust ANSI/OSC sequence stripping
- **Bluetooth Manager & Physical Keyboard (BLE HID)** — connection, pairing, and automatic reconnection with Bluetooth Low Energy peripherals (HOGP) like physical keyboards and combo touchpad/mice, direct keystroke injection into apps (e.g., Notes and Terminal), dynamic virtual keyboard hiding, and top-bar connection indicator
- **Mouse & Touchpad BLE HID with Visual Pointer** — automatic detection of BLE mice and touchpads, high-visibility visual cursor on LVGL 9, navigation and clicks adapted to all screen orientations (0°, 90°, 180°, 270°), and tap-to-click gesture recognition
- **File Manager ("Arquivos")** — SD card directory and file browser supporting interactive folder navigation, two view modes (Grid icons or Detailed list), and automatic opening of associated file types
- **Notes App & File Associations** — integrated text editor with note creation, save modal with name suggestion/editing, and native opening/editing of `.txt` and `.cfg` files
- **Advanced Wi-Fi Manager** — multi-network saved profiles in SD (`wifi.cfg`), intelligent mesh BSSID deduplication (keeping the highest RSSI), connected and saved visual badges, and connect/disconnect/forget network actions
- **PT-BR virtual keyboard** — LVGL native keyboard + textarea, works in portrait and landscape, with symbols (`*`, `@`, `#`, etc.) and an accents page (ç, accented lowercase and uppercase vowels) reachable through the "1#" key
- **Reactive window resizing** — application windows and modals automatically adapt their height and position when the virtual keyboard opens and closes
- **Orientation Persistence** — display rotation is automatically saved to the SD card and restored upon boot
- **OS-style top bar** — gear button, Wi-Fi status indicator, Bluetooth connection indicator, and live clock
- **Auto-rotation by IMU** — BMI270 gravity vector drives `lv_display_set_rotation` (0/90/180/270) with debounce
- **Settings menu** — quick panel integrated into the top bar with theme toggle (light/dark), screensaver timeout selector (Disabled, 1 min, 2 min, 5 min), IMU auto-rotation switch, and independent power switches for Wi-Fi and Bluetooth with NVS persistence, radio control, and smart auto-reconnection
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
│   └── app_main.cpp          # Boot: display, RTC, IMU, UI
├── components/
│   ├── app/                  # UI + IMU + Terminal + WiFi/BT + Camera + Gallery + Server
│   │   ├── ui_desktop.cpp    # Responsive desktop grid and styled app icons
│   │   ├── ui_bar.cpp        # Top bar, settings menu, clock
│   │   ├── ui_camera.cpp     # Camera app with preview and shutter button
│   │   ├── camera_mgr.cpp    # V4L2/ISP manager and async photo capture
│   │   ├── ui_recorder.cpp   # Voice Recorder and Audio Player app
│   │   ├── audio_recorder.cpp # I2S audio backend, ES7210/ES8388 codecs, and WAV encoder
│   │   ├── ui_gallery.cpp    # Gallery photo viewer
│   │   ├── tjpgd.c           # Hardware/optimized JPEG decompressor (TJpgDec)
│   │   ├── ui_fileserver.cpp # HTTP File Server control app
│   │   ├── http_file_server.cpp # Embedded HTTP server for photo downloads
│   │   ├── ui_screensaver.cpp # Anti-burn-in screensaver with clock/date
│   │   ├── ui_mouse.cpp      # BLE HID mouse/touchpad support & cursor
│   │   ├── ui_terminal.cpp   # Terminal app (interactive console)
│   │   ├── terminal_cmd.cpp  # Shell command execution engine
│   │   ├── ssh_client.cpp    # Asynchronous SSH client (FreeRTOS task + libssh)
│   │   ├── ui_keyboard.cpp   # Virtual keyboard + PT-BR accents page
│   │   ├── ui_status.cpp     # Orientation badge
│   │   ├── ui_theme.cpp      # Light/dark palettes
│   │   ├── imu_reader.cpp    # BMI270 events -> rotation target
│   │   ├── orientation.cpp   # Gravity vector -> rotation mapping
│   │   └── fonts/            # Custom Latin-1 font
│   ├── m5stack_tab5/         # Local BSP (override of the official one)
│   └── rtc_rx8130/           # RX8130CE RTC driver
├── .github/workflows/        # CI: build + lint (ci.yml) and CodeQL (codeql.yml)
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

## Planning & Roadmap

To check the development timeline, engineering decisions, detailed architecture, and specifications of all implemented and planned phases of the operating system, see the [Architecture & Implementation Plan (PLANO.md)](PLANO.md).

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.
