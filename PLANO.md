# PoC M5Stack Tab5 — Teclado Virtual + Rotação por Sensor

Plano de implementação. Atualizado em 2026-08-13.

## 1. Contexto técnico

- **Não é Android**: o Tab5 é um ESP32-P4 (RISC-V) com firmware bare-metal em **ESP-IDF v5.5.5 (FreeRTOS)** e UI em **LVGL 9** via `esp_lvgl_port`. O "app" é o firmware inteiro, gravado via `idf.py flash` (USB-C, segurar Boot ~2s para download mode).
- **IDF 5.5+ é obrigatório**: o componente managed `espressif/usb` (dependência do BSP, `usb ^1`) **nunca compila com IDF 5.4.x** — todas as versões 1.0.0–1.5.0 usam `usb_dwc_hal_set_fifo_config` (HAL só existe no 5.5+). Espressif removeu o suporte a 5.4 (PR esp-usb#501). Mínimo: IDF 5.5.3.
- **Display**: 5" MIPI-DSI, painel nativo 720x1280 (retrato), UI default 1280x720 (paisagem) via rotação HW (PPA). Touch I2C: GT911 / ST7123 / ST7121.
- **Sensor**: BMI270 (acel + giro, 6 eixos) no I2C 0x68.
- **Sem rotação automática de UI** — o app lê o vetor de gravidade e rotaciona a tela.
- **BSP oficial**: `espressif/m5stack_tab5` v1.2.0 no registry (variant detection do LCD: ILI9881C → ST7123 → ST7121).
- **Referências**: `M5Tab5-UserDemo` (painel IMU) e `M5Tab5-Keyboard-UserDemo` (lv_textarea + teclado, init de display/touch).

## 2. Decisões de arquitetura

| # | Decisão | Escolha |
|---|---|---|
| D1 | Base do firmware | App standalone (main + components), sem fork do launcher |
| D2 | Teclado | `lv_keyboard` + `lv_textarea` nativos do LVGL 9 |
| D3 | Rotação | Task lê IMU a ~10–20 Hz, calcula ângulo pelo vetor gravidade, aplica `lv_display_set_rotation` e sincroniza touch via esp_lvgl_port |

## 3. Estrutura do projeto

```
tab5-os/
├── sdkconfig.defaults          # target esp32p4, LVGL, DPI 130
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml       # deps: m5stack_tab5, esp_lvgl_port, accel_gyro_bmi270
│   └── app_main.cpp            # init BSP + LVGL + tarefas
└── components/app/
    ├── ui_keyboard.cpp/.h      # lv_textarea + lv_keyboard
    ├── imu_reader.cpp/.h       # wrapper do BMI270 (raw → m/s², °/s)
    └── orientation.cpp/.h      # vetor gravidade → orientação com histerese
```

## 4. Fases

### Fase 0 — Toolchain (~0,5 d)
- Instalar ESP-IDF v5.5.5 + dependências; `idf.py set-target esp32p4`; compilar hello + BSP; flash no Tab5.
- Critério: boot imprime log e a tela acende.

### Fase 1 — Display + Touch (LVGL) (~1 d)
- Init BSP + `esp_lvgl_port`, LVGL 9, RGB565, full-frame buffer em PSRAM (direct_mode), DPI 130.
- Smoke test: botão LVGL que muda de cor ao toque.
- Critério: tocar na tela aciona o callback.

### Fase 2 — Teclado virtual (~1 d)
- `lv_textarea` (área de digitação) + `lv_keyboard` acoplado (modos ABC/número).
- Critério: digitar na tela insere texto no textarea.

### Fase 3 — Leitura do IMU (~1 d)
- Driver `accel_gyro_bmi270`: init(bus) → enable_sensor → get_data.
- Conversão: acc = raw/835.92/10 m/s²; gyr = raw/32.768/10 °/s.
- Log do vetor gravidade no monitor serial.
- Critério: girar o aparelho altera os valores no serial.

### Fase 4 — Rotação dinâmica (~1–2 d, maior risco — spike cedo)
- Mapear vetor gravidade → 4 orientações (0/90/180/270) com histerese (~45°) e debounce (N leituras estáveis antes de girar).
- Aplicar `lv_display_set_rotation` e sincronizar touch com `esp_lvgl_port_set_touch_rotation`.
- Spike: validar rotação em runtime com direct_mode + PPA. Fallback: reconfigurar DSI sw_rotation e/ou reduzir buffer.
- Critério: virar para retrato gira a UI e o toque continua preciso nos novos eixos.

### Fase 5 — Polimento e validação (~1 d)
- Dark mode, feedback visual nas teclas, layout da área de digitação.
- Checklist manual no device: 4 orientações, touch mapping, sem crash em giro rápido.

## 5. Riscos e mitigações

| Risco | Mitigação |
|---|---|
| Rotação em runtime com direct_mode + PPA pode não funcionar de primeira | Spike isolado na Fase 4; fallback sw_rotation + buffer menor |
| Mapping do touch após rotação | Sincronizar via esp_lvgl_port; testar eixo a eixo |
| Variante de HW do LCD | BSP v1.2.0 (variant detection); fallback `m5_tab5_component` |
| Silício do P4 (rev v1.3) | IDF 5.5.5 compila p/ rev >= 3.0 por padrão — setar `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` + `CONFIG_ESP32P4_REV_MIN_100=y` no sdkconfig.defaults (verificado no flash) |
| Tela preta do painel | BSP v1.2.0 tem tabela de init errada (0x71 0x23) p/ painel ST7121 (touch FW=1). Substituir por tabela ST7121 do esp-iot-solution (0x71 0x21, +0x78/0x79/0xA5, SLPOUT 120ms). Patch em managed_components — tornar durável copiando o componente p/ components/. Cold boot (unplug USB) obrigatório |
| `lv_keyboard` nativo visualmente simples | Aceitável para PoC; custom depois |
| Primeiro build do ESP-IDF é pesado | Uma vez preparado, builds incrementais são rápidos |

## 6. Estimativa

~5–7 dias com o hardware em mãos (F0 0,5 · F1 1 · F2 1 · F3 1 · F4 1–2 · F5 1).

---

# Plano — Teclado PT-BR (acentos + cedilha)

Atualizado em 2026-08-14. Proposta para habilitar escrita em português do Brasil (ç, vogais acentuadas etc.). Ainda **não implementado**.

## 1. Verificação prévia (já feita)

- **Fontes OK — nenhuma mudança necessária**: `lv_font_montserrat_14_latin1` cobre Latin-1 (0xA0–0xFF), ou seja, já possui glifos para `ç ã õ á é í ó ú â ê ô à` e maiúsculas (`Ç Ã Õ Á É Í Ó Ú Â Ê Ô À`). Teclas e textarea usam essa fonte.
- **Teclado atual**: usa os mapas padrão do LVGL (só ASCII). O modo `SPECIAL` ("1#") hoje mostra números/símbolos genéricos. O LVGL suporta mapas custom por modo via `lv_keyboard_set_map(kb, mode, map[], ctrl_map[])` (confirmado em `lv_keyboard.c/h`), com 4 modos de usuário livres (`USER_1..4`) além dos padrão.
- **Limitação do mecanismo nativo**: a troca de modo é feita por texto exato de botão ("abc"/"ABC"/"1#"). Criar uma tecla custom de acesso ("áç") exigiria interceptar o handler padrão (static, não removível) — inviável sem hack.

## 2. Abordagem recomendada

**Página de acentos no modo `SPECIAL`** (via `lv_keyboard_set_map`) — aproveita o mecanismo nativo, a tecla "1#" já existente abre a página, sem código custom de troca de modo.

Layout da página de acentos (5 linhas):
```
1  2  3  4  5  6  7  8  9  0        ⌫
à  á  â  ã  ç  é  ê  í  ó  ô  õ  ú
À  Á  Â  Ã  Ç  É  Ê  Í  Ó  Ô  Õ  Ú
,  .  ;  :  !  ?  -  _  +  =  /  @  #
[⌨] [abc] ◀  espaço  ▶  [OK]
```

- Números + símbolos essenciais permanecem na página (datas, pontuação).
- "abc"/⌨ voltam para letras; ⌫/◀/▶/OK mantêm comportamento atual.

**Alternativas descartadas (para PoC)**:
- **B)** Trocar teclas do QWERTY por acentos → cobre menos caracteres e polui o layout.
- **C)** Long-press/popover por tecla → não é nativo no LVGL 9; exige handler custom frágil.

## 3. Implementação (em `components/app/ui_keyboard.cpp`)

1. Definir `pt_br_map_spec[]` (array de strings com `"\n"` separando linhas) e `pt_br_ctrl_spec[]` (array de controles: `LV_KEYBOARD_CTRL_BUTTON_FLAGS` para ⌫/"abc"/⌨/◀/▶/OK; `LV_KB_BTN(n)` para larguras relativas das letras/números) — espelhando o padrão de `default_kb_map_spec` (lv_keyboard.c:156-175).
2. Em `ui_keyboard_create`, após `lv_keyboard_create`: `lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, pt_br_map_spec, pt_br_ctrl_spec)`.
3. `is_action_key()`/`accent_action_keys()` continuam válidos (⌫, OK, ◀, ▶, "abc", "1#" já são reconhecidos).
4. `apply_keyboard_layout` **sem mudança** (5 linhas cabem nos 35%/52% atuais) — validar visualmente; só ajustar se as teclas ficarem baixas.
5. **Polish opcional**: incluir `LV_SYMBOL_NEW_LINE` (Enter) no `is_action_key()` — hoje o Enter do QWERTY não recebe destaque.

## 4. Verificação (hardware)

1. Build + flash + cold boot.
2. Notas → tocar no textarea → teclar **"1#"** → página de acentos.
3. Digitar **todos** os acentos (minúsculas e maiúsculas) + números → conferir renderização e inserção corretas no textarea.
4. Testar **retrato e paisagem** (teclas legíveis) e **temas claro/escuro**.
5. Voltar com "abc" ou ⌨; ⌫ e OK funcionando.

## 5. Riscos / pontos de atenção

- **5 linhas** deixam teclas da página de acentos mais baixas que as do QWERTY (altura do teclado é fixa em % da tela). Mitigação: aumentar o percentual de altura ou reduzir uma linha se ficar ruim.
- **Rótulo da tecla de acesso continua "1#"** (limitação do mecanismo nativo). Melhoria futura: botão custom "áç" com handler próprio.

---

# Plano — WiFi (conexão + configuração + persistência em SD)

Atualizado em 2026-08-15. Proposta para conectar o Tab5 a uma rede WiFi, com tela de configuração (scan/lista/senha), persistência em cartão SD e ícone de status na barra. Ainda **não implementado**.

## 1. Contexto técnico (verificado 2026-08-15)

- **P4 não tem WiFi nativo**: o Tab5 usa um **ESP32-C6-MINI-1U como companion chip** conectado ao P4 via **SDIO2** (`D0=G11, D1=G10, D2=G9, D3=G8, CMD=G13, CK=G12, RESET=G15, IO2=G14`). A alimentação do rádio é `WLAN_PWR_EN` (PI4IOE5V6408-2, pino P0) — o **BSP local já expõe** `bsp_feature_enable(BSP_FEATURE_WIFI, true)` (m5stack_tab5.h:573-591), então o power-on do chip é resolvido pelo BSP.
- **Solução oficial Espressif para P4**: **ESP-Hosted-MCU + componente `esp_wifi_remote`**. A API `esp_wifi_*` é espelhada do host (P4) para o slave (C6) via RPC sobre SDIO (backend `esp_hosted`, até ~50 Mbps TCP). Guia oficial: `docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/api-guides/wifi-expansion.html` · repos `espressif/esp-wifi-remote` e `espressif/esp-hosted-mcu` (docs/esp32_p4_function_ev_board.md).
- **C6 de fábrica**: já vem com firmware ESP-Hosted (companion) — sem flash extra no C6.
- **Cartão SD**: BSP SDMMC slot 0 (`D0=G39, D1=G40, D2=G41, D3=G42, CMD=G44, CLK=G43`), `bsp_sdcard_mount()` → mount point `/sdcard` (`CONFIG_BSP_SD_MOUNT_POINT`), FATFS via `esp_vfs_fat`. API completa no BSP: `bsp_sdcard_sdmmc_mount()`, `bsp_sdcard_unmount()` (bsp_storage.c).
- **NVS já inicializado** em `app_main.cpp:19-24` (padrão Fase 5: `imu_reader` usa `nvs_get_u8/nvs_set_u8` com `NVS_KEY_ROT_ENABLED`).
- **UI existente**: `ui_shell` gerencia telas via `lv_disp_load_scr` (desktop ↔ notas); `ui_bar` é flex row (`gear | spacer | status_badge | relógio`) — o **ícone WiFi entra antes do relógio**; `ui_keyboard` expõe `ui_keyboard_attach(ta)` reutilizável para o campo de senha.

## 2. Decisões de arquitetura

| # | Decisão | Escolha |
|---|---|---|
| D1 | Rádio | ESP-Hosted + `esp_wifi_remote` (companion C6 via SDIO2), API `esp_wifi` padrão; power via `bsp_feature_enable(BSP_FEATURE_WIFI, true)` |
| D2 | Persistência | Arquivo de config no SD (`/sdcard/tab5_os/wifi.cfg`: ssid + senha em linhas); lido no boot para conexão automática |
| D3 | Estado/eventos | Event loop IDF (`WIFI_EVENT` / `IP_EVENT`); módulo `wifi_mgr` centraliza estado (IDLE/SCANNING/CONNECTING/CONNECTED) e notifica a UI |
| D4 | UI de config | Telas novas no padrão do `ui_shell` (wifi_settings + wifi_info); campo de senha com `ui_keyboard_attach` |
| D5 | Ícone na barra | Label na `ui_bar` antes do relógio; cores por estado (text/text_muted/accent); click → tela de config (desconectado) ou info (conectado) |

## 3. Estrutura do projeto

```
components/app/
    ├── wifi_mgr.cpp/.h        # init esp_wifi_remote, scan, connect, eventos, estado
    ├── wifi_storage.cpp/.h    # mount SD + ler/gravar /sdcard/tab5_os/wifi.cfg
    ├── ui_wifi.cpp/.h         # telas de config (scan/lista/senha) e info da conexão
    └── ui_bar.cpp             # ícone WiFi antes do relógio + handlers de clique
```

Deps a adicionar em `main/idf_component.yml`: `espressif/esp_hosted` + `espressif/esp_wifi_remote` (versões compatíveis com IDF 5.5.5, conforme guia Wi-Fi Expansion).

## 4. Fases

### Fase 8 — Alimentação + stack WiFi (~1 d, maior risco — spike cedo)
- `bsp_feature_enable(BSP_FEATURE_WIFI, true)` no boot; add deps `esp_hosted`/`esp_wifi_remote`; `esp_netif_init` + event loop + `esp_wifi_init`/`esp_wifi_start`.
- Spike: scan de redes no log (`esp_wifi_scan_start` + `WIFI_EVENT_SCAN_DONE`) com o aparelho próximo a um AP.
- Critério: log lista SSIDs encontrados.

### Fase 9 — SD: mount + persistência de config (~0,5 d)
- `bsp_sdcard_mount()`; `wifi_storage` lê/grava `/sdcard/tab5_os/wifi.cfg` (cria dir se preciso; senha em texto plano — PoC).
- Critério: gravar config, desligar, religar e reler com os mesmos valores (serial log).

### Fase 10 — Conexão automática + reconexão (~0,5–1 d)
- No boot: se config existe → `esp_wifi_set_config` + `esp_wifi_connect`; tratar `WIFI_EVENT_STA_DISCONNECTED` com retry com backoff; sucesso = `IP_EVENT_STA_GOT_IP` (log do IP).
- Critério: boot conecta sozinho na rede salva; desligar o AP → reconecta quando voltar.

### Fase 11 — UI de configuração (~1–1,5 d, design via @designer)
- Tela `wifi_settings`: botão "Escanear", lista de SSIDs (scroll), seleção abre campo de senha (texto com `ui_keyboard_attach`), botão "Conectar" → salva config + conecta.
- Critério: fluxo completo descobrir → senha → conectar → status conectado.

### Fase 12 — Ícone de status + tela de info (~0,5–1 d, design via @designer)
- Ícone WiFi na `ui_bar` antes do relógio (estados: apagado/muted = desconectado, accent = conectado, ânimo de scan opcional).
- Click desconectado → abre `wifi_settings`; click conectado → `wifi_info` (SSID, IP, status) + botão "Reconfigurar WiFi" → `wifi_settings`.
- Critério: ícone reflete estado; cliques navegam corretamente.

## 5. Riscos e mitigações

| Risco | Mitigação |
|---|---|
| ESP-Hosted/`esp_wifi_remote` em IDF 5.5.5 pode exigir versões específicas | Spike na Fase 8 com o guia oficial Wi-Fi Expansion; versões pinadas no idf_component.yml |
| SD e WiFi compartilham periféricos/tensão | SDMMC (G39-44) e SDIO2 do C6 (G8-15) são barramentos distintos — sem conflito de pinos; validar power budget (SD 3.3V + rádio) |
| C6 precisa do firmware ESP-Hosted de fábrica | Verificar na Fase 8 (erro de transporte SDIO indica FW ausente/errado; flashar `esp_hosted` slave se necessário) |
| Scan/connect assíncrono + UI (thread-safety LVGL) | Todos os callbacks de evento publicam estado; UI reage via `lv_timer` na task LVGL (padrão já usado na rotação) |
| Senha em texto plano no SD | Aceitável para PoC; nota no README |
| C6 é 2.4 GHz apenas | Lista apenas redes 2.4 GHz; ignorar 5 GHz no scan |

## 6. Verificação (hardware)

1. Build + flash + cold boot (unplug USB) + monitor `--no-reset`.
2. Fase 8: scan lista SSIDs 2.4 GHz no serial.
3. Fase 9: `wifi.cfg` criado no SD; re-leitura após reboot com mesmos valores.
4. Fase 10: boot conecta na rede salva (GOT_IP no serial); AP desligado → retry; AP de volta → reconecta.
5. Fase 11: fluxo completo pela tela (escanear → selecionar → senha com teclado → conectar), em retrato e paisagem, temas claro/escuro.
6. Fase 12: ícone correto por estado; clique desconectado abre config; clique conectado abre info com "Reconfigurar WiFi" voltando à config.
