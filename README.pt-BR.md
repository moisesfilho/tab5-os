# tab5-os

<p>
  <img src="https://img.shields.io/badge/version-v0.1.0-blue" alt="Version">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
  <img src="https://img.shields.io/badge/platform-ESP32--P4-blue" alt="Platform">
  <img src="https://img.shields.io/badge/ESP--IDF-v5.5.5-blue" alt="ESP-IDF">
  <img src="https://img.shields.io/badge/LVGL-9-blue" alt="LVGL">
</p>

**Idiomas:** [English](README.md) | [Português](README.pt-BR.md)

Prova de conceito de sistema operacional para o **M5Stack Tab5** (ESP32-P4): teclado virtual, rotação automática por IMU, barra superior estilo SO com relógio RTC e tema claro/escuro.

## Funcionalidades

- **Teclado virtual** — teclado nativo LVGL + textarea, funciona em retrato e paisagem
- **Rotação automática por IMU** — o vetor de gravidade do BMI270 aciona `lv_display_set_rotation` (0/90/180/270) com debounce
- **Barra superior estilo SO** — botão de engrenagem, badge de orientação e relógio ao vivo
- **Menu de configurações** — Configuração → Tema (claro/escuro) com destaque do item ativo
- **Relógio RTC** — RX8130CE semeia o relógio do sistema no boot (`settimeofday`)
- **Fonte Latin-1** — Montserrat 14px custom com o suplemento Latin-1 completo, para caracteres acentuados renderizarem corretamente

## Hardware

- M5Stack Tab5 (SKU K145)
- ESP32-P4 (RISC-V dual 360 MHz) + ESP32-C6
- TFT IPS 5" MIPI-DSI, 720×1280
- Touch: GT911 / ST7123
- IMU: BMI270 · RTC: RX8130CE
- Flash 16 MB · PSRAM 32 MB

## Pré-requisitos

- ESP-IDF **v5.5.5** (versões anteriores não compilam com `espressif/usb`)
- Python 3.12
- Host Linux (testado no Ubuntu)

## Build & Flash

```bash
. ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor --no-reset
```

> **Cold boot obrigatório**: desplugue e replugue o cabo USB após o flash — um reset quente deixa o display DSI sem imagem.

## Uso

- **Digite** no teclado virtual (touch)
- **Incline** o aparelho para girar a interface (retrato/paisagem)
- A **engrenagem** (canto superior esquerdo) abre o menu de configurações: Configuração → Tema → Claro/Escuro
- O **relógio** mostra a hora real lida do RTC

## Estrutura do Projeto

```
tab5-os/
├── main/
│   └── app_main.cpp          # Boot: display, RTC, IMU, UI
├── components/
│   ├── app/                  # UI + IMU
│   │   ├── ui_bar.cpp        # Barra superior, menu de configurações, relógio
│   │   ├── ui_keyboard.cpp   # Teclado virtual + textarea
│   │   ├── ui_status.cpp     # Badge de orientação
│   │   ├── ui_theme.cpp      # Paletas claro/escuro
│   │   ├── imu_reader.cpp    # Eventos do BMI270 → alvo de rotação
│   │   ├── orientation.cpp   # Vetor de gravidade → mapeamento de rotação
│   │   └── fonts/            # Fonte Latin-1 custom
│   ├── m5stack_tab5/         # BSP local (override do oficial)
│   └── rtc_rx8130/           # Driver do RTC RX8130CE
└── sdkconfig.defaults
```

## Notas

- A rotação usa um limiar de inclinação de ~27° (magnitude do plano de 0.45 G) para evitar oscilação; inclinações decididas sempre giram.
- `sw_rotate=true` é obrigatório (LVGL 9 + DSI).
- A fonte custom é gerada do mesmo `Montserrat-Medium.ttf` usado pelos fonts built-in do LVGL, adicionando o suplemento Latin-1 (`0xA0–0xFF`).

## Licença

Este projeto é licenciado sob a **Licença MIT** — veja o arquivo [LICENSE](LICENSE).