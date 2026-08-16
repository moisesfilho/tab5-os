# tab5-os

<p>
  <img src="https://img.shields.io/badge/version-v0.1.0-blue" alt="Version">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
  <img src="https://img.shields.io/badge/platform-ESP32--P4-blue" alt="Platform">
  <img src="https://img.shields.io/badge/ESP--IDF-v5.5.5-blue" alt="ESP-IDF">
  <img src="https://img.shields.io/badge/LVGL-9-blue" alt="LVGL">
</p>

**Idiomas:** [Português](README.md) | [English](README.en.md)

Prova de conceito de sistema operacional para o **M5Stack Tab5** (ESP32-P4): teclado virtual, rotação automática por IMU, barra superior estilo SO com relógio RTC e tema claro/escuro.

## Funcionalidades

- **Aplicativo Terminal (Shell Interativo)** — shell de console estilo Linux integrado ao sistema, com prompt interativo (`/sdcard $`), execução direta com tecla Enter, histórico de comandos e suporte aos comandos essenciais (`ls`, `cd`, `pwd`, `mkdir`, `rm`, `rmdir`, `touch`, `cat`, `echo`, `clear`, `whoami`, `uname`, `help`)
- **Gerenciador de Bluetooth & Teclado Físico (BLE HID)** — suporte a conexão, pareamento e auto-reconexão com periféricos Bluetooth Low Energy (HOGP), como teclados físicos e mouses/touchpads integrados, com injeção direta de digitação nos aplicativos (ex: Notas e Terminal), ocultação dinâmica do teclado virtual e indicador de conexão na barra superior
- **Suporte a Mouse & Touchpad BLE HID com Cursor Visual** — identificação automática de mouses e touchpads BLE, cursor visual de alta visibilidade no LVGL 9, navegação e clique com adaptação completa a todas as rotações da tela (0°, 90°, 180°, 270°) e detecção de gestos de toque rápido (tap-to-click)
- **Gerenciador de Arquivos ("Arquivos")** — navegador de arquivos e pastas do cartão SD com suporte a navegação por diretórios, dois modos de visualização (Ícones em grade ou Lista detalhada) e abertura automática de arquivos associados
- **Aplicativo Notas e Associações de Arquivos** — editor de texto integrado com criação de notas, salvamento modal com sugestão/edição de nome e suporte nativo a abertura e edição de arquivos `.txt` e `.cfg`
- **Gerenciador de Wi-Fi Avançado** — suporte a múltiplas redes salvas no SD (`wifi.cfg`), desduplicação inteligente de redes Mesh (mantendo o maior RSSI), indicadores visuais de rede conectada e redes salvas, além de ações para conectar, desconectar e esquecer rede
- **Teclado virtual PT-BR** — teclado nativo LVGL + textarea, funciona em retrato e paisagem, com símbolos (`*`, `@`, `#`, etc.) e página de acentos (ç, vogais acentuadas minúsculas e maiúsculas) acessada pela tecla "1#"
- **Redimensionamento reativo** — janelas de aplicativos e modais ajustam sua altura e posição visível automaticamente ao abrir e fechar o teclado virtual
- **Persistência de Orientação** — a posição da tela é persistida automaticamente no cartão SD e restaurada no boot
- **Barra superior estilo SO** — botão de engrenagem, ícone de status Wi-Fi, ícone de status Bluetooth e relógio ao vivo
- **Rotação automática por IMU** — o vetor de gravidade do BMI270 aciona `lv_display_set_rotation` (0/90/180/270) com debounce
- **Menu de configurações** — painel rápido integrado à barra superior com alternância de tema (claro/escuro), controle de rotação por IMU e interruptores para ligar/desligar Wi-Fi e Bluetooth com persistência em NVS, controle de rádio e auto-reconexão inteligente
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

## Qualidade de código & CI

Validações automáticas garantem consistência de estilo e segurança no firmware:

- **pre-commit** — hooks locais que rodam a cada commit: `clang-format` (estilo C/C++ do projeto), `cmake-lint` (CMakeLists) e `codespell` (typos, com ignore-list PT-BR).
- **GitHub Actions**:
  - `ci.yml` — build com **ESP-IDF v5.5.5** (target `esp32p4`) + **clang-tidy** e **cppcheck** via `compile_commands.json`.
  - `codeql.yml` — análise estática de segurança (SAST) com **CodeQL** (C/C++), build manual `idf.py`.

Para ativar os hooks localmente:

```bash
pipx install pre-commit   # ou: python3 -m venv ~/.local/share/precommit-venv
pre-commit install
```

Comandos úteis:

```bash
pre-commit run --all-files   # roda todos os hooks em todos os arquivos
```

## Uso

- **Digite** no teclado virtual (touch)
- **"1#"** abre a página de acentos (à á â ã ç é ê í ó ô õ ú e maiúsculas); **"abc"** volta ao QWERTY
- **Incline** o aparelho para girar a interface (retrato/paisagem)
- A **engrenagem** (canto superior esquerdo) abre o menu de configurações: Configuração → Tema → Claro/Escuro
- O **relógio** mostra a hora real lida do RTC

## Estrutura do Projeto

```
tab5-os/
├── main/
│   └── app_main.cpp          # Boot: display, RTC, IMU, UI
├── components/
│   ├── app/                  # UI + IMU + Terminal + WiFi/BT
│   │   ├── ui_bar.cpp        # Barra superior, menu de configurações, relógio
│   │   ├── ui_terminal.cpp   # Aplicativo Terminal (console interativo)
│   │   ├── terminal_cmd.cpp  # Motor de execução de comandos shell
│   │   ├── ui_keyboard.cpp   # Teclado virtual + página de acentos PT-BR
│   │   ├── ui_status.cpp     # Badge de orientação
│   │   ├── ui_theme.cpp      # Paletas claro/escuro
│   │   ├── imu_reader.cpp    # Eventos do BMI270 → alvo de rotação
│   │   ├── orientation.cpp   # Vetor de gravidade → mapeamento de rotação
│   │   └── fonts/            # Fonte Latin-1 custom
│   ├── m5stack_tab5/         # BSP local (override do oficial)
│   └── rtc_rx8130/           # Driver do RTC RX8130CE
├── .github/workflows/        # CI: build + lint (ci.yml) e CodeQL (codeql.yml)
├── .clang-format             # Estilo C/C++ do projeto
├── .clang-tidy               # Checkers de lint estático
├── .pre-commit-config.yaml   # Hooks locais de qualidade
├── .codespellrc              # Ignore-list PT-BR do codespell
└── sdkconfig.defaults
```

## Notas

- A rotação usa um limiar de inclinação de ~27° (magnitude do plano de 0.45 G) para evitar oscilação; inclinações decididas sempre giram.
- `sw_rotate=true` é obrigatório (LVGL 9 + DSI).
- A fonte custom é gerada do mesmo `Montserrat-Medium.ttf` usado pelos fonts built-in do LVGL, adicionando o suplemento Latin-1 (`0xA0–0xFF`).

## Licença

Este projeto é licenciado sob a **Licença MIT** — veja o arquivo [LICENSE](LICENSE).
