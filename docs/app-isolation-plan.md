# Plano de Arquitetura: Tab5 OS Modular & Sistema de Aplicações Desacopladas (WASM + Pacotes SD)

> Branch: `feat/app-isolation`
> Status: Planejado
> Data: 2026-08-28

Este plano estabelece a separação do **Tab5 OS** em um **Sistema Operacional Base** (Kernel, Drivers, Shell, LVGL, WAMR Runtime e Gerenciador de Pacotes) e **Aplicações Independentes** distribuídas como pacotes individuais (`.tab5pkg`) executadas em sandbox WebAssembly de alta performance.

As aplicações são **instaladas por cima do sistema operacional**: o firmware do SO base é flashado uma única vez e cada aplicação é instalada de forma isolada, com seu próprio instalador e ciclo de vida.

---

## 1. Visão Geral da Arquitetura

```
+-------------------------------------------------------------------------+
|                              CARTÃO SD                                  |
|  /sdcard/apps/                                                          |
|  ├── music.tab5pkg         ───(Instalador/App Store)───┐                |
|  ├── chat.tab5pkg                                       │                |
|  └── installed/                                         ▼                |
|      └── com.tab5.music/ ── [manifest.json, app.wasm, icon.bin, data/]  |
+-------------------------------------------------------------------------+
                                    │
                                    ▼ (App Runtime)
+-------------------------------------------------------------------------+
|                             TAB5 OS BASE                                |
|  [ Shell & Desktop ] ─── [ App Registry & Lifecycle ] ─── [ Package Mgr]|
|                                    │                                    |
|                      [ WAMR (WebAssembly Engine) ]                      |
|                                    │ (Native Host Bindings)             |
|  [ LVGL 9.5 UI API ]  [ Storage/FS API ]  [ Audio/Net/BT API ]  [ IPC ] |
|  ---------------------------------------------------------------------  |
|  [ Drivers Hardware: MIPI-DSI, Touch, BMI270, INA226, RTC, WiFi/BT ]    |
|  [ ESP-IDF v5.5.5 / FreeRTOS / ESP32-P4 RISC-V Dual-Core + 32MB PSRAM ] |
+-------------------------------------------------------------------------+
```

---

## 2. Especificação do Pacote de Instalação (`.tab5pkg`)

Cada aplicação terá seu próprio repositório/projeto e pipeline de build, gerando um pacote compactado `.tab5pkg`.

### Estrutura do Pacote

```
com.tab5.appname.tab5pkg
├── manifest.json       # Metadados, permissões, ID, versão, criador, ícone
├── app.wasm            # Binário WebAssembly compilado pelo SDK da App
├── icon.bin / icon.png # Asset de ícone (ou especificação de símbolo LVGL)
└── assets/             # Imagens, fontes, sons ou dados estáticos da aplicação
```

### Exemplo de `manifest.json`

```json
{
  "id": "com.tab5.notas",
  "name": "Notas",
  "version": "1.0.0",
  "author": "Moisés Filho",
  "description": "Editor de texto e notas do Tab5",
  "entry": "app.wasm",
  "icon": {
    "symbol": "LV_SYMBOL_EDIT",
    "bg_color": "#2196F3"
  },
  "file_associations": [".txt", ".md", ".log"],
  "permissions": [
    "storage.readwrite",
    "ui.keyboard"
  ]
}
```

---

## 3. Componentes do Sistema Operacional Base

### 3.1. Tab5 Native Host SDK & Bindings (C/C++ Export)

Camada que expõe as funções essenciais do SO para dentro do sandbox Wasm via WAMR Native Symbols:

1. **UI / LVGL Host Bindings:** Funções para criar telas de app, botões, labels, textareas, listas e gerenciar layout responsivo.
2. **Ciclo de Vida:** Eventos `app_on_init`, `app_on_resume`, `app_on_pause`, `app_on_destroy`, `app_on_open_file`.
3. **Storage / I/O:** Leitura/escrita segura em sandbox de arquivos (`/sdcard/data/<app_id>/` e `/sdcard/`).
4. **Hardware & Sistema:** Notificações, teclado virtual (`ui_keyboard`), áudio, status de rede/bateria e data/hora.

### 3.2. Gerenciador de Pacotes (`package_manager` / `ui_appstore`)

1. **Instalador:**
   - Varredura de pacotes pendentes em `/sdcard/apps/*.tab5pkg` ou instalação via Gerenciador de Arquivos/Download.
   - Extração do pacote para `/sdcard/apps/installed/<app_id>/`.
   - Validação de integridade do manifesto e permissões.
2. **Desinstalador:**
   - Remoção da pasta de instalação e desregistro no `app_registry`.
   - Opção de manter ou apagar dados de usuário (`/sdcard/data/<app_id>`).
3. **App Registry Dinâmico:**
   - O `app_registry` deixa de ser estático: no boot, ele lê os apps embutidos (partição dedicada) e `/sdcard/apps/installed/*/manifest.json`, populando o Desktop dinamicamente com ícones e callbacks de inicialização Wasm.

### 3.3. Runtime WAMR (WebAssembly Micro Runtime)

- Integrado como componente no ESP-IDF.
- Configurado com memória PSRAM (Fast-Interpreter / AOT suporte RISC-V).
- Instanciação de heap dedicada por processo/app isolado.

---

## 4. SDK para Criação de Aplicações Independentes (`tab5-app-sdk`)

Um repositório/template de desenvolvimento separado permitirá compilar aplicações de forma isolada, gerando o arquivo `.tab5pkg` sem necessidade de clonar ou compilar o SO completo.

### 4.1. Estrutura do Projeto de uma Aplicação

```
minha-app-tab5/
├── CMakeLists.txt              # Build Wasm via Emscripten / Clang wasi-sdk
├── manifest.json               # Metadados, versão, permissões e ícone
├── assets/                     # Imagens, fontes, áudios
│   └── icon.png
├── src/
│   ├── main.c / main.cpp       # Código da aplicação usando tab5_sdk.h
│   └── ui.c
└── tools/
    └── pack.py                 # Script que empacota manifesto + wasm + assets em .tab5pkg
```

### 4.2. Exemplo de Código de uma Aplicação no SDK

```c
#include "tab5_sdk.h"

static lv_obj_t *main_screen;
static lv_obj_t *label;

void on_button_click(lv_event_t *e) {
    lv_label_set_text(label, "Olá do WebAssembly!");
    tab5_sound_play_beep(1000, 100);
}

TAB5_APP_EXPORT void app_main(void) {
    main_screen = tab5_ui_get_screen();

    label = lv_label_create(main_screen);
    lv_label_set_text(label, "Aplicação Desacoplada Tab5");
    lv_obj_center(label);

    lv_obj_t *btn = lv_button_create(main_screen);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_add_event_cb(btn, on_button_click, LV_EVENT_CLICKED, NULL);
}
```

---

## 5. Fluxo de Instalação, Execução e Gerenciamento

### 5.1. Ciclo de Instalação

1. O usuário copia um arquivo `nome_app.tab5pkg` para a raiz ou pasta `/sdcard/apps/` do cartão SD (via leitor USB do PC ou via HTTP `:8080` do Tab5).
2. O Tab5 OS inclui o app nativo **"Gerenciador de Apps / Instalador"** (ou detector automático no app de Arquivos):
   - Ao tocar em um arquivo `.tab5pkg`, a UI exibe a tela de confirmação do instalador: Nome, Versão, Descrição, Criador e Permissões solicitadas.
   - O OS descompacta o pacote para `/sdcard/apps/installed/<app_id>/`.
   - O OS valida a assinatura/estrutura do `manifest.json`.
   - O novo app é registrado imediatamente no `app_registry` e o ícone aparece na grade do Desktop em tempo real (sem precisar reiniciar o dispositivo).

### 5.2. Ciclo de Execução

1. **Toque no ícone do Desktop:**
   - O SO aloca um ambiente de execução WAMR em PSRAM.
   - O bytecode `app.wasm` é instanciado.
   - O shell transiciona para uma nova janela/tela gerenciada pela `ui_app_bar` do sistema.
   - Executa o ponto de entrada `app_main` / `on_launch`.
2. **Fechamento do App (Botão Fechar na Barra Superior):**
   - O SO invoca `on_close` do app para salvar dados.
   - Destrói os objetos LVGL da tela do app.
   - Libera a memória/instância do WAMR na PSRAM.
   - Retorna ao Desktop com limpeza total de memória (zero vazamento de heap).

---

## 6. Estratégia de Branch & Repositórios (Trabalho em Paralelo)

### 6.1. Branches do repositório `tab5-os`

```
main ───────────────────────────────────────────────► (estável, releases)
   │
develop ─── (linha atual: PoC monolítica contínua)
   │
   └─ feat/app-isolation   ← NOVO BRANCH (a partir da develop atual)
         ├─ components/os → sistema base (WAMR + host bindings + package mgr)
         ├─ 9 apps padrão EMBUTIDAS no firmware (partição dedicada/LittleFS)
         └─ evolui em paralelo sem tocar em develop
```

- `feat/app-isolation` criado a partir da **develop atual** (todo histórico monolítico vira referência).
- SO base mantém **flash funcionando a cada commit** com as 9 apps padrão embutidas.
- Merge → `develop` → validação → release quando estável.

### 6.2. Repositórios das Aplicações

Cada app tem **repositório próprio** com pipeline de build → `.tab5pkg`:

| Repositório | App | Distribuição |
| :--- | :--- | :--- |
| `tab5-os` (este) | SO base + instalador/app store | Firmware |
| `tab5-app-wifi` | WiFi | **Padrão — embutida no firmware** |
| `tab5-app-bluetooth` | Bluetooth | **Padrão — embutida no firmware** |
| `tab5-app-notas` | Notas | **Padrão — embutida no firmware** |
| `tab5-app-terminal` | Terminal | **Padrão — embutida no firmware** |
| `tab5-app-camera` | Câmera | **Padrão — embutida no firmware** |
| `tab5-app-gallery` | Galeria | **Padrão — embutida no firmware** |
| `tab5-app-fileserver` | Servidor | **Padrão — embutida no firmware** |
| `tab5-app-calendar` | Calendário | **Padrão — embutida no firmware** |
| `tab5-app-files` | Arquivos | **Padrão — embutida no firmware** |
| `tab5-app-music` | Música | **Opcional** — instalação via SD |
| `tab5-app-chat` | Chat | **Opcional** — instalação via SD |

### 6.3. Entrega dos Apps Padrão (Embutidos no Firmware)

- Os **9 apps padrão** (`wifi`, `bluetooth`, `notas`, `terminal`, `camera`, `gallery`, `fileserver`, `calendar`, `files`) são compilados como pacotes e **gravados em uma partição dedicada do firmware** (LittleFS/SPIFFS ou partição `apps` custom), imunes à remoção do SD.
- O SO base, no boot, registra os apps da partição embutida no `app_registry` — Desktop mostra os 9 ícones sempre.
- **Música e Chat NÃO vêm embutidas**: ficam como `.tab5pkg` prontos (via repo próprio + download/transferência para `/sdcard/apps/`) para instalação sob demanda.
- Atualizar app embutido = re-flash do firmware ou SO priorizar versão instalada no SD por cima da embutida (mesmo `app_id`, versão SD > embutida).

---

## 7. Fases de Implementação

| Fase | Título | Entregas |
| :--- | :--- | :--- |
| **Fase 0: Branch & Esqueleto** | `feat/app-isolation` + estrutura modular | Branch da develop; estrutura `components/os/{base,runtime,packages}`; spec ABI `tab5_sdk.h`. |
| **Fase 1: Core Host ABI** | Bindings C/C++ do SO | Símbolos nativos (LVGL, I/O, teclado, status, ciclo de vida). |
| **Fase 2: WAMR Runtime** | WebAssembly Micro Runtime | WAMR no ESP-IDF, heap PSRAM, teste `.wasm` no hardware + simulador SDL. |
| **Fase 3: Package Manager** | Pacotes `.tab5pkg` & manifestos | Parser JSON, instalador/desinstalador SD, app registry dinâmico. |
| **Fase 4: Embedded Bundle** | Partição de apps padrão | Partição LittleFS/custom com 9 `.tab5pkg`; seed/registro no boot; precedência SD > embutido. |
| **Fase 5: Installer UI / App Store** | Interface de instalação | Confirmação (nome, versão, permissões); integração com `ui_files`; instalar/atualizar/remover. |
| **Fase 6: SDK & Migração** | `tab5-app-sdk` + provas de conceito | Template de repo (CMake + clang wasi-sdk/Emscripten + `pack.py`); migrar `notas` e `calendar`. |
| **Fase 7: Migração completa** | Portar todas as apps | Migrar 9 padrão p/ embutido + publicar `music`/`chat` opcionais; merge → `develop` → release. |

---

## 8. Decisões Registradas

- **Mecanismo de instalação:** SD Card Package (`.tab5pkg`) — apps instaladas por cima do SO base, com instalador próprio.
- **Modelo de execução:** WebAssembly via WAMR (sandbox, máxima segurança, acesso a API LVGL/SO via símbolos nativos).
- **Branch do SO base:** criado a partir da `develop` atual.
- **Apps padrão:** embutidas no firmware (partição dedicada), imunes à remoção do SD.
- **Apps opcionais:** `music` e `chat` — não instaladas por padrão, disponíveis como `.tab5pkg`.
