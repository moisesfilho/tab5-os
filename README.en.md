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

- **PT-BR virtual keyboard** — LVGL native keyboard + textarea, works in portrait and landscape, with an accents page (ç, accented lowercase and uppercase vowels) reachable through the "1#" key
- **Auto-rotation by IMU** — BMI270 gravity vector drives `lv_display_set_rotation` (0/90/180/270) with debounce
- **OS-style top bar** — gear button, orientation badge and live clock
- **Settings menu** — Configuração → Tema (light/dark) with active item highlight
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
│   ├── app/                  # UI + IMU
│   │   ├── ui_bar.cpp        # Top bar, settings menu, clock
│   │   ├── ui_keyboard.cpp   # Virtual keyboard + PT-BR accents page
│   │   ├── ui_status.cpp     # Orientation badge
│   │   ├── ui_theme.cpp      # Light/dark palettes
│   │   ├── imu_reader.cpp    # BMI270 events → rotation target
│   │   ├── orientation.cpp   # Gravity vector → rotation mapping
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

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.
