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

---

# Planejamento: Aplicação "Arquivos" (Fase 16)

## 1. Contexto & Objetivo
- Criação do aplicativo **"Arquivos"** (File Manager do `tab5-os`), permitindo navegar e visualizar diretórios e arquivos armazenados no cartão SD (`/sdcard`).
- Dois modos de exibição dinâmicos com alternador na barra de ferramentas:
  1. **Modo Ícones (Grade/Grid)**: Pastas e arquivos dispostos em blocos com ícone (`LV_SYMBOL_DIRECTORY` / `LV_SYMBOL_FILE`) e nome.
  2. **Modo Lista (Detalhes)**: Tabela/lista com nome, tamanho formatado (Bytes, KB, MB ou `<DIR>`) e data/hora da última modificação (`DD/MM/AAAA HH:MM`).
- Navegação interativa: toque em diretórios para abrir subpastas e botão de retorno para subir de nível até a raiz `/sdcard`.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha |
|---|---|---|
| D1 | Launcher/Desktop | Adição de tile "Arquivos" no `ui_desktop.cpp` ao lado de Notas e WiFi |
| D2 | Leitura de Arquivos | APIs POSIX padrão (`opendir`, `readdir`, `stat`) sobre o VFS do FATFS (`/sdcard`) |
| D3 | Modos de Exibição | Estado interno com alternador: `VIEW_MODE_GRID` e `VIEW_MODE_LIST`, re-renderizando a área de visualização |
| D4 | UI e Janela | Padrão `ui_shell` com barra própria (`surface_alt`), botão voltar (`LV_SYMBOL_PREV`), caminho atual, alternador de visualização e botão fechar |
| D5 | Redimensionamento | Integração com `ui_shell_notify_keyboard_layout()` e rotação dinâmica de tela |

## 3. Estrutura de Arquivos

```
components/app/
├── include/
│   ├── ui_files.h         # Declarações do ciclo de vida e visualização do app Arquivos
├── ui_files.cpp           # Leitura do SD, alternância de modos (ícones/lista) e navegação
├── ui_desktop.cpp         # Novo tile 'Arquivos' no desktop
├── ui_shell.cpp           # Integração com ui_shell_open_files / close_files
└── CMakeLists.txt         # Registro de ui_files.cpp
```

## 4. Fases de Execução da Funcionalidade

### Etapa 1 — Backend de Listagem de Arquivos
- Mapeamento e paginação/leitura de diretórios via `stat` e `readdir`.
- Formatação de tamanho (`B`, `KB`, `MB`) e data (`localtime_r`).
- Tratamento para SD ausente ou pastas vazias.

### Etapa 2 — Criação da Interface `ui_files`
- Janela com barra de navegação superior (título/caminho, botão subir nível `..`, alternador Ícones/Lista, fechar).
- Renderização em modo Grade (`LV_FLEX_FLOW_ROW_WRAP`).
- Renderização em modo Lista (`LV_FLEX_FLOW_COLUMN`).

### Etapa 3 — Integração com o Desktop e Shell
- Registro do tile no launcher desktop.
- Transições de tela no `ui_shell`.
- Suporte a temas claro/escuro e rotação de tela.

### Etapa 4 — Validação e Gravação
- Execução de `pre-commit run --all-files`.
- Teste em hardware navegando pelas pastas e arquivos criados pelo sistema (`/sdcard/tab5_os/`).

---

# Planejamento: Persistência de Notas e Registro de Associações de Arquivo (Fase 17)

## 1. Contexto & Objetivo
- **Salvar Notas em `/sdcard/notas/`**:
  - A aplicação **Notas** salvará e carregará notas como arquivos `.txt`.
  - A pasta `/sdcard/notas` será criada automaticamente se não existir.
  - O app Notas terá botões de ação na barra superior: **Salvar** (`LV_SYMBOL_SAVE`), **Novo** (`LV_SYMBOL_PLUS`) e indicador do arquivo em edição.
  - Ao salvar uma nova nota, gera nome automático com timestamp ou sequencia numerica (ex: `nota_YYYYMMDD_HHMM.txt`), ou atualiza o arquivo aberto.
- **Tabela / Registro de Associações de Tipos de Arquivo (File Associations)**:
  - Criação de um módulo central no SO (`file_assoc`) que mapeia extensões de arquivo para seus respectivos manipuladores de abertura.
  - Registro de extensão `.txt` mapeado para abrir o app Notas com o caminho do arquivo.
- **Abertura de Arquivos a partir do app "Arquivos"**:
  - No aplicativo **Arquivos**, ao clicar em um item de arquivo (ex: `documento.txt`), o sistema consulta a tabela de associações.
  - Se houver um app associado para a extensão `.txt`, o arquivo é aberto diretamente no aplicativo **Notas**, carregando o seu conteúdo para leitura e edição.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha |
|---|---|---|
| D1 | Registro de Extensões | Módulo `file_assoc.h` / `file_assoc.cpp` com tabela `ext -> handler_fn(const char *path)` |
| D2 | Diretório Padrão de Notas | `/sdcard/notas/` (criado automaticamente com `mkdir`) |
| D3 | Ações no App Notas | Botão **Salvar** (`LV_SYMBOL_SAVE`), **Novo** (`LV_SYMBOL_PLUS`) e exibição do nome do arquivo |
| D4 | Integração no App Arquivos | `item_click_cb`: se `S_ISDIR`, entra na pasta; se for arquivo, consulta `file_assoc_open()` |
| D5 | Abertura com Argumento | `ui_shell_open_notas_file(const char *filepath)` carrega o conteúdo via I/O no `notas_ta` |

## 3. Estrutura de Arquivos

```
components/app/
├── include/
│   ├── file_assoc.h       # [NEW] Registro de extensões do sistema (.txt -> Notas)
│   ├── ui_notas.h         # [MODIFY] Exporta ui_notas_open_file e ui_notas_save_current
│   └── ui_files.h         # [MODIFY] Mantém interface do navegador de arquivos
├── file_assoc.cpp         # [NEW] Implementação do dispatcher de extensões
├── ui_notas.cpp           # [MODIFY] Botões Salvar/Novo, lógica de I/O em /sdcard/notas/
├── ui_files.cpp           # [MODIFY] Acionamento de file_assoc_open_file() no clique de arquivo
├── ui_shell.cpp           # [MODIFY] Roteamento de abertura de notas com caminho de arquivo
└── CMakeLists.txt         # [MODIFY] Registro de file_assoc.cpp
```

## 4. Fases de Execução da Funcionalidade

### Etapa 1 — Módulo `file_assoc`
- Tabela associativa de extensões de arquivo para callbacks de abertura do sistema.
- Função `file_assoc_open(const char *filepath)` para despacho automático.

### Etapa 2 — Aprimoramento do App "Notas"
- Botões Salvar (`💾`) e Novo (`+`) na barra do aplicativo.
- I/O com `/sdcard/notas/` (gravação e leitura de arquivos `.txt`).
- Indicador do nome do arquivo ativo na barra superior.

### Etapa 3 — Integração no App "Arquivos"
- Ao tocar em um arquivo `.txt` na grade ou na lista, dispara a abertura automática no app Notas.

### Etapa 4 — Validação e Gravação
- Executar `pre-commit run --all-files`.
- Compilação e gravação no dispositivo.
- Teste prático de criação, salvamento e reabertura via gerenciador de arquivos.

---

# Planejamento: Múltiplas Redes WiFi, Status Conectado/Salvo, Desconectar e Esquecer (Fase 18)

## 1. Contexto & Objetivo
- Suporte a múltiplas redes salvas no arquivo `/sdcard/tab5_os/wifi.cfg`.
- Identificação visual na lista de scan do app WiFi:
  - Indicação de rede atualmente **Conectada** (`LV_SYMBOL_OK` / `(Conectado)`).
  - Indicação de redes **Salvas** (`LV_SYMBOL_SAVE` / `(Salva)`).
- Ações contextuais de rede:
  - Botão **Desconectar** para a rede ativa.
  - Botão **Esquecer** para apagar as credenciais salvas de uma rede.
  - Botão **Conectar** usando a senha salva ou inserindo nova senha.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha |
|---|---|---|
| D1 | Formato do `wifi.cfg` | Múltiplas seções `[rede]` ou linhas `ssid=...` e `password=...`, mantendo compatibilidade retroativa |
| D2 | API de Armazenamento | `wifi_storage_load_all()`, `wifi_storage_add_or_update()`, `wifi_storage_remove()`, `wifi_storage_find()` |
| D3 | Controle de Conexão | `wifi_mgr_disconnect()`, `wifi_mgr_forget()` e seleção de melhor rede conhecida no boot |
| D4 | UI Contextual | Botões Conectar / Desconectar / Esquecer dinâmicos conforme o estado da rede selecionada |

## 3. Fases de Execução

### Etapa 1 — Backend `wifi_storage`
- Implementar parsing e serialização de múltiplas redes em `wifi.cfg`.
- Operações de busca, adição, atualização e remoção de rede.

### Etapa 2 — Controle `wifi_mgr`
- Implementar métodos de desconexão e esquecimento de rede no FreeRTOS/ESP-WiFi.

### Etapa 3 — Interface de Usuário `ui_wifi`
- Renderização de badges/ícones na listagem (Conectado / Salva / Sinal).
- Controles de ação: Conectar, Desconectar, Esquecer e campo de senha contextual.

### Etapa 4 — Validação e Gravação
- `pre-commit run --all-files` 100% aprovado.
- Gravação no Tab5 e validação em hardware.

---

# Planejamento: Gerenciador de Conexão Bluetooth, Periféricos e Supressão de Teclado Virtual (Fase 19)

## 1. Contexto & Objetivo
- Criação do aplicativo **"Bluetooth"** (Gerenciador de Conexões Bluetooth do `tab5-os`) e infraestrutura para periféricos sem fio (teclados, mouses e fones de ouvido).
- **Aplicativo Próprio (`ui_bluetooth`)**: Tela com interface de busca, listagem de dispositivos com ícones por categoria (`LV_SYMBOL_KEYBOARD`, `LV_SYMBOL_AUDIO`, `LV_SYMBOL_SETTINGS`), badges de status (Conectado / Pareado / RSSI) e ações contextuais (Conectar, Desconectar, Parear, Esquecer).
- **Ícone na Barra de Status (`ui_status` / `ui_bar`)**: Indicador `LV_SYMBOL_BLUETOOTH` ao lado do Wi-Fi, com cor atenuada (`text_muted`) quando desconectado e cor de destaque (`accent`) quando conectado a pelo menos um dispositivo. Toque no ícone abre o app Bluetooth.
- **Launcher / Desktop (`ui_desktop`)**: Tile 76x76 estilizado com `LV_SYMBOL_BLUETOOTH` e rótulo "Bluetooth".
- **Supressão Inteligente do Teclado Virtual**:
  - Quando um teclado Bluetooth físico estiver conectado, o teclado virtual (`ui_keyboard`) **permanece oculto** ao focar campos de texto (`lv_textarea`), permitindo que a aplicação ativa aproveite 100% da altura da tela.
  - Ao desconectar o teclado físico, o teclado virtual volta a ser exibido automaticamente sob toque na tela.
- **Gerenciador e Persistência (`bt_mgr` & `bt_storage`)**: Controle assíncrono do rádio Bluetooth (via ESP-Hosted / ESP32-C6 companion) e salvamento de dispositivos pareados em `/sdcard/tab5_os/bt.cfg` para reconexão automática no boot.
- **Entrada e Áudio**: Drivers `lv_indev_t` para injeção de teclas de teclado e ponteiro de mouse no LVGL, e roteamento de áudio para fones pareados.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha |
|---|---|---|
| D1 | Rádio & Host Stack | ESP-Hosted Bluetooth HCI (ESP32-C6 companion via SDIO2) + Host Stack no ESP32-P4 |
| D2 | Janela do App | `ui_bluetooth.cpp` integrado ao `ui_shell` (`ui_shell_open_bluetooth` / `ui_shell_close_bluetooth`) |
| D3 | Status Bar | Ícone `LV_SYMBOL_BLUETOOTH` em `ui_status.cpp` interativo com navegação |
| D4 | Persistência | `/sdcard/tab5_os/bt.cfg` através de `bt_storage.cpp` |
| D5 | Categorização | Ícones dedicados por tipo: `LV_SYMBOL_KEYBOARD` (teclados), `LV_SYMBOL_AUDIO` (fones), `LV_SYMBOL_SETTINGS` (mouses) |
| D6 | Supressão de Teclado | Interceptação em `ui_keyboard_attach()` consultando `bt_mgr_is_keyboard_connected()` |
| D7 | Dispositivos de Entrada | Registro de `lv_indev_t` virtual para teclado e mouse no LVGL 9 |

## 3. Estrutura de Arquivos

```
components/app/
├── include/
│   ├── bt_mgr.h           # [NEW] API e eventos do gerenciador Bluetooth
│   ├── bt_storage.h       # [NEW] Persistência em /sdcard/tab5_os/bt.cfg
│   ├── ui_bluetooth.h     # [NEW] Interface do aplicativo Bluetooth
│   ├── ui_keyboard.h      # [MODIFY] Suporte à supressão do teclado virtual
│   ├── ui_status.h        # [MODIFY] Ícone de status Bluetooth
│   └── ui_shell.h         # [MODIFY] Transições de tela do Bluetooth
├── bt_mgr.cpp             # [NEW] Implementação do backend Bluetooth
├── bt_storage.cpp         # [NEW] I/O do arquivo bt.cfg
├── ui_bluetooth.cpp       # [NEW] Interface gráfica do app Bluetooth
├── ui_keyboard.cpp        # [MODIFY] Lógica condicional de visibilidade
├── ui_desktop.cpp         # [MODIFY] Tile 'Bluetooth' no launcher
├── ui_status.cpp          # [MODIFY] Indicador Bluetooth na barra superior
├── ui_shell.cpp           # [MODIFY] Ciclo de vida da tela bluetooth_scr
└── CMakeLists.txt         # [MODIFY] Registro dos novos módulos e deps
```

## 4. Fases de Execução da Funcionalidade

### Etapa 1 — Backend de Armazenamento e Configuração (`bt_storage`)
- Definição do formato e parsing do arquivo `/sdcard/tab5_os/bt.cfg`.
- Métodos para carregar, adicionar, atualizar e remover dispositivos pareados.

### Etapa 2 — Gerenciador Bluetooth e Detecção de Dispositivos (`bt_mgr`)
- Inicialização do rádio e ciclo de scan assíncrono.
- Classificação por perfil (Teclado, Mouse, Fone de ouvido).
- Funções de conexão, pareamento, desconexão e checagem de dispositivos conectados.

### Etapa 3 — Supressão Inteligente do Teclado Virtual (`ui_keyboard`)
- Modificação de `ui_keyboard_attach` para checar `bt_mgr_is_keyboard_connected()`.
- Ocultação do teclado virtual e liberação da altura total para os aplicativos.
- Transição em tempo real ao conectar/desconectar teclado físico.

### Etapa 4 — Interface do Aplicativo (`ui_bluetooth`)
- Janela do app com barra superior, botão fechar, botão de busca e lista dinâmica de dispositivos.
- Painel de ações contextuais (Conectar / Desconectar / Esquecer / Parear).
- Suporte a temas claro/escuro e rotação retrato/paisagem.

### Etapa 5 — Integração com o Sistema (Desktop, Barra de Status e Shell)
- Adição do tile "Bluetooth" no desktop launcher.
- Adição do ícone interativo de conexão na barra de status.
- Roteamento e transições no `ui_shell`.
- Inicialização em `main/app_main.cpp`.

### Etapa 6 — Mapeamento de Drivers HID e Áudio
- Registro de drivers de entrada LVGL para teclado e mouse.
- Roteamento de áudio para fones conectados.

### Etapa 7 — Validação e Gravação
- Executar `pre-commit run --all-files`.
- Compilação do firmware e validação em hardware.

## 5. Status de Conclusão: CONCLUÍDO (100%)
- **Backend BLE NimBLE (HCI via ESP-Hosted / SDIO)**: 100% operacional.
- **Descoberta de Serviços e Descritores 0x2902 (CCCD)**: Pipeline sequencial ativado com sucesso.
- **Entrada de Teclado Físico no Notas**: Mapeamento completo de caracteres e teclas especiais no LVGL 9.
- **Reconexão Automática e Auto-Conexão no Boot**: 100% testado e validado em hardware com o teclado físico sem fio.

---

# Fase 7: Controles de Liga/Desliga para Wi-Fi e Bluetooth no Menu de Configurações

## 1. Visão Geral
Adicionar opções independentes no Menu de Configurações (painel da engrenagem) para habilitar e desabilitar o **Wi-Fi** e o **Bluetooth**, com controle efetivo dos rádios, persistência em NVS, bloqueio de buscas/conexões manuais quando inativos, e restauração imediata da auto-reconexão inteligente com redes e dispositivos pareados quando reativados.

## 2. Escopo e Entregáveis
1. **Menu de Configurações (`ui_bar.cpp`)**:
   - Linhas com switches para **Wi-Fi** e **Bluetooth** abaixo de "Rotação".
2. **Gerenciador de Wi-Fi (`wifi_mgr.h`, `wifi_mgr.cpp`)**:
   - APIs `wifi_mgr_set_enabled(bool)` e `wifi_mgr_is_enabled()`.
   - Persistência de estado em NVS (`radios` / `wifi_en`).
   - Bloqueio de conexões e buscas quando desativado; auto-reconexão imediata a redes salvas ao reativar.
3. **Gerenciador de Bluetooth (`bt_mgr.h`, `bt_mgr.cpp`)**:
   - APIs `bt_mgr_set_enabled(bool)` e `bt_mgr_is_enabled()`.
   - Persistência de estado em NVS (`radios` / `bt_en`).
   - Desconexão geral, cancelamento de conexões/scans e restauração do teclado virtual quando desativado.
   - Auto-reconexão imediata e transparente a dispositivos pareados ao reativar.
4. **Indicadores na Barra Superior (`ui_status.cpp`)**:
   - Atualização visual dos ícones de status refletindo os estados ligado/desligado/conectado.
5. **Aplicativos de Rede (`ui_wifi.cpp` e `ui_bluetooth.cpp`)**:
   - Bloqueio e feedback quando o respectivo rádio estiver desligado.

## 3. Status de Conclusão: CONCLUÍDO (100%)
- **Switches de Wi-Fi e Bluetooth no Menu**: Integrados ao painel popover e compatíveis com temas claro/escuro.
- **Persistência em NVS**: Salvo na partição NVS (`radios`) e restaurado em cada boot.
- **Controle Efetivo de Rádio**: Desconexão e bloqueio de buscas quando desativado; auto-reconexão imediata e inteligente a redes (`wifi.cfg`) e dispositivos Bluetooth (`bt.cfg`) quando reativado.
- **Sincronização Visual**: Barra de status e aplicativos de rede refletem em tempo real o estado de cada rádio.
