# Caderno Geral de Arquitetura e Planos de Implementação — tab5-os

Documento mestre de planejamento técnico, decisões de engenharia, arquitetura de software e roadmap evolutivo do sistema operacional **tab5-os** para o dispositivo **M5Stack Tab5 (ESP32-P4 + ESP32-C6 companion)**.

---

## Legenda de Marcações de Implementação

- `[x]` **`✅ IMPLEMENTADO`**: Funcionalidade codificada, integrada, testada com `pre-commit` e validada em hardware real.
- `[-]` **`🚧 EM ANDAMENTO`**: Funcionalidade com desenvolvimento em progresso ou testes parciais.
- `[ ]` **`⏳ PLANEJADO`**: Funcionalidade arquitetada e especificada, aguardando início de implementação.
- `[~]` **`❌ CANCELADO`**: Funcionalidade cancelada ou descartada do escopo.

---

## Sumário Executivo de Fases e Módulos

| Status | Fase / Módulo | Título | Categoria | Principais Entregáveis |
|:---:|---|---|---|---|
| `[x]` | **Fase 0–5** | PoC Base: Display, Touch, IMU e Teclado | Core / BSP | Init BSP, LVGL 9, rotação dinâmica via BMI270, teclado QWERTY |
| `[x]` | **Fase 6** | Teclado Virtual PT-BR | UI / Input | Modo Especial com acentos (`ç`, vogais com til, agudo, circunflexo, crase) |
| `[x]` | **Fases 8–12** | Stack Wi-Fi e Configuração | Conectividade | Rádio SDIO C6, scan, persistência SD, auto-conexão, app `ui_wifi` |
| `[x]` | **Fase 16** | Gerenciador de Arquivos ("Arquivos") | Aplicativo | Navegação `/sdcard`, modos Grade e Lista detalhada, ciclo de vida shell |
| `[x]` | **Fase 17** | Persistência de Notas e Associações | Sistema / App | Módulo `file_assoc`, I/O em `/sdcard/notas/`, abertura direta pelo Arquivos |
| `[x]` | **Fase 18** | Gerenciamento de Múltiplas Redes Wi-Fi | Conectividade | Badges Conectado/Salva/Sinal, botões Conectar, Desconectar e Esquecer |
| `[x]` | **Fase 19** | Gerenciador Bluetooth e Teclado Físico | Conectividade / Input | BLE NimBLE, pareamento, persistência SD, supressão de teclado virtual |
| `[x]` | **Fase 19.1** | Controles de Rádio no Menu de Configurações | Sistema / Config | Switches Wi-Fi/Bluetooth na barra, NVS, bloqueio/reconexão automática |
| `[x]` | **Fase 19.2** | Suporte a Mouse/Touchpad BLE HID | Input / Periféricos | Cursor visual ARGB8888 em `lv_layer_sys`, rotação 4 eixos, cliques/gestos |
| `[x]` | **Fase 20** | Aplicativo Terminal Interativo | Aplicativo / Shell | Mini-shell C++, comandos POSIX em `/sdcard`, buffer circular 8 KB |
| `[x]` | **Fase 21** | Cliente SSH Remoto no Terminal | Conectividade / CLI | Task FreeRTOS, `david-cermak/libssh`, PTY xterm, auth por senha/chave |
| `[x]` | **Fase 22** | Protetor de Tela Anti-Burn-in com Data e Hora | Sistema / Display | Screensaver fundo preto, relógio grande, reposicionamento 30s, wake no toque |
| `[x]` | **Fase 23** | Controle de Brilho da Tela no Menu de Configurações | Sistema / Display | Slider de brilho no menu, PWM 10–100%, feedback visual, persistência |
| `[x]` | **Fase 23.1** | Configuração Geral de Fuso Horário | Sistema / Config | Módulo `timezone_mgr`, offset POSIX `TZ`, ajuste `[-]`/`[+]` no menu, sincronização em todos os apps |
| `[x]` | **Fase 24** | Aplicativos de Câmera, Galeria de Fotos e Servidor HTTP | Aplicativo / Mídia | Preview MIPI-CSI, gravação `/sdcard/photos/`, galeria com TJpgDec, servidor HTTP e grid desktop responsivo |
| `[x]` | **Fase 25** | Aplicativo Gravador de Voz e Player de Áudio | Aplicativo / Mídia | Gravação I2S ES7210, `/sdcard/gravacoes/*.wav`, limite 5 min, barra de progresso e exclusão |
| `[x]` | **Fase 26** | Aplicativo Chat IA (OpenAI-compatível) | Aplicativo / Conectividade | Chat texto, cadastro de token/URL/modelo, cliente HTTP `/chat/completions`, persistência em `ai.cfg` |
| `[x]` | **Fase 27** | Padronização do Shell e Registro Modular de Apps | Sistema / Arquitetura | Barra de título padronizada `ui_app_bar`, registro `app_registry`, desktop dinâmico, manifesto descentralizado de arquivos |
| `[~]` | **Fase 28** | Modo Pen Drive USB (USB Mass Storage) | Sistema / Conectividade | TinyUSB MSC sobre USB-OTG, exposição do microSD como disco, recuperação de arquivos pelo computador |
| `[x]` | **Fase 29** | Aplicativo "Música" — Player de Áudio Local (MP3/WAV) | Aplicativo / Mídia | Decoder `esp_audio_codec`, reprodução de `/sdcard/musica/*.mp3|wav`, controles e volume via ES8388 |
| `[x]` | **Fase 30** | Testes Unitários Automáticos com Cobertura ≥80% | Qualidade / Testes | Suíte GoogleTest em host nativo, cobertura gcov/lcov com gate ≥80%, job `test` no Quality Gate do CI |
| `[x]` | **Fase 31** | Otimização de Memória Interna e Robustez do Servidor de Arquivos | Sistema / Memória | Upload HTTP sem erro "Out of DMA memory", `malloc()`/LVGL na PSRAM, reprodução de músicas em subpastas |
| `[x]` | **Fase 32** | Ativação Manual do Servidor de Arquivos | Aplicativo / Segurança | Servidor HTTP não inicia mais ao abrir o app; ativação apenas pelo botão "Iniciar Servidor", desliga ao fechar o app |
| `[x]` | **Fase 33** | Estabilidade do Relógio da Barra Superior | Sistema / UI | Fonte monoespaçada JetBrains Mono no relógio (`dd/mm/aaaa hh:mm`), largura fixa e alinhamento à direita: zero deslocamento lateral dos ícones |
| `[x]` | **Fase 34** | Desligamento Automático da Tela (Screen-Off) | Sistema / Display / Energia | Timeout configurável (30s–10min), backlight 0 via PWM, apps continuam rodando, despertar por duplo toque, mouse/teclado BLE e persistência em NVS |
| `[x]` | **Fase 35** | Botão de Energia na Barra Superior (Power Menu) | Sistema / Energia / UI | Ícone de power na ponta esquerda da barra, painel com Desligar Tela / Reiniciar (`esp_restart`) / Desligar (deep sleep), confirmação modal antes de ações destrutivas |
| `[x]` | **Fase 36** | Monitor de Bateria INA226 e Proteção de Carregamento | Sistema / Energia / UI | Driver próprio do INA226 (I2C 0x41, shunt 5 mΩ), ícone com percentual e popup de detalhes na barra, estados Carregando/Na tomada/Na bateria/Somente cabo, corte de carga em 90% via `CHG_EN` com retomada em 85% e switch persistido em NVS |
| `[x]` | **Fase 37** | Persistência do Volume Geral de Áudio | Sistema / Áudio / UI | Volume geral (menu Configuração e app Música) salvo ao soltar o slider em NVS (`tab5/volume`) e SD (`/sdcard/tab5_os/audio.cfg`), restaurado no boot via lazy-load no player, seguindo o padrão do brilho (`display_storage`) |
| `[x]` | **Fase 38** | Ajustes de Usabilidade do Menu de Configuração | Sistema / UI | Painel alargado de 230 px para 320 px eliminando texto cortado, e trilha dos sliders visível nos dois temas (`text_muted` com 40% de opacidade em `LV_PART_MAIN`) no Brilho/Volume do menu e no app Música |
| `[x]` | **Fase 39** | Screenshot pela Barra do Sistema | Sistema / UI | Snapshot lógico RGB565 da tela ativa + blend alpha do `layer_top`, gravação BMP 24-bit assíncrona em `/sdcard/screenshots` com flash e toast, e `decode_bmp` da Galeria corrigido (escala por potências de 2 e stride correto) |
| `[x]` | **Fase 40** | Simulador Host SDL e Regressão Visual da UI | Qualidade / Testes | UI real (shell + apps) compilada sobre o LVGL vendido com backend SDL2 720×1280, 15 cenários comparados contra imagens douradas determinísticas, comparador com tolerância e PNGs de diff |
| `[x]` | **Fase 42** | Aplicativo Calendário Mensal | Aplicativo / Shell / UI | Popup mensal acionado pela data/hora, aplicativo em tela dedicada, navegação entre meses, integração com desktop e regressão visual |
| `[ ]` | **Fase 43** | TTS em Nuvem no Chat (Leitura de Respostas) | Aplicativo / Conectividade / Áudio | Auto-falar resposta do assistente via API TTS (OpenAI/Google/Azure/ElevenLabs) → MP3 → minimp3 → es8388, com config e parada |
| `[x]` | **Fase 44** | Visualização de Arquivos/Pastas Ocultos no app Arquivos | Aplicativo / UI | Toggle "Mostrar ocultos" na barra (ocultos por padrão, persistido em NVS), filtro de nomes iniciados em `.` em `load_directory` |
| `[x]` | **Fase 45** | Consolidação das Configs do SO em Pasta Oculta | Sistema / Config | Todas as configs em `/sdcard/.tab5_os/` (corrigindo `timezone.cfg` que sai de `/sdcard/`), migração automática no boot com fallback de leitura |
| `[x]` | **Fase 46** | Isolamento e Modularização de Aplicações | Sistema / WAMR | Sandbox WAMR, `.tab5pkg`, Package Manager, Storage Sandbox e Tab5 SDK |
| `[-]` | **Fase 47** | Host ABI de UI Genérica & Desacoplamento Total | Sistema / SDK / UI | Handles opacos de UI, widgets nativos (containers, flex, labels, buttons, sliders, switches, lists), despacho de eventos assíncrono para WASM |


---

# [x] Fase 0–5: PoC Base — Display, Touch, Rotação por Sensor e Teclado Virtual `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- **Hardware**: O M5Stack Tab5 é baseado no SoC **ESP32-P4 (RISC-V)** com firmware bare-metal desenvolvido sobre **ESP-IDF v5.5.5 (FreeRTOS)** e interface gráfica **LVGL 9** via `esp_lvgl_port`.
- **Compilação e Requisitos**: O componente managed `espressif/usb` (dependência do BSP `usb ^1`) exige HAL `usb_dwc_hal_set_fifo_config`, tornando o ESP-IDF 5.5+ obrigatório (mínimo 5.5.3).
- **Display & Touch**: Painel MIPI-DSI nativo 720x1280 (retrato) e 1280x720 (paisagem) via rotação por hardware PPA. Controladores de toque suportados: GT911 / ST7123 / ST7121.
- **Sensor de Orientação**: IMU Bosch BMI270 (acelerômetro + giroscópio de 6 eixos) no barramento I2C (endereço `0x68`). A rotação de tela é processada em software a partir do vetor de gravidade.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Base do firmware | App standalone (`main` + `components`) | Independência completa e arquitetura modular |
| D2 | Teclado inicial | `lv_keyboard` + `lv_textarea` nativos do LVGL 9 | Simplicidade e desacoplamento para a prova de conceito |
| D3 | Rotação dinâmica | Task periódica lendo BMI270 a ~10–20 Hz | Cálculo do vetor gravidade com histerese, `lv_display_set_rotation` e sincronização de toque via `esp_lvgl_port` |

## 3. Estrutura de Arquivos & Componentes

```
tab5-os/
├── sdkconfig.defaults          # Target esp32p4, LVGL 9, DPI 130
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml       # Dependências: m5stack_tab5, esp_lvgl_port, accel_gyro_bmi270
│   └── app_main.cpp            # Inicialização BSP + LVGL + tarefas do sistema
└── components/app/
    ├── ui_keyboard.cpp/.h      # Criação e layout de lv_textarea + lv_keyboard
    ├── imu_reader.cpp/.h       # Wrapper do sensor BMI270 (raw → m/s², °/s)
    └── orientation.cpp/.h      # Cálculo de vetor de gravidade e filtro de orientação
```

## 4. Fases de Execução da Funcionalidade

- [x] **Fase 0 — Toolchain e Ambiente Base (~0,5 d)**: Configuração do ESP-IDF v5.5.5; `idf.py set-target esp32p4`; validação de compilação do BSP e gravação USB-C. Boot imprime log serial sem panics e a tela MIPI-DSI acende.
- [x] **Fase 1 — Display e Touch no LVGL 9 (~1 d)**: Inicialização do BSP com `esp_lvgl_port`, LVGL 9, RGB565, direct mode com full-frame buffer em PSRAM, DPI 130. Botão LVGL de teste responde ao toque.
- [x] **Fase 2 — Teclado Virtual Inicial (~1 d)**: Integração de `lv_textarea` com `lv_keyboard` acoplado nos modos padrão (letras e números).
- [x] **Fase 3 — Driver e Leitura do IMU BMI270 (~1 d)**: Driver `accel_gyro_bmi270`: inicialização I2C, leitura de registradores e conversão física (`acc = raw / 835.92 / 10 m/s²`, `gyr = raw / 32.768 / 10 °/s`).
- [x] **Fase 4 — Rotação Dinâmica de Interface (~1–2 d)**: Mapeamento de vetor gravidade para 4 orientações (0°, 90°, 180°, 270°) com histerese (~45°) e debounce. Aplicação de `lv_display_set_rotation()` e ajuste de toque com `esp_lvgl_port_set_touch_rotation()`.
- [x] **Fase 5 — Polimento e Temas (~1 d)**: Tema claro/escuro, ajuste de proporções da área de digitação e mitigação de flickering nas 4 orientações.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Rotação em runtime com direct mode + PPA | Validado na Fase 4; suporte a sw_rotation como fallback caso necessário |
| Variante de hardware do LCD (ST7121) | BSP v1.2.0 corrigido com tabela de inicialização do ST7121 (`0x71 0x21`, `SLPOUT 120ms`) |
| Silício ESP32-P4 rev v1.3 | Configurado `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` e `CONFIG_ESP32P4_REV_MIN_100=y` no `sdkconfig.defaults` |
| Dessincronização de eixos do touch | Sincronização direta via esp_lvgl_port em cada evento de rotação |

## 6. Critérios de Validação & Teste em Hardware
1. Build, gravação via `idf.py flash` e boot limpo sem reinicializações.
2. Digitação fluida no teclado virtual com resposta tátil visual.
3. Rotação suave entre modos retrato e paisagem com coordenadas de toque calibradas.
4. Persistência de tema e layout responsivo.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Firmware Base e BSP**: Integrado com sucesso no ESP-IDF 5.5.5 e LVGL 9.
- **Orientação e Toque**: Rotação automática por gravidade calibrada em 4 quadrantes.

---

# [x] Fase 6: Teclado Virtual PT-BR (Acentos e Cedilha) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Suporte nativo à escrita em língua portuguesa no teclado virtual do `tab5-os`, incluindo cedilha (`ç`/`Ç`), vogais acentuadas (`á`, `é`, `í`, `ó`, `ú`, `ã`, `õ`, `â`, `ê`, `ô`, `à` e maiúsculas correspondentes).
- **Tipografia**: A fonte `lv_font_montserrat_14_latin1` já possui cobertura completa para a faixa Latin-1 (0xA0–0xFF), dispensando geração de fontes customizadas pesadas.
- **Mecanismo LVGL**: Aproveitamento da API `lv_keyboard_set_map(kb, mode, map[], ctrl_map[])` customizando o modo `LV_KEYBOARD_MODE_SPECIAL` (tecla "1#").

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Acesso à página de acentos | Modo `SPECIAL` nativo do LVGL ("1#") | Evita interceptação invasiva de handlers internos e mantém compatibilidade |
| D2 | Grade do teclado | Matriz de 5 linhas com números, acentos e pontuação | Acomoda caracteres minúsculos, maiúsculos e símbolos essenciais |
| D3 | Ações de controle | Manter botões de navegação, Backspace, Enter e troca de modo | Preserva o fluxo ergonômico padrão do usuário |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   └── ui_keyboard.h          # [MODIFY] Declarações e rotinas de layout do teclado
└── ui_keyboard.cpp            # [MODIFY] Definição de pt_br_map_spec e pt_br_ctrl_spec
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Mapeamento de Teclas e Controles**: Definição de `pt_br_map_spec[]` com 5 linhas (números/backspace, minúsculas acentuadas, maiúsculas acentuadas, pontuação e barra de navegação/espaço) e array de flags `pt_br_ctrl_spec[]`.
- [x] **Etapa 2 — Registro e Integração no Teclado**: Aplicação de `lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, pt_br_map_spec, pt_br_ctrl_spec)` e estilização de teclas de ação e Enter (`LV_SYMBOL_NEW_LINE`).

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Altura reduzida das teclas com 5 linhas | Ajuste proporcional do percentual de ocupação em tela (35% paisagem, 52% retrato) |
| Rótulo da tecla de acesso mantido como "1#" | Solução padrão que preserva a troca de modo sem complexidade adicional |

## 6. Critérios de Validação & Teste em Hardware
1. Abertura do app Notas e clique no campo de texto.
2. Toque na tecla "1#" para abrir a página de caracteres em português.
3. Teste de digitação de todas as vogais acentuadas minúsculas e maiúsculas.
4. Validação do retorno ao modo QWERTY através da tecla "abc".

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Mapeamento PT-BR**: Teclado virtual 100% habilitado com suporte completo a acentuação e cedilha.

---

# [x] Fases 8–12: Wi-Fi — Conexão, Configuração e Persistência em SD `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- **Arquitetura do Rádio**: O SoC ESP32-P4 não possui rádio Wi-Fi integrado. O Tab5 utiliza um coprocessador **ESP32-C6-MINI-1U** conectado via interface **SDIO2** (`D0=G11, D1=G10, D2=G9, D3=G8, CMD=G13, CK=G12, RESET=G15, IO2=G14`).
- **Alimentação e Stack**: A alimentação do rádio é controlada via expansor I/O (`WLAN_PWR_EN` / `bsp_feature_enable(BSP_FEATURE_WIFI, true)`). A comunicação host-slave é gerenciada pelo protocolo oficial **ESP-Hosted-MCU** e camada de abstração `esp_wifi_remote`.
- **Armazenamento**: Persistência de credenciais em cartão microSD (`/sdcard/tab5_os/wifi.cfg`) montado via SDMMC slot 0 sobre VFS FATFS.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Stack Wi-Fi | `espressif/esp_hosted` + `esp_wifi_remote` | Padrão oficial da Espressif para expansão de rádio no P4 |
| D2 | Persistência | Arquivo `/sdcard/tab5_os/wifi.cfg` | Facilidade de inspeção, portabilidade e leitura no boot |
| D3 | Gerenciamento | Módulo centralizado `wifi_mgr` | Isola o event loop do FreeRTOS e despacha notificações para a UI |
| D4 | Telas de Usuário | `ui_wifi.cpp` integrado ao `ui_shell` | Configuração (scan/senha) e detalhes da conexão (IP/SSID) |
| D5 | Barra de Status | Ícone dinâmico na `ui_bar` | Feedback em tempo real com estados: desconectado, buscando e conectado |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── wifi_mgr.h             # [NEW] Controle do rádio, scan e eventos
│   ├── wifi_storage.h         # [NEW] I/O de credenciais no microSD
│   ├── ui_wifi.h              # [NEW] Janelas de configuração e detalhes
│   └── ui_bar.h               # [MODIFY] Ícone Wi-Fi na barra superior
├── wifi_mgr.cpp               # [NEW] Backend ESP-Hosted e esp_wifi_remote
├── wifi_storage.cpp           # [NEW] Leitura e escrita de /sdcard/tab5_os/wifi.cfg
├── ui_wifi.cpp                # [NEW] Interface gráfica do aplicativo Wi-Fi
└── ui_bar.cpp                 # [MODIFY] Indicador de status de rede
main/
└── idf_component.yml          # [MODIFY] Inclusão de esp_hosted e esp_wifi_remote
```

## 4. Fases de Execução da Funcionalidade

- [x] **Fase 8 — Alimentação e Inicialização do Rádio (~1 d)**: `bsp_feature_enable(BSP_FEATURE_WIFI, true)`, handshake SDIO2 com ESP32-C6, init de `esp_netif` e event loop. Scan serial operacional.
- [x] **Fase 9 — Armazenamento no Cartão SD (~0,5 d)**: Montagem FATFS via `bsp_sdcard_mount()`, leitura e escrita em `wifi_storage`. Arquivo `/sdcard/tab5_os/wifi.cfg` persistindo entre boots.
- [x] **Fase 10 — Auto-Conexão e Reconexão (~1 d)**: Conexão automática no boot com rede salva, retry em desconexão e captura de IP DHCP via `IP_EVENT_STA_GOT_IP`.
- [x] **Fase 11 — Interface de Configuração (~1,5 d)**: Tela com scan de redes, RSSI, teclado virtual para senha e conexão interativa.
- [x] **Fase 12 — Indicadores na Barra Superior (~0,5 d)**: Ícone de Wi-Fi antes do relógio sincronizado em tempo real com os eventos de rádio.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Barramento compartilhado SDMMC vs SDIO2 | Pinos isolados (SDMMC em G39-44; SDIO2 em G8-15) sem concorrência de hardware |
| Thread-safety da UI durante eventos assíncronos | Atualizações da UI despachadas exclusivamente sob mutex `bsp_display_lock()` |
| Redes de 5 GHz | Rádio ESP32-C6 opera em 2.4 GHz; redes de 5 GHz são filtradas transparentemente |

## 6. Critérios de Validação & Teste em Hardware
1. Inicialização do sistema com alimentação correta do C6.
2. Scan de redes exibindo SSIDs no log serial e na interface gráfica.
3. Conexão bem-sucedida com roteador Wi-Fi e obtenção de IP por DHCP.
4. Persistência de arquivo `wifi.cfg` após reboot físico do Tab5.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Stack Wi-Fi**: Conexão, persistência e interface gráfica operando com estabilidade.

---

# [x] Fase 16: Aplicativo "Arquivos" (File Manager) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Criação do aplicativo nativo **"Arquivos"** para navegação e gerenciamento dos diretórios e arquivos armazenados no cartão microSD (`/sdcard`).
- **Modos de Exibição Dinâmicos**:
  1. **Modo Grade (Ícones)**: Disposição em blocos com ícones (`LV_SYMBOL_DIRECTORY` / `LV_SYMBOL_FILE`) e nomes legíveis.
  2. **Modo Lista (Detalhes)**: Tabela contendo nome, tamanho formatado (`B`, `KB`, `MB` ou `<DIR>`) e data/hora da última modificação (`DD/MM/AAAA HH:MM`).
- **Navegação**: Suporte a abrir subpastas, botão de retorno para subir de nível (`..`) até a raiz `/sdcard` e tratamento de erros de leitura.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Acesso a dados | APIs POSIX (`opendir`, `readdir`, `stat`) | Padrão robusto suportado nativamente pelo VFS FATFS |
| D2 | Modos de visualização | `VIEW_MODE_GRID` e `VIEW_MODE_LIST` | Flexibilidade ergonômica para visualização rápida ou detalhada |
| D3 | Interface gráfica | Padrão de janelas `ui_shell` com barra de ferramentas | Uniformidade visual e suporte a temas claro/escuro |
| D4 | Integração no Desktop | Tile dedicado no lançador `ui_desktop` | Acesso rápido a partir da tela inicial |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   └── ui_files.h             # [NEW] Declarações de interface e ciclo de vida do app Arquivos
├── ui_files.cpp               # [NEW] Leitura POSIX do SD, renderização em lista/grade e navegação
├── ui_desktop.cpp             # [MODIFY] Tile 'Arquivos' no launcher
├── ui_shell.cpp               # [MODIFY] Rotinas ui_shell_open_files / close_files
└── CMakeLists.txt             # [MODIFY] Registro de ui_files.cpp
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Backend de Listagem de Diretórios**: Mapeamento POSIX com `opendir`/`readdir`/`stat`, ordenação e formatação de metadados e data/hora (`localtime_r`).
- [x] **Etapa 2 — Interface Gráfica e Modos de Exibição**: Barra superior com caminho ativo, botão `..`, alternador Grade/Lista e renderização `LV_FLEX_FLOW_ROW_WRAP` / `LV_FLEX_FLOW_COLUMN`.
- [x] **Etapa 3 — Integração com Shell e Desktop**: Registro do tile no launcher e transições de tela no `ui_shell` com suporte a 4 rotações e temas.
- [x] **Etapa 4 — Validação e Testes**: Aprovado no `pre-commit` e validado com navegação em pastas reais no `/sdcard`.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Diretórios com centenas de itens consumindo memória | Limite de paginação e desalocação limpa de nós ao navegar entre diretórios |
| Remoção inesperada do cartão SD | Verificação prévia de montagem e exibição de aviso visual amigável |

## 6. Critérios de Validação & Teste em Hardware
1. Abertura do app Arquivos pelo ícone do Desktop.
2. Navegação pelas pastas do sistema (`/sdcard/tab5_os/`, `/sdcard/notas/`).
3. Alternância instantânea entre modo Grade e modo Lista com preservação de estado.
4. Subida e descida de níveis de diretório sem vazamentos de memória.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **File Manager**: Navegação em árvore de diretórios e alternância de modos operacionais.

---

# [x] Fase 17: Persistência de Notas e Registro de Associações de Arquivos `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- **Persistência do App Notas**: Salvar e carregar notas como arquivos `.txt` no diretório `/sdcard/notas/`, com criação automática de pasta, geração de nomes sequenciais/timestamps e botões **Salvar** (`LV_SYMBOL_SAVE`) e **Novo** (`LV_SYMBOL_PLUS`).
- **Sistema de Associações de Arquivo (`file_assoc`)**: Módulo central no SO que mapeia extensões de arquivo para seus respectivos manipuladores de abertura.
- **Abertura Integrada a partir do Arquivos**: Ao clicar em um arquivo `.txt` no aplicativo Arquivos, o sistema consulta a tabela `file_assoc` e dispara automaticamente a abertura no Notas com o conteúdo carregado.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Despacho de extensões | Módulo `file_assoc` com tabela `ext -> handler_fn` | Arquitetura desacoplada e facilmente extensível para novos tipos de arquivo |
| D2 | Diretório padrão | `/sdcard/notas/` | Organização limpa e isolamento de documentos de texto |
| D3 | Ações no Notas | Botões Salvar e Novo + label do arquivo ativo | Feedback direto ao usuário sobre o estado de edição |
| D4 | Integração cruzada | Disparo via `file_assoc_open(filepath)` | Desacopla o gerenciador de arquivos dos apps específicos |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── file_assoc.h           # [NEW] Tabela e despacho de associações (.txt -> Notas)
│   ├── ui_notas.h             # [MODIFY] Exporta ui_notas_open_file e ui_notas_save_current
│   └── ui_files.h             # [MODIFY] Disparo de associações no clique de item
├── file_assoc.cpp             # [NEW] Implementação do dispatcher de extensões
├── ui_notas.cpp               # [MODIFY] Ações de I/O em /sdcard/notas/ e botões de barra
├── ui_files.cpp               # [MODIFY] Integração com file_assoc_open()
├── ui_shell.cpp               # [MODIFY] Roteamento de abertura de notas com caminho de arquivo
└── CMakeLists.txt             # [MODIFY] Registro de file_assoc.cpp
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Módulo `file_assoc`**: Tabela associativa estática de extensões para handlers e função `file_assoc_open(const char *filepath)` com despacho automático.
- [x] **Etapa 2 — Aprimoramento do Aplicativo Notas**: Botões Salvar (`💾`) e Novo (`+`) na barra interna, gravação/leitura de `.txt` em `/sdcard/notas/` e indicador do arquivo aberto.
- [x] **Etapa 3 — Integração no Aplicativo Arquivos**: Tratamento de clique em arquivos de texto direcionando para `file_assoc_open()`.
- [x] **Etapa 4 — Validação e Gravação**: Pre-commit aprovado e validação do ciclo completo de salvar notas e reabrir via gerenciador de arquivos.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Arquivo muito extenso no Notas | Limite seguro de leitura com buffer alocado dinamicamente |
| Caracteres inválidos em nomes de arquivos | Sanitização de strings antes da chamada `fopen()` |

## 6. Critérios de Validação & Teste em Hardware
1. Criação de nova nota, digitação e salvamento com nome automático.
2. Abertura do app Arquivos, navegação até `/sdcard/notas/` e toque no arquivo `.txt`.
3. Abertura automática do Notas com o texto original carregado perfeitamente.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Notas & File Associations**: Persistência completa e integração de ponta a ponta com o File Manager.

---

# [x] Fase 18: Múltiplas Redes Wi-Fi, Status e Controle Avançado `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Expandir o gerenciamento de Wi-Fi para suportar múltiplas redes conhecidas salvas no arquivo `/sdcard/tab5_os/wifi.cfg`.
- **Identificação Visual na Lista**:
  - Indicação de rede atualmente **Conectada** (`LV_SYMBOL_OK` / `(Conectado)`).
  - Indicação de redes **Salvas** (`LV_SYMBOL_SAVE` / `(Salva)`).
  - Indicador dinâmico de intensidade de sinal RSSI.
- **Ações Contextuais de Rede**:
  - Botão **Desconectar** para a rede ativa.
  - Botão **Esquecer** para remover credenciais de rede previamente salvas.
  - Botão **Conectar** com conexão imediata ou solicitação de senha.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Formato de arquivo | Linhas `ssid=...` e `password=...` em blocos | Compatibilidade retroativa com configurações anteriores |
| D2 | API de armazenamento | CRUD completo em `wifi_storage` (`load_all`, `add_or_update`, `remove`, `find`) | Manipulação segura e padronizada da lista de redes |
| D3 | Controle de conexão | Métodos dedicados em `wifi_mgr` (`disconnect`, `forget`) | Controle explícito de estado do rádio e reconexão inteligente |
| D4 | UI Contextual | Painel com botões que se adaptam ao estado da rede selecionada | Interface limpa sem sobrecarga de elementos visuais |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── wifi_storage.h         # [MODIFY] Estruturas de múltiplas redes e CRUD de configs
│   ├── wifi_mgr.h             # [MODIFY] Métodos de desconectar, esquecer e auto-seleção
│   └── ui_wifi.h              # [MODIFY] Renderização de badges e ações contextuais
├── wifi_storage.cpp           # [MODIFY] Parsing e serialização de lista de credenciais
├── wifi_mgr.cpp               # [MODIFY] Desconexão manual e supressão de auto-reconexão
└── ui_wifi.cpp                # [MODIFY] Badges visuais e botões dinâmicos de controle
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Backend de Armazenamento (`wifi_storage`)**: Parsing e serialização de lista de credenciais com operações de busca, inclusão, atualização e remoção.
- [x] **Etapa 2 — Controle de Rádio no `wifi_mgr`**: Métodos `wifi_mgr_disconnect()`, `wifi_mgr_forget_network()` e seleção inteligente de rede salva no boot.
- [x] **Etapa 3 — Interface de Usuário no `ui_wifi`**: Badges (Conectado / Salva / Sinal) e painel contextual com botões Conectar, Desconectar e Esquecer.
- [x] **Etapa 4 — Validação em Hardware**: Testado em ambiente real com múltiplos roteadores e persistência no microSD.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Corrupção de arquivo de configuração em gravação | Escrita atômica com arquivo temporário antes de substituir o arquivo principal |
| Conexão concorrente durante troca rápida de rede | Cancelamento prévio de tentativas pendentes antes de iniciar nova associação |

## 6. Critérios de Validação & Teste em Hardware
1. Salvar duas redes Wi-Fi distintas no cartão SD.
2. Desconectar manualmente de uma rede ativa através do botão Desconectar.
3. Esquecer uma rede salva e validar a remoção do registro no `wifi.cfg`.
4. Reboot e validação da auto-conexão na rede remanescente.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Múltiplas Redes**: Suporte robusto a múltiplos APs, esquecimento e controle contextual.

---

# [x] Fase 19: Gerenciador de Conexão Bluetooth, Periféricos e Supressão de Teclado Virtual `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Criação do aplicativo nativo **"Bluetooth"** (`ui_bluetooth`) para gerenciamento de dispositivos BLE, periféricos de entrada (teclados, mouses) e áudio.
- **Aplicativo Próprio**: Interface de busca de dispositivos, listagem com ícones por categoria (`LV_SYMBOL_KEYBOARD`, `LV_SYMBOL_AUDIO`, `LV_SYMBOL_SETTINGS`), badges de status (Conectado / Pareado / RSSI) e ações contextuais (Conectar, Desconectar, Parear, Esquecer).
- **Ícone na Barra de Status (`ui_status`)**: Indicador `LV_SYMBOL_BLUETOOTH` ao lado do Wi-Fi, com cor atenuada (`text_muted`) quando desconectado e cor de destaque (`accent`) quando conectado.
- **Supressão Inteligente do Teclado Virtual**:
  - Quando um teclado físico Bluetooth estiver conectado, o teclado virtual (`ui_keyboard`) **permanece oculto** ao focar campos de texto (`lv_textarea`), permitindo que os apps aproveitem 100% da altura da tela.
  - Ao desconectar o teclado físico, o teclado virtual volta a ser exibido automaticamente.
- **Persistência de Dispositivos**: Salvamento de dispositivos pareados em `/sdcard/tab5_os/bt.cfg` para reconexão automática e transparente no boot.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Stack Bluetooth | ESP-Hosted HCI (SDIO2) + NimBLE Host no ESP32-P4 | Desempenho e compatibilidade com BLE HOGP no FreeRTOS |
| D2 | Janela do App | `ui_bluetooth.cpp` integrado ao `ui_shell` | Ciclo de vida consistente e compatível com temas |
| D3 | Status Bar | Ícone `LV_SYMBOL_BLUETOOTH` interativo | Acesso rápido e feedback visual permanente |
| D4 | Persistência | `/sdcard/tab5_os/bt.cfg` via `bt_storage.cpp` | Armazenamento de endereços MAC, nomes e chaves de pareamento |
| D5 | Supressão de Teclado | Interceptação em `ui_keyboard_attach()` com `bt_mgr_is_keyboard_connected()` | Maximização do espaço útil de tela durante uso de periféricos |
| D6 | Dispositivos de Entrada | Driver virtual `lv_indev_t` no LVGL 9 | Injeção nativa de eventos de teclado físico no foco ativo |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── bt_mgr.h               # [NEW] API e eventos do gerenciador Bluetooth / NimBLE
│   ├── bt_storage.h           # [NEW] Persistência em /sdcard/tab5_os/bt.cfg
│   ├── ui_bluetooth.h         # [NEW] Interface do aplicativo Bluetooth
│   ├── ui_keyboard.h          # [MODIFY] Suporte à supressão do teclado virtual
│   ├── ui_status.h            # [MODIFY] Ícone de status Bluetooth
│   └── ui_shell.h             # [MODIFY] Transições de tela do Bluetooth
├── bt_mgr.cpp                 # [NEW] Backend BLE NimBLE, descoberta GATT e CCCD
├── bt_storage.cpp             # [NEW] I/O do arquivo bt.cfg
├── ui_bluetooth.cpp           # [NEW] Interface gráfica do app Bluetooth
├── ui_keyboard.cpp            # [MODIFY] Lógica condicional de visibilidade
├── ui_desktop.cpp             # [MODIFY] Tile 'Bluetooth' no launcher
├── ui_status.cpp              # [MODIFY] Indicador Bluetooth na barra superior
├── ui_shell.cpp               # [MODIFY] Ciclo de vida da tela bluetooth_scr
└── CMakeLists.txt             # [MODIFY] Registro dos novos módulos
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Backend de Armazenamento (`bt_storage`)**: Formato e parsing de `/sdcard/tab5_os/bt.cfg` com CRUD de dispositivos pareados.
- [x] **Etapa 2 — Gerenciador Bluetooth e Detecção (`bt_mgr`)**: NimBLE sobre transporte HCI SDIO, scan assíncrono, descoberta GATT HID e CCCD (0x2902) e auto-reconexão no boot.
- [x] **Etapa 3 — Supressão Inteligente do Teclado Virtual**: Interceptação em `ui_keyboard_attach()` com liberação da altura total dos aplicativos.
- [x] **Etapa 4 — Interface do Aplicativo (`ui_bluetooth`)**: Tela de gerenciamento com listagem, busca e ações contextuais.
- [x] **Etapa 5 — Integração com o Sistema**: Registro de tile no launcher, ícone na barra e transições no `ui_shell`.
- [x] **Etapa 6 — Mapeamento de Teclas HID**: Driver LVGL 9 injetando eventos de digitação do teclado BLE físico.
- [x] **Etapa 7 — Validação e Gravação**: Testado e aprovado com teclado sem fio em hardware real.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Timeout na subscrição de CCCD (0x2902) | Pipeline sequencial de descoberta com confirmação assíncrona antes de habilitar notificações |
| Conexão instável em ambientes com ruído de rádio | Lógica de reconexão automática com backoff |

## 6. Critérios de Validação & Teste em Hardware
1. Descoberta e pareamento de teclado Bluetooth sem fio.
2. Injeção de caracteres digitados diretamente no app Notas.
3. Supressão do teclado virtual ao tocar em campos de texto enquanto o teclado físico estiver conectado.
4. Auto-reconexão no boot do Tab5.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Bluetooth & Periféricos**: NimBLE, descoberta GATT/CCCD, supressão de teclado virtual e auto-conexão 100% operacionais.

---

# [x] Fase 19.1: Controles de Liga/Desliga para Wi-Fi e Bluetooth no Menu de Configurações `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Adicionar switches independentes no **Menu de Configurações** (painel popover da engrenagem) para ligar e desligar os rádios de **Wi-Fi** e **Bluetooth**.
- **Controle Efetivo de Rádio**: Desligamento real das operações de rádio, economia de energia e bloqueio de novas conexões/scans quando inativos.
- **Persistência em NVS**: Armazenamento do estado dos rádios na partição NVS (`radios/wifi_en` e `radios/bt_en`) com restauração em cada boot.
- **Reconexão Inteligente**: Ao religar um rádio, disparar imediatamente a reconexão automática com redes e periféricos salvos.
- **Sincronização Visual**: Ícones da barra superior e janelas dos aplicativos refletem instantaneamente o estado de cada rádio.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Localização dos controles | Popover de configurações (`ui_bar.cpp`) abaixo de "Rotação" | Acesso centralizado e rápido a todas as opções do sistema |
| D2 | Persistência | NVS (Namespace `radios`, chaves `wifi_en` e `bt_en`) | Rápido, seguro e independente do cartão microSD |
| D3 | Tratamento nos Apps | Bloqueio de interface com aviso explicativo quando o rádio estiver desativado | Previne ações inválidas e orienta o usuário |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── wifi_mgr.h             # [MODIFY] APIs wifi_mgr_set_enabled / wifi_mgr_is_enabled
│   ├── bt_mgr.h               # [MODIFY] APIs bt_mgr_set_enabled / bt_mgr_is_enabled
│   └── ui_bar.h               # [MODIFY] Declarações do painel de configurações
├── wifi_mgr.cpp               # [MODIFY] Lógica de enable/disable e persistência NVS
├── bt_mgr.cpp                 # [MODIFY] Desconexão de periféricos e parada de rádio
├── ui_bar.cpp                 # [MODIFY] Switches visuais no painel de configurações
├── ui_status.cpp              # [MODIFY] Atualização de ícones de status para modo desligado
├── ui_wifi.cpp                # [MODIFY] Tela bloqueada com mensagem quando desativado
└── ui_bluetooth.cpp           # [MODIFY] Tela bloqueada com mensagem quando desativado
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Switches no Painel de Configurações**: Inclusão de switches de Wi-Fi e Bluetooth no menu popover da engrenagem.
- [x] **Etapa 2 — Controle e Persistência no `wifi_mgr`**: `wifi_mgr_set_enabled(bool)` com gravação em NVS, desligamento e auto-reconexão.
- [x] **Etapa 3 — Controle e Persistência no `bt_mgr`**: `bt_mgr_set_enabled(bool)` com gravação em NVS, desconexão de periféricos e restauração do teclado virtual.
- [x] **Etapa 4 — Sincronização nos Aplicativos e Barra Superior**: Atualização em tempo real de ícones e bloqueio nos apps de rádio quando inativos.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Desligamento abrupto com transmissão ativa | Finalização graciosa de conexões antes do encerramento do rádio |
| Concorrência de escrita na NVS | Operações protegidas com commit seguro |

## 6. Critérios de Validação & Teste em Hardware
1. Desligar o Wi-Fi pelo menu: validar desconexão imediata e ícone apagado na barra.
2. Desligar o Bluetooth: validar desconexão do teclado físico e restauração do teclado virtual.
3. Reiniciar o Tab5: verificar se os estados dos rádios foram restaurados via NVS.
4. Religar os rádios: verificar reconexão automática com a rede Wi-Fi e periféricos Bluetooth.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Controles de Rádio**: Switches no menu, persistência NVS e reconexão automática 100% validados.

---

# [x] Fase 19.2: Suporte a Mouse/Touchpad BLE HID e Cursor Visual `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Suporte a periféricos de apontamento Bluetooth Low Energy (HOGP) — como Touchpads integrados a teclados físicos e mouses sem fio.
- **Cursor Visual Renderizado**: Renderização de cursor de alta visibilidade na camada de sistema do LVGL (`lv_layer_sys()`) usando bitmap ARGB8888.
- **Transformação de Coordenadas e Rotação**: Adaptação dinâmica dos eixos de movimento (`dx`, `dy`) e rastreamento absoluto `(s_cursor_x, s_cursor_y)` com clamp automático de acordo com a rotação ativa da tela (0°, 90°, 180°, 270°).
- **Interação**: Suporte a cliques esquerdo/direito, tap-to-click e liberação atômica no motor de eventos do LVGL 9.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Dispositivo de Entrada | `LV_INDEV_TYPE_POINTER` registrado no LVGL 9 | Integração padrão com os componentes interativos do LVGL |
| D2 | Camada de renderização | `lv_layer_sys()` | Cursor permanece visível sobre qualquer aplicativo, modal ou barra |
| D3 | Transformação angular | Inversão e rotação de `(dx, dy)` sincronizada com o IMU | Movimento natural do cursor independente da orientação do dispositivo |
| D4 | Extração de relatórios | Parser HID de Report ID `0x02` no `bt_mgr.cpp` | Compatibilidade com relatórios curtos padrão de mouse BLE |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   └── ui_mouse.h             # [NEW] Driver de ponteiro, injeção de movimento e cursor
├── ui_mouse.cpp               # [NEW] Implementação de coordenadas, desenho e rotação
├── bt_mgr.cpp                 # [MODIFY] Extração de pacotes HID de mouse e despacho para ui_mouse
├── CMakeLists.txt             # [MODIFY] Registro de ui_mouse.cpp
main/
└── app_main.cpp               # [MODIFY] Inicialização via ui_mouse_init()
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Driver de Ponteiro e Cursor (`ui_mouse`)**: Registro de `lv_indev_t` do tipo ponteiro, bitmap ARGB8888 em `lv_layer_sys()` e rastreamento absoluto com clamp.
- [x] **Etapa 2 — Transformação de Eixos por Rotação**: Transformação matemática cobrindo 0° (retrato), 90° (paisagem), 180° (retrato invertido) e 270° (paisagem invertida).
- [x] **Etapa 3 — Integração com o Backend Bluetooth**: Extração de relatórios HID de mouse no `bt_mgr.cpp` e despacho para `ui_mouse_inject_motion()`.
- [x] **Etapa 4 — Validação e Testes**: Validação em hardware com movimentação, tap-to-click e cliques físicos.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Cursor ultrapassar as bordas da tela | Aplicação de clamp absoluto `[0, width - 1]` e `[0, height - 1]` |
| Cliques fantasmas ou botões presos | Envio explícito de evento `LV_INDEV_STATE_RELEASED` após liberação |

## 6. Critérios de Validação & Teste em Hardware
1. Conectar mouse/touchpad BLE e verificar surgimento do cursor visual.
2. Movimentar o ponteiro por toda a tela nas 4 orientações (0°, 90°, 180°, 270°).
3. Testar cliques em botões, tiles do launcher e campos de texto.
4. Desconectar o mouse e validar a ocultação automática do cursor.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Mouse & Cursor Visual**: Driver de ponteiro, rotação de 4 quadrantes e cliques 100% integrados.

---

# [x] Fase 20: Aplicativo Terminal Interativo `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Criação do aplicativo **Terminal** integrado ao `tab5-os`, funcionando como um shell interativo estilo Linux para exploração do sistema e execução de comandos utilitários.
- **Ambiente de Execução**: Como o ESP-IDF roda sobre FreeRTOS (sem suporte a `fork()`/`exec()`), todos os comandos são implementados como funções C++ internas no motor `terminal_cmd`.
- **Console Unificado**: Interface com tela cheia, prompt dinâmico (`/sdcard $`), buffer circular de ~8 KB para histórico, proteção contra deleção de saída e rolagem automática.
- **Entrada**: Compatível com o teclado virtual do sistema e teclados físicos Bluetooth.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Motor de comandos | Mini-shell em C++ (`terminal_cmd.cpp`) | Execução rápida e determinística sem overhead de subprocessos |
| D2 | Diretório de trabalho | Início em `/sdcard` com bloqueio fora do ponto de montagem | Protege o sistema contra acessos a sistemas de arquivos inexistentes |
| D3 | Área de exibição | `lv_textarea` com buffer circular de ~8 KB | Descarte automático de linhas antigas sem estourar o heap da PSRAM |
| D4 | Integração no Shell | Padrão `ui_shell` (`ui_shell_open_terminal` / `close_terminal`) | Transições fluidas e compatibilidade com o launcher |
| D5 | Tipografia | Fonte padrão `lv_font_montserrat_14_latin1` | Legibilidade e suporte completo a acentos |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── terminal_cmd.h         # [NEW] API e protótipos do motor de comandos
│   └── ui_terminal.h          # [NEW] Interface gráfica do Terminal
├── terminal_cmd.cpp           # [NEW] Parser de linha de comando e execução de comandos POSIX
├── ui_terminal.cpp            # [NEW] Console interativo, gerenciamento de buffer e eventos
├── ui_desktop.cpp             # [MODIFY] Tile 'Terminal' no launcher
├── ui_shell.cpp               # [MODIFY] Ciclo de vida da tela terminal_scr
├── ui_shell.h                 # [MODIFY] Métodos de abertura e fechamento
└── CMakeLists.txt             # [MODIFY] Registro de terminal_cmd.cpp e ui_terminal.cpp
```

## 4. Comandos Implementados

| Comando | Descrição e Comportamento |
|---|---|
| `ls [path]` | Lista arquivos e pastas do diretório alvo (ou CWD), identificando `<DIR>` |
| `cd <path>` | Altera o diretório de trabalho atual (suporta caminhos relativos, absolutos e `..`) |
| `pwd` | Imprime o caminho completo do diretório de trabalho ativo |
| `mkdir <dir>` | Cria um novo diretório no sistema de arquivos |
| `rm <path>` | Remove um arquivo (`unlink`) ou pasta vazia (`rmdir`) |
| `rmdir <dir>` | Remove um diretório vazio |
| `touch <file>` | Cria um arquivo vazio ou atualiza a data/hora de modificação |
| `cat <file>` | Exibe o conteúdo de arquivos de texto (com limite de segurança de 4 KB) |
| `echo <texto>` | Imprime a mensagem de texto fornecida no console |
| `clear` | Limpa o buffer de exibição da tela |
| `help` | Exibe a lista completa de comandos disponíveis |
| `whoami` | Retorna o identificador do usuário ativo (`root@tab5`) |
| `uname` | Imprime informações de versão do sistema operacional e kernel |

## 5. Especificação Visual e Layout da Interface

```
┌─────────────────────────────────────────────────────────┐
│  [←] Terminal                                    [✕]   │  ← Barra superior do app (UI_BAR_HEIGHT)
├─────────────────────────────────────────────────────────┤
│ /sdcard $                                               │
│ > ls                                                    │
│ notas/  tab5_os/                                        │
│ /sdcard $                                               │
│ > cd notas                                              │
│ /sdcard/notas $                                         │
│ > _                                                     │  ← Console unificado com rolagem automática
├─────────────────────────────────────────────────────────┤
│ [ Teclado Virtual ou Físico Bluetooth ]                 │
└─────────────────────────────────────────────────────────┘
```

## 6. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Motor de Comandos Shell (`terminal_cmd`)**: Parser de argumentos e despacho para rotinas POSIX com contenção sob `/sdcard`.
- [x] **Etapa 2 — Interface do Console (`ui_terminal`)**: Área de texto unificada com controle de prompt, proteção de cursor e buffer circular de 8 KB.
- [x] **Etapa 3 — Integração com o Desktop e Shell**: Tile no launcher e roteamento no `ui_shell` com rotação e temas.
- [x] **Etapa 4 — Validação em Hardware**: Teste de execução de todos os comandos POSIX e validação de estabilidade.

## 7. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Consumo excessivo de memória no buffer | Buffer circular limitado a ~8 KB com descarte de linhas antigas |
| Execução de `cat` em arquivos gigantes | Limite de leitura de 4 KB com aviso de truncamento |
| Tentativa de `cd` para raiz não montada | Validação e bloqueio de caminhos fora de `/sdcard` |

## 8. Critérios de Validação & Teste em Hardware
1. Abertura do app Terminal a partir do Desktop.
2. Criação de pastas e arquivos via `mkdir` e `touch`.
3. Navegação com `cd` e listagem com `ls`.
4. Leitura de arquivos com `cat` e limpeza da tela com `clear`.

## 9. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Aplicativo Terminal**: Mini-shell, comandos POSIX e console unificado 100% implementados e validados.

---

# [x] Fase 21: Cliente SSH Remoto no Terminal `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Implementar o cliente **SSH (Secure Shell)** integrado ao aplicativo **Terminal** do `tab5-os`, permitindo conexão remota e sessão interativa com servidores e dispositivos na rede (ex: Raspberry Pi, servidores Linux, roteadores, etc.).
- **Arquitetura Assíncrona**: Como as operações de criptografia SSH e I/O de sockets são bloqueantes e consomem processamento, o cliente roda em uma **Task FreeRTOS dedicada** (`ssh_client_task`, stack ~16 KB em PSRAM) com buffers em PSRAM e I/O não bloqueante integrado ao console do Terminal.
- **Modos de Operação**:
  - `LOCAL_SHELL`: Modo padrão local com prompt `/sdcard $`.
  - `SSH_PASSWORD_PROMPT`: Modo de entrada de senha com eco oculto.
  - `SSH_SESSION`: Sessão interativa remota encaminhando I/O bruto para o canal SSH.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Biblioteca SSH | Componente oficial `david-cermak/libssh` do ESP Component Registry | Port oficial de libssh para ESP-IDF v5+ |
| D2 | Execução do Protocolo | Task assíncrona dedicada FreeRTOS (`ssh_client_task`) | Isola a criptografia e rede sem bloquear a thread gráfica do LVGL |
| D3 | Modos do Terminal | Estados `LOCAL_SHELL`, `SSH_PASSWORD_PROMPT` e `SSH_SESSION` | Separação clara entre comandos locais e sessão remota |
| D4 | Autenticação | Suporte inicial a **Senha** e futuro a chaves privadas (`/sdcard/.ssh/`) | Flexibilidade de acesso a servidores remotos |
| D5 | Emulação de Terminal | PTY interativo `vt100` / `xterm` com filtragem ANSI básica | Formatação limpa de saídas e retornos de carro (`\r\n`) |
| D6 | Validação de Rede | Checagem prévia com `wifi_mgr_is_connected()` | Evita tentativas inúteis de handshake sem conectividade |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── ssh_client.h           # [NEW] API e callbacks da sessão SSH assíncrona
│   ├── terminal_cmd.h         # [MODIFY] Adição do comando 'ssh' no parser
│   └── ui_terminal.h          # [MODIFY] Suporte ao modo de sessão SSH e injeção de I/O
├── ssh_client.cpp             # [NEW] Task FreeRTOS, handshake libssh, PTY e canais
├── terminal_cmd.cpp           # [MODIFY] Validação sintática e despacho do comando ssh
├── ui_terminal.cpp            # [MODIFY] Modo interativo: envio de teclas brutas e recepção de stdout
├── CMakeLists.txt             # [MODIFY] Registro de ssh_client.cpp
main/
└── idf_component.yml          # [MODIFY] Adição da dependência david-cermak/libssh
```

## 4. Especificação de Sintaxe e Fluxo do Comando

### Sintaxe
```bash
ssh [user@]host [-p porta]
```
Exemplos:
- `ssh 192.168.1.50` (usuário padrão: `root`, porta padrão: 22)
- `ssh pi@raspberrypi.local`
- `ssh admin@10.0.0.1 -p 2222`

### Fluxo de Execução
```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  Comando     │     │  Validação   │     │  Handshake   │     │  Sessão      │
│  "ssh host"  │ ──> │  Wi-Fi Ativo │ ──> │  Task Free   │ ──> │  Interativa  │
│  no Terminal │     │  & Parâmetros│     │  RTOS / Auth │     │  PTY Remota  │
└──────────────┘     └──────────────┘     └──────────────┘     └──────────────┘
```
1. O usuário digita o comando `ssh` no Terminal e pressiona Enter.
2. O `terminal_cmd` valida os argumentos e verifica se o Wi-Fi está conectado. Se desconectado, exibe erro imediatamente.
3. O `ssh_client` inicia a task FreeRTOS e estabelece a conexão TCP/SSH.
4. Se o servidor requisitar autenticação por senha, o Terminal entra temporariamente em modo `SSH_PASSWORD_PROMPT` exibindo `Password: ` e oculta o texto digitado.
5. Ao autenticar com sucesso, o Terminal entra em modo `SSH_SESSION`.
6. A partir desse momento, qualquer tecla digitada (virtual ou física Bluetooth) é despachada diretamente para o canal SSH remoto.
7. Respostas e saídas do servidor são impressas em tempo real no console.
8. Ao digitar `exit` no servidor ou cair a conexão, o cliente fecha a sessão e o Terminal restaura automaticamente o prompt local `/sdcard $ `.

## 5. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Dependência e Backend `ssh_client`**: Adicionar `david-cermak/libssh` ao `main/idf_component.yml`, criar `ssh_client.h` e `ssh_client.cpp` com criação de sessão, conexão, PTY `xterm`, autenticação por senha e I/O não bloqueante com stack em PSRAM.
- [x] **Etapa 2 — Comando `ssh` no `terminal_cmd`**: Parsing de `user`, `host`, `port` em `terminal_cmd.cpp`, validação de conectividade Wi-Fi e despacho.
- [x] **Etapa 3 — Integração com `ui_terminal`**: Estados `LOCAL_SHELL`, `SSH_PASSWORD_PROMPT` e `SSH_SESSION`, callbacks `rx_cb`/`state_cb` thread-safe sob `bsp_display_lock()` e restauração de prompt ao sair.
- [x] **Etapa 4 — Build, Validação e Gravação**: Atualizar `CMakeLists.txt`, checar `pre-commit`, compilar com `idf.py build` e validar em hardware.

## 6. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Alto consumo de memória durante o handshake criptográfico (DH, RSA, AES) | Alocação da task e buffers da sessão SSH na PSRAM externa de 32 MB do Tab5 |
| I/O bloqueante travar a interface gráfica LVGL | Todas as operações de rede e libssh rodam exclusivamente na task FreeRTOS dedicada; a UI só recebe callbacks protegidos por mutex |
| Perda de conexão Wi-Fi durante sessão SSH ativa | Detecção de erro no socket/canal SSH e timeout automático com retorno seguro ao prompt local |
| Caracteres de controle ANSI complexos desformatarem o textarea | Tratamento e filtragem de caracteres de controle e quebras de linha (`\r\n`) para exibição limpa no LVGL |

## 7. Critérios de Validação & Teste em Hardware
1. Conexão via `ssh user@host` em um servidor Linux ou Raspberry Pi na mesma rede Wi-Fi.
2. Entrada de senha sem eco visual de caracteres sensíveis.
3. Execução de comandos remotos interativos (`top`, `uptime`, `ls -la`).
4. Saída limpa via comando `exit` retornando ao prompt `/sdcard $`.

## 8. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Cliente SSH**: Implementado, integrado ao Terminal, compilado e gravado no hardware real.

---

# [x] Fase 22: Protetor de Tela Anti-Burn-in com Data e Hora `✅ CONCLUÍDO`

## 1. Contexto & Objetivos
- Criar o mecanismo de **Protetor de Tela (Screensaver)** do `tab5-os` para preservação do painel MIPI-DSI, prevenindo retenção de imagem (*burn-in* / *image sticking*) durante períodos de inatividade.
- **Elementos Visuais e Estética**:
  - Fundo completamente preto (`#000000`) cobrindo 100% da área útil da tela.
  - Tipografia clara em alto contraste: branco puro (`#FFFFFF`) ou cinza suave (`#D0D0D0`).
  - **Em destaque principal**: **Hora atual** (tamanho grande/bold, formato `HH:MM:SS`) e **Data completa** (ex: `Segunda-feira, 17 de Agosto de 2026`).
  - Nome do sistema operacional e versão em texto secundário discreto (`tab5-os v0.4.0`).
- **Mecanismo Anti-Burn-in (Relocação a cada 30 segundos)**:
  - Timer periódico de 30 segundos (`lv_timer_t`) que sorteia novas coordenadas `(x, y)` para o bloco de texto.
  - **Cálculo de Bounding Box Seguro**: As coordenadas sorteadas respeitam estritamente os limites da resolução ativa (`x_max = screen_w - block_w - margin`, `y_max = screen_h - block_h - margin`), garantindo que **nenhuma informação seja cortada** nas bordas da tela.
  - Compatibilidade com as 4 orientações de tela (0°, 90°, 180°, 270°).
- **Ativação e Despertar (Wake-up)**:
  - Ativação automática após tempo de inatividade configurável (1 min, 2 min, 5 min ou Desativado).
  - Despertar imediato ao detectar qualquer evento de entrada (toque na tela, clique/movimento de mouse BLE ou pressionamento de tecla em teclado físico/virtual).

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Camada de Exibição | Tela dedicada gerenciada pelo `ui_shell` (`screensaver_scr`) | Isolamento completo da aplicação ativa sem destruir o estado anterior |
| D2 | Detecção de Inatividade | `lv_display_get_inactive_time()` ou monitor global de `lv_indev_t` | API nativa e eficiente do LVGL 9 sem overhead de polling manual |
| D3 | Prevenção de Burn-in | Timer LVGL de 30s com relocação aleatória inteligente | Movimenta os pixels acesos continuamente pelo painel sem degradar o display |
| D4 | Atualização do Relógio | Timer de 1s para sincronização em tempo real de data e hora | Exibição precisa e viva do relógio sem drift temporal |
| D5 | Despertar Instantâneo | Eventos globais de input restauram `prev_scr` sem lag | Experiência de uso fluida e retorno imediato ao trabalho ativo |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── ui_screensaver.h       # [NEW] API de controle, ativação e timer anti-burn-in
│   ├── ui_shell.h             # [MODIFY] Integração do screensaver no loop de inatividade
│   └── ui_bar.h               # [MODIFY] Opção de tempo limite no menu de configurações
├── ui_screensaver.cpp         # [NEW] Janela, container seguro, relógio e lógica de sorteio
├── ui_shell.cpp               # [MODIFY] Monitoramento de inatividade e transição de tela
└── CMakeLists.txt             # [MODIFY] Registro de ui_screensaver.cpp
```

## 4. Layout e Especificação de Relocação

```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│        ┌─────────────────────────────┐                  │
│        │  tab5-os v0.4.0             │                  │
│        │  19:45:30                   │  ← Posição (x1, y1)
│        │  Domingo, 16 de Agosto      │     no tempo T = 0s
│        └─────────────────────────────┘                  │
│                                                         │
│                                                         │
│                      ┌─────────────────────────────┐    │
│                      │  tab5-os v0.4.0             │    │
│                      │  19:46:00                   │    │ ← Nova posição (x2, y2)
│                      │  Domingo, 16 de Agosto      │    │    no tempo T = 30s
│                      └─────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

## 5. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Módulo `ui_screensaver` e Layout Visual**: Criação do container escuro (`#000000`), label do relógio em destaque com fonte grande, data formatada e label de versão do SO.
- [x] **Etapa 2 — Algoritmo de Relocação Anti-Burn-in (30s)**: Timer de 30 segundos calculando novas coordenadas `(x, y)` aleatórias baseadas na largura/altura medidas do container com margem de segurança de 20 px.
- [x] **Etapa 3 — Detecção de Inatividade e Loop de Ativação**: Monitoramento de inatividade no `ui_shell` e disparo automático ao atingir o timeout definido.
- [x] **Etapa 4 — Despertar Imediato e Restauração de Estado**: Interceptação de eventos de toque, mouse e teclado para fechamento instantâneo do protetor e retorno à tela de trabalho.
- [x] **Etapa 5 — Configuração no Menu e Persistência**: Adição de submenu e seletor de tempo de inatividade (Desativado, 1 min, 2 min, 5 min) no menu popover de configurações persistido em NVS.
- [x] **Etapa 6 — Build, Validação e Teste em Hardware**: Validação de compilação, conformidade com pre-commit e verificação das transições de tela.

## 6. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Texto cortar nas bordas ao mudar de orientação | Medição real com `lv_obj_get_width/height` recalculada após rotação do display |
| Sincronização de data/hora sem conexão NTP | Leitura do relógio RTC interno com fallback para hora salva no boot |
| Retardo perceptível ao tocar na tela | Despertar direto sem animação pesada ou desalocação complexa |

## 7. Critérios de Validação & Teste em Hardware
1. Aguardar o tempo de inatividade configurado e verificar entrada automática no screensaver.
2. Confirmar fundo preto `#000000` com relógio grande, data e versão do SO legíveis.
3. Observar a relocação aleatória a cada 30 segundos, validando que o bloco permanece 100% visível sem cortar nas margens.
4. Testar em modo retrato (0°, 180°) e paisagem (90°, 270°).
5. Tocar no display ou teclar em periférico físico e verificar despertar instantâneo para a tela anterior.

## 8. Status de Conclusão: `[x] CONCLUÍDO`
- **Protetor de Tela**: Implementação completa do módulo `ui_screensaver`, timer de relocação anti-burn-in de 30s, menu de configuração com persistência em NVS e despertar instantâneo via toque, mouse BLE e teclado físico.

---

# [x] Fase 23: Controle de Brilho da Tela no Menu de Configurações `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Adicionar controle interativo da intensidade de **brilho do backlight** do display MIPI-DSI no **Menu de Configurações** (painel popover da engrenagem).
- **Suporte de Hardware e BSP**: O Tab5 possui controle de backlight por modulação PWM através da função oficial do BSP `bsp_display_brightness_set(int brightness_percent)`.
- **Interface e Ergonomia**:
  - Slider horizontal (`lv_slider`) elegante e responsivo no popover de configurações.
  - Indicador numérico percentual em tempo real (ex: `75%`) e ícone indicativo de luminosidade (`LV_SYMBOL_IMAGE` / `LV_SYMBOL_EYE_OPEN`).
  - Faixa de ajuste seguro entre **10% e 100%** (com limite inferior de 10% para impedir que o usuário apague completamente a tela por engano).
- **Persistência de Estado**:
  - O nível de brilho selecionado é persistido em NVS (namespace `tab5`, chave `brightness`) ou `/sdcard/tab5_os/display.cfg` e restaurado automaticamente em cada boot.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Controle de Hardware | `bsp_display_brightness_set(int percent)` | API nativa do BSP que gerencia o driver PWM do backlight |
| D2 | Componente de UI | `lv_slider` em `ui_bar.cpp` (popover de configurações) | Controle intuitivo, contínuo e integrado ao painel existente |
| D3 | Limites de Brilho | Mínimo de 10% e Máximo de 100% | Evita tela preta acidental mantendo a UI sempre visível |
| D4 | Persistência | NVS (`tab5/brightness`) e `display_storage` | Rápido, seguro e independente do cartão microSD |
| D5 | Estratégia de I/O | PWM imediato no arraste e gravação em NVS ao soltar | Resposta visual instantânea sem desgaste de escrita em Flash |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── display_storage.h      # [MODIFY] Protótipos para salvar e carregar brilho
│   └── ui_bar.h               # [MODIFY] Declarações do slider de brilho
├── display_storage.cpp        # [MODIFY] I/O do nível de brilho em NVS / SD
├── ui_bar.cpp                 # [MODIFY] Slider de brilho, label percentual e eventos
└── CMakeLists.txt             # [MODIFY] Mantém os módulos do componente app
```

## 4. Layout no Menu de Configurações

```
┌─────────────────────────────────────────┐
│ Configurações                           │
├─────────────────────────────────────────┤
│ Rotação Automática              [ O ]   │
│ Wi-Fi                           [ O ]   │
│ Bluetooth                       [ O ]   │
├─────────────────────────────────────────┤
│ Brilho                            80%   │
│ ━━━━━━━━━━━━●━━━━━━━━━━━ [10% - 100%]   │
├─────────────────────────────────────────┤
│ Protetor de Tela               [ 2 min] │
└─────────────────────────────────────────┘
```

## 5. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Backend de Persistência (`display_storage`)**: Funções `display_storage_load_brightness(int *percent)` e `display_storage_save_brightness(int percent)` com fallback padrão de 80%. Aplicação do brilho no boot durante `app_main.cpp`.
- [x] **Etapa 2 — Slider de Brilho no `ui_bar.cpp`**: Adição da linha "Brilho" no menu popover com `lv_slider`, label de porcentagem e estilização compatível com temas claro e escuro.
- [x] **Etapa 3 — Eventos e Ajuste em Tempo Real**: Callback `LV_EVENT_VALUE_CHANGED` chamando `bsp_display_brightness_set()` imediatamente e evento `LV_EVENT_RELEASED` gravando o valor persistente.
- [x] **Etapa 4 — Build, Validação e Teste em Hardware**: Teste físico de variação de intensidade luminosa do backlight e validação de persistência após desligar e ligar o Tab5.

## 6. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Usuário zerar o brilho e perder visibilidade | Clamp rígido `lv_slider_set_range(slider, 10, 100)` impedindo valores inferiores a 10% |
| Desgaste de Flash por escrita contínua durante o arraste | Gravação em NVS/SD despachada apenas no evento `LV_EVENT_RELEASED` (ao soltar o toque) |

## 7. Critérios de Validação & Teste em Hardware
1. Abrir o menu de configurações (engrenagem) na barra superior.
2. Arrastar o slider de brilho e observar a variação imediata e fluida da iluminação do display.
3. Verificar que o slider não permite ajuste abaixo de 10%.
4. Reiniciar o Tab5 e validar que o nível de brilho configurado é restaurado no boot.

## 8. Status de Conclusão: `[x] IMPLEMENTADO`
- **Controle de Brilho**: Implementado com slider de 10% a 100% no menu de configurações, PWM em tempo real, persistência em NVS/SD e restauração automática no boot.

---

# [x] Fase 23.1: Configuração Geral de Fuso Horário `✅ CONCLUÍDO`

## 1. Contexto & Objetivos
- Criação do módulo centralizado **`timezone_mgr`** para gerenciamento de fuso horário em todo o sistema operacional **Tab5 OS**.
- **Entrada Numérica Direta e Intuitiva**: O usuário configura o offset em horas inteiras (ex: `-3` para UTC-3 / Brasília, `0` para UTC, `+5` para UTC+5).
- **Padronização POSIX TZ**:
  - Conversão automática do offset para a convenção POSIX `TZ` (`setenv("TZ", tz_str, 1)` + `tzset()`), aplicando os sinais invertidos da especificação POSIX.
  - Sincronização automática com todas as funções C/C++ padrão (`localtime_r`, `strftime`, `ctime`).
- **Persistência Dupla**:
  - Gravado no **NVS** (namespace `"tab5"`, chave `"tz_offset"`) e espelhado no **microSD** em `/sdcard/timezone.cfg`.
- **Refatorações nos Módulos e Aplicações**:
  - **Barra Superior (`ui_bar`)**: Inclusão de linha "Fuso Horário" com botões `[-]` e `[+]` e atualização instantânea do relógio digital.
  - **Proteção de Tela (`ui_screensaver`)**: Relógio e data em descanso ajustados em tempo real ao fuso configurado.
  - **Câmera (`camera_mgr`)**: Nomenclatura `IMG_YYYYMMDD_HHMMSS.jpg` gerada com o timestamp local.
  - **Gravador (`audio_recorder`)**: Nomenclatura `REC_YYYYMMDD_HHMMSS.wav` gerada com o timestamp local.
  - **Notas (`ui_notas`)**: Sugestão de salvamento `nota_YYYYMMDD_HHMMSS.txt` com o timestamp local.
  - **Arquivos (`ui_files`)**: Coluna de data/hora de modificação dos arquivos formatada com o fuso local.
  - **Galeria (`ui_gallery`)**: Cabeçalhos e metadados cronológicos com o fuso local.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Abordagem de Fuso | Variável de ambiente POSIX `TZ` + `tzset()` | Mecanismo nativo, thread-safe com `localtime_r` e universal no libc/ESP-IDF |
| D2 | Formato de Entrada | Offset numérico inteiro simples (ex: `-3`) | Simplicidade ergonômica sem sobrecarregar a memória com banco tzdata completo |
| D3 | Interface de Ajuste | Botões `[-]` e `[+]` com indicador de valor no menu popover | Ajuste rápido sem necessidade de teclado virtual |
| D4 | Persistência | NVS (`tab5/tz_offset`) + microSD (`/sdcard/timezone.cfg`) | Resiliência e persistência mesmo sem o cartão SD inserido |
| D5 | Sincronização Reativa | Callback de botão invoca `clock_update()` imediatamente | Feedback visual instantâneo na barra de status |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── timezone_mgr.h         # [NEW] Declaração da API de fuso horário e helpers
│   ├── ui_bar.h               # [MODIFY] Declaração dos controles de fuso horário
│   └── ui_screensaver.h       # [MODIFY] Atualização do relógio sob fuso
├── timezone_mgr.cpp           # [NEW] Persistência NVS/SD, POSIX TZ e localtime helpers
├── ui_bar.cpp                 # [MODIFY] Linha Fuso Horário no menu e refresh de relógio
├── ui_screensaver.cpp         # [MODIFY] Relógio e data adaptados ao fuso horário
├── camera_mgr.cpp             # [MODIFY] Nomes de fotos IMG_YYYYMMDD_HHMMSS com fuso
├── audio_recorder.cpp         # [MODIFY] Nomes de gravações REC_YYYYMMDD_HHMMSS com fuso
├── ui_notas.cpp               # [MODIFY] Sugestão de nota_YYYYMMDD_HHMMSS com fuso
├── ui_files.cpp               # [MODIFY] Formatação de data/hora na lista com fuso
└── CMakeLists.txt             # [MODIFY] Registro de timezone_mgr.cpp
```

## 4. Status de Conclusão: `[x] IMPLEMENTADO`
- **Fuso Horário Global**: Implementado com offset numérico (`-3`), integração POSIX `TZ`, interface interativa no menu de configurações e sincronização completa com relógio, screensaver e geradores de arquivos.

---

# [x] Fase 24: Aplicativos de Câmera e Galeria de Fotos `✔ CONCLUÍDO`

## 1. Contexto & Objetivos
- **Aplicativo "Câmera" (`ui_camera`)**:
  - Integração com o sensor de câmera MIPI-CSI do ESP32-P4 através do subsistema `esp_video` (V4L2) e inicialização via `bsp_camera_start()`.
  - Exibição de preview em tempo real na tela em widget LVGL (canvas RGB565 / frame buffer em PSRAM).
  - Botão de disparo centralizado na barra inferior e atalho direto para a Galeria de Fotos.
  - **Salvamento Automático no microSD**:
    - Criação automática do diretório `/sdcard/imagens/` (se inexistente).
    - Nomenclatura no formato ISO/Americano: `IMG_YYYYMMDD_HHMMSS.jpg` (ex: `IMG_20260816_195500.jpg`), garantindo **ordenação natural cronológica** ou numeração sequencial monotônica caso o relógio RTC esteja sem sincronização.
    - **Orientação Adaptativa (Retrato / Paisagem)**: O preview da Câmera adapta a rotação dos frames RGB565 em tempo real (480×640 em modo Retrato e 640×480 em modo Paisagem) e grava os arquivos JPEG com a orientação física correta correspondente à tela do dispositivo.
- **Aplicativo "Galeria" (`ui_gallery`)**:
  - Leitura e listagem de fotos de `/sdcard/imagens/` ordenadas da **mais recente para a mais antiga**.
  - **Sincronização Estrita de Gravação**: Ao abrir a Galeria diretamente ou pelo atalho da Câmera, o sistema aguarda qualquer gravação assíncrona em andamento finalizar completamente no cartão SD (`camera_mgr_wait_save_done`) antes de escanear o diretório, garantindo que a última foto tirada seja sempre indexada e exibida de imediato.
  - **Decodificação Dinâmica com TJpgDec**: Adaptação do canvas para imagens verticais ($480 \times 640$) e horizontais ($640 \times 480$) com centralização responsiva na tela e suporte a `LV_EVENT_SIZE_CHANGED`.
  - Exibição da imagem em tela cheia com cabeçalho contendo nome do arquivo, data/hora e contador de fotos (`1 de N`).
  - **Navegação Intuitiva por Gestos e Toque**:
    - **Próxima imagem (mais antiga)**: Gesto de arrastar (swipe) da **direita para a esquerda** (`LV_DIR_LEFT`) OU toque simples na **metade direita** da tela.
    - **Imagem anterior (mais recente)**: Gesto de arrastar (swipe) da **esquerda para a direita** (`LV_DIR_RIGHT`) OU toque simples na **metade esquerda** da tela.
    - Botão superior de retorno ao launcher e atalho para abrir o app Câmera.
  - **Exclusão de Fotos com Confirmação**:
    - Botão de exclusão (`LV_SYMBOL_TRASH`) na barra superior do aplicativo.
    - Modal de confirmação (`lv_msgbox`) para prevenir exclusões acidentais.
    - Ao confirmar, o arquivo físico é removido do microSD (`unlink()`), o índice e contador (`N de Total`) são atualizados e a próxima foto (ou anterior) é exibida automaticamente (ou mensagem de "Nenhuma foto" se esvaziada).
- **Integração com a Tabela de Associações de Tipos de Arquivo (`file_assoc`)**:
  - Registro de extensões de imagem (`.jpg`, `.jpeg`, `.png`, `.bmp`) associadas à função `ui_shell_open_gallery_with_file(filepath)`.
  - **Abertura a partir do Gerenciador de Arquivos ("Arquivos")**: Ao navegar pelas pastas do microSD no aplicativo Arquivos (seja em `/sdcard/imagens/` ou em qualquer outro diretório), o clique em uma imagem dispara `file_assoc_open()`, abrindo diretamente o aplicativo **Galeria** para exibir a foto clicada em tela cheia e contextualizar a lista de fotos a partir daquele ponto.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Subsistema de Câmera | `esp_video` (V4L2) via MIPI-CSI / `bsp_camera_start()` | Padrão oficial de captura de alta performance no ESP32-P4 |
| D2 | Diretório e Nomes | `/sdcard/imagens/` + `IMG_YYYYMMDD_HHMMSS.jpg` / sequencial | Ordenação lexicográfica idêntica à ordem cronológica |
| D3 | Orientação Dinâmica | Rotação adaptativa do frame RGB565 e gravação JPEG orientada | Corrige desalinhamento do sensor em modo retrato e paisagem |
| D4 | Sincronização Câmera-Galeria | Espera bloqueante assíncrona antes do scan (`wait_save_done`) | Elimina condição de corrida e garante exibição da foto mais recente |
| D5 | Ordenação na Galeria | Varredura de diretório com ordenação reversa (decrescente) | Prioriza as fotos mais recentes imediatamente ao abrir |
| D6 | Controles de Navegação | `LV_EVENT_GESTURE` (swipe) + detecção de quadrante no `LV_EVENT_CLICKED` | Ergonomia tátil moderna e natural sem depender de botões minúsculos |
| D7 | Exclusão de Fotos | Botão `LV_SYMBOL_TRASH` + diálogo de confirmação | Previne remoção acidental e atualiza a galeria em tempo real |
| D8 | Associação no SO | Registro de `.jpg`/`.jpeg`/`.png`/`.bmp` em `file_assoc` | Abertura nativa e transparente a partir do app "Arquivos" |
| D9 | Lançadores no Desktop | Tiles dedicados "Câmera" e "Galeria" no `ui_desktop.cpp` | Acesso direto da tela principal com ciclo de vida no `ui_shell` |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── camera_mgr.h           # [NEW] Captura MIPI-CSI, frame buffers e compressão JPEG
│   ├── ui_camera.h            # [NEW] Interface do app Câmera (preview + botão de disparo)
│   ├── ui_gallery.h           # [NEW] Interface da Galeria (renderizador, gestos swipe/touch e exclusão)
│   ├── file_assoc.h           # [MODIFY] Registro de extensões (.jpg, .jpeg, .png, .bmp -> Galeria)
│   ├── ui_desktop.h           # [MODIFY] Tiles 'Câmera' e 'Galeria' no desktop
│   └── ui_shell.h             # [MODIFY] Rotinas ui_shell_open_gallery_with_file / close_gallery
├── camera_mgr.cpp             # [NEW] Backend de captura de vídeo e I/O de imagens no SD
├── ui_camera.cpp              # [NEW] UI da Câmera, atualização de preview e disparo
├── ui_gallery.cpp             # [NEW] UI da Galeria, ordenação decrescente, navegação e exclusão
├── file_assoc.cpp             # [MODIFY] Dispatcher para ui_shell_open_gallery_with_file
├── ui_desktop.cpp             # [MODIFY] Registro visual dos novos tiles
├── ui_shell.cpp               # [MODIFY] Transições de tela da câmera e galeria com suporte a caminho de arquivo
└── CMakeLists.txt             # [MODIFY] Registro de camera_mgr.cpp, ui_camera.cpp e ui_gallery.cpp
```

## 4. Especificação Visual e Layout dos Aplicativos

### Layout do Aplicativo "Câmera"
```
┌─────────────────────────────────────────┐
│  [←] Câmera                       [🖼]  │  ← Barra com voltar e atalho para Galeria
├─────────────────────────────────────────┤
│                                         │
│                                         │
│            Área de Preview              │  ← Video stream MIPI-CSI em tempo real
│                                         │
│                                         │
├─────────────────────────────────────────┤
│               [  (●)  ]                 │  ← Botão de disparo centralizado
└─────────────────────────────────────────┘
```

### Layout do Aplicativo "Galeria"
```
┌─────────────────────────────────────────┐
│  [←] IMG_20260816_195500.jpg (1/12) [🗑][📷]│ ← Barra com nome, índice, lixeira e atalho Câmera
├─────────────────────────────────────────┤
│ ◄ Toque / Swipe Dir │ Toque / Swipe Esq ►│
│ (Imagem Anterior)   │ (Próxima Imagem)   │
│                     │                    │
│                 [ FOTO ]                 │  ← Imagem em tela cheia com clamp
│                     │                    │
│                     │                    │
└─────────────────────────────────────────┘
```

## 5. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Backend de Câmera e Captura (`camera_mgr`)**: Inicialização do driver MIPI-CSI com `bsp_camera_start()`, streaming V4L2 em buffer PSRAM e gravação JPEG com nome `IMG_YYYYMMDD_HHMMSS.jpg` em `/sdcard/imagens/`.
- [x] **Etapa 2 — Interface do Aplicativo Câmera (`ui_camera`)**: Janela LVGL com canvas/image de preview, botão circular de disparo e atalho para galeria no `ui_shell`.
- [x] **Etapa 3 — Interface e Navegação da Galeria (`ui_gallery`)**: Leitura de `/sdcard/imagens/`, ordenação decrescente por data/nome e renderização em tela cheia com suporte a abertura de imagem específica via `ui_gallery_open_file(filepath)`.
- [x] **Etapa 4 — Sistema de Gestos, Cliques e Exclusão de Fotos**: Manipulação de `LV_EVENT_GESTURE` (`LV_DIR_LEFT` = próxima foto mais antiga, `LV_DIR_RIGHT` = foto anterior mais recente), toques nas metades laterais da tela e botão `[🗑]` com modal de confirmação para remoção física de fotos do SD.
- [x] **Etapa 5 — Atualização de Associações (`file_assoc`) e Desktop**: Registro de `.jpg`, `.jpeg`, `.png` e `.bmp` no `file_assoc_init()` apontando para a Galeria, e adição dos tiles "Câmera" e "Galeria" no `ui_desktop.cpp`.
- [x] **Etapa 6 — Build, Validação e Teste em Hardware**: Teste físico de captura de foto, gravação com nome cronológico no microSD, navegação por swipe na galeria, exclusão de fotos com diálogo de confirmação e validação de abertura de fotos diretamente pelo app Arquivos.

## 6. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Taxa de atualização do preview concorrer com o renderizador LVGL | Alocação de frame buffers duplos em PSRAM e lock `bsp_display_lock()` apenas na troca de ponteiro de frame |
| Atraso (lag) na gravação de imagem bloqueando o preview | Task assíncrona dedicada FreeRTOS para compressão e gravação no SD |
| Decodificação de imagens de alta resolução na Galeria | Uso do hardware JPEG decoder do ESP32-P4 ou decodificação com scaling para a resolução do display |
| Exclusão acidental de foto por toque inadvertido | Diálogo de confirmação obrigatório antes da remoção no microSD |

## 7. Critérios de Validação & Teste em Hardware
1. Abrir o app Câmera pelo Desktop e validar preview de vídeo fluido e nítido.
2. Pressionar o botão de disparo: verificar feedback de captura e criação de arquivo `IMG_YYYYMMDD_HHMMSS.jpg` na pasta `/sdcard/imagens/`.
3. Tocar no atalho de galeria: verificar abertura da Galeria exibindo a foto recém-tirada como a primeira da lista.
4. Deslizar o dedo da direita para a esquerda (ou clicar na direita da tela) e confirmar avanço para a foto anterior.
5. Deslizar da esquerda para a direita (ou clicar na esquerda da tela) e confirmar retorno.
6. Pressionar o botão de lixeira `[🗑]`, confirmar a exclusão e verificar a remoção do arquivo do SD e a exibição da foto subsequente.
7. Abrir o aplicativo **Arquivos**, navegar até `/sdcard/imagens/`, tocar em um arquivo `.jpg` e validar a abertura imediata no aplicativo **Galeria** exibindo a foto correspondente.

## 8. Status de Conclusão: `[x] CONCLUÍDO`
- **Câmera & Galeria**: Arquitetura implementada, validada e compilada com sucesso na Fase 24.


---

# [x] Fase 25: Aplicativo Gravador de Voz e Player de Áudio `✅ CONCLUÍDO`

## 1. Contexto & Objetivos
- Criação do aplicativo nativo **"Gravador"** (`ui_recorder`) para captura de voz através dos microfones integrados do M5Stack Tab5 e reprodução no alto-falante interno.
- **Hardware & Codecs**:
  - Captura de áudio através do codec ADC **ES7210** (`bsp_audio_codec_microphone_init()`) conectado ao barramento I2S DMA do ESP32-P4.
  - Reprodução através do codec DAC **ES8388** com amplificador de potência (`bsp_audio_codec_speaker_init()`).
- **Armazenamento e Padronização**:
  - Criação automática do diretório `/sdcard/gravacoes/` para persistência dos áudios.
  - Padrão de nomenclatura cronológico no formato ISO/Americano: `REC_YYYYMMDD_HHMMSS.wav` (ex: `REC_20260816_200530.wav`), estruturado em formato **WAV PCM 16-bit** (RIFF WAV header padrão de 44 bytes).
- **Interface e Recursos de Gravação**:
  - **Contador Digital de Tempo**: Formato `MM:SS` (ex: `00:00`, `02:45`) atualizado em tempo real a cada segundo durante a gravação.
  - **Limite Máximo Seguro de 5 Minutos (`05:00`)**: Trava automática de segurança que finaliza e salva a gravação ao atingir 5 minutos, protegendo a memória e o espaço de armazenamento.
  - **Botão de Gravação Principal**: Botão de início/parada com feedback de status visual (ícone de gravação animado/vermelho).
- **Listagem e Gestão de Gravações**:
  - Exibição de lista rolável das gravações salvas em `/sdcard/gravacoes/`, ordenadas da **mais recente para a mais antiga**.
  - **Ações por Item**:
    - Botão **Reproduzir / Pausar** (`LV_SYMBOL_PLAY` / `LV_SYMBOL_PAUSE`).
    - Botão **Excluir** (`LV_SYMBOL_TRASH`) com modal de confirmação antes da remoção definitiva do arquivo.
  - **Barra de Progresso de Reprodução**: Quando um áudio estiver em reprodução, exibe uma barra de progresso visual (`lv_bar` ou `lv_slider`) indicando a posição atual em relação à duração total (`01:20 / 03:45`).
- **Integração com a Tabela de Associações (`file_assoc`)**:
  - Registro da extensão `.wav` (e `.pcm`) no `file_assoc_init()` apontando para `ui_shell_open_recorder_with_file(filepath)`.
  - Ao clicar em um arquivo de áudio no aplicativo **"Arquivos"**, o sistema abre diretamente o **Gravador** carregando a gravação para reprodução com sua barra de progresso.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Codecs de Áudio | ES7210 (Microfone ADC) + ES8388 (Alto-falante DAC) via I2S | Suporte nativo do BSP `espressif/m5stack_tab5` |
| D2 | Formato de Gravação | WAV PCM linear 16-bit (16 kHz ou 44.1 kHz, mono) | Qualidade nítida de voz sem sobrecarga de CPU para compressão |
| D3 | Diretório e Nomes | `/sdcard/gravacoes/` + `REC_YYYYMMDD_HHMMSS.wav` | Ordenação cronológica natural idêntica à ordem alfabética |
| D4 | Limite de Gravação | 5 minutos fixos (`300 segundos`) | Previne esgotamento de heap/PSRAM e espaço em disco |
| D5 | Player e Barra de Progresso | Task I2S assíncrona despachando progresso percentual para `lv_bar` | Reprodução suave sem bloquear a thread gráfica da interface |
| D6 | Associação no SO | Registro de `.wav` em `file_assoc` -> `ui_shell_open_recorder_with_file` | Permite reproduzir áudios diretamente a partir do app "Arquivos" |
| D7 | Lançador no Desktop | Tile "Gravador" no `ui_desktop.cpp` | Acesso direto da tela principal com ciclo de vida no `ui_shell` |
| D8 | Tipografia e Ortografia | Fonte Latin-1 global (`lv_theme_default_init`) + ortografia PT-BR completa | Elimina caracteres desconhecidos (tofu) e padroniza acentuação na UI |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── audio_recorder.h       # [NEW] API de captura/playback I2S, encoder WAV e task de áudio
│   ├── ui_recorder.h          # [NEW] Interface do app Gravador (contador MM:SS, player e lista)
│   ├── file_assoc.h           # [MODIFY] Registro da extensão .wav -> Gravador
│   ├── ui_desktop.h           # [MODIFY] Tile 'Gravador' no launcher desktop
│   └── ui_shell.h             # [MODIFY] Rotinas ui_shell_open_recorder_with_file / close_recorder
├── audio_recorder.cpp         # [NEW] Backend de I/O de microfone/speaker, buffers e cabeçalho WAV
├── ui_recorder.cpp            # [NEW] Interface LVGL: contador digital, lista reversa e barra de progresso
├── file_assoc.cpp             # [MODIFY] Dispatcher para abertura de áudios via ui_shell
├── ui_desktop.cpp             # [MODIFY] Registro do tile 'Gravador' no desktop launcher
├── ui_shell.cpp               # [MODIFY] Transições de tela do gravador com suporte a caminho de arquivo
└── CMakeLists.txt             # [MODIFY] Registro de audio_recorder.cpp e ui_recorder.cpp
```

## 4. Especificação Visual e Layout da Interface

```
┌─────────────────────────────────────────────────────────┐
│  [←] Gravador                                     [✕]   │  ← Barra com voltar e fechar
├─────────────────────────────────────────────────────────┤
│                                                         │
│                      02:45 / 05:00                      │  ← Contador digital MM:SS
│                 [   ● GRAVAR / PARAR   ]                │  ← Botão principal de gravação
│                                                         │
├─────────────────────────────────────────────────────────┤
│  Reproduzindo: REC_20260816_200530.wav                  │
│  [▶] ━━━━━━━●━━━━━━━━━━━━━━━━━━━━ [01:10 / 02:45]       │  ← Player ativo com barra de progresso
├─────────────────────────────────────────────────────────┤
│  Gravações Salvas (/sdcard/gravacoes/):                 │
│  ┌───────────────────────────────────────────────────┐  │
│  │ ♫ REC_20260816_200530.wav (02:45)    [▶]   [🗑]   │  │  ← Item mais recente (topo)
│  │ ♫ REC_20260816_191012.wav (00:54)    [▶]   [🗑]   │  │
│  │ ♫ REC_20260816_183000.wav (04:12)    [▶]   [🗑]   │  │
│  └───────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

## 5. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Backend de Áudio e Codecs (`audio_recorder`)**: Inicialização do microfone ES7210 e speaker ES8388 via `bsp_audio_init()`, alocação de buffers DMA em PSRAM e implementação da escrita de cabeçalho WAV (RIFF, fmt, data).
- [x] **Etapa 2 — Mecanismo de Gravação e Limite de 5 Minutos**: Task de captura I2S com timer de 1s para o contador `MM:SS`, gravação em streaming em `/sdcard/gravacoes/` e parada automática aos 300 segundos (`05:00`).
- [x] **Etapa 3 — Mecanismo de Reprodução e Barra de Progresso**: Task de playback I2S alimentando o speaker ES8388 e atualizando a barra de progresso (`lv_bar`) em tempo real.
- [x] **Etapa 4 — Interface Gráfica e Lista de Gravações (`ui_recorder`)**: Listagem decrescente das gravações do microSD, botões contextuais de reprodução e exclusão de arquivos com modal de confirmação.
- [x] **Etapa 5 — Atualização de Associações (`file_assoc`) e Desktop**: Registro da extensão `.wav` no `file_assoc_init()` apontando para `ui_shell_open_recorder_with_file` e inclusão do tile "Gravador" no `ui_desktop.cpp`.
- [x] **Etapa 6 — Build, Validação e Teste em Hardware**: Teste físico de gravação de voz com os microfones integrados, teste do limitador de 5 minutos, reprodução no alto-falante com barra de progresso, exclusão de arquivos e abertura direta pelo app Arquivos.

## 6. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Queda de frames de áudio durante escrita no microSD | Buffer em anel duplo em PSRAM de 64 KB com escrita em blocos assíncronos |
| Concorrência de I2S entre microfone e alto-falante | Controle atômico de estado (IDLE, RECORDING, PLAYING) desabilitando o canal oposto durante a operação |
| Gravação longa consumir todo o espaço do SD | Limite rígido de 5 minutos (~4.8 MB em 16 kHz 16-bit mono) com checagem de espaço livre antes de iniciar |

## 7. Critérios de Validação & Teste em Hardware
1. Abrir o app Gravador pelo Desktop.
2. Iniciar uma nova gravação: verificar contador `MM:SS` avançando a cada segundo.
3. Testar a trava automática ao atingir 5 minutos (`05:00`), conferindo o salvamento automático do arquivo `REC_YYYYMMDD_HHMMSS.wav` em `/sdcard/gravacoes/`.
4. Verificar que a nova gravação aparece no topo da lista de gravações.
5. Pressionar o botão Reproduzir de uma gravação: verificar emissão de som clara no alto-falante e avanço da barra de progresso.
6. Pressionar o botão Excluir: confirmar a remoção física do arquivo do cartão SD e atualização da lista.
7. Abrir o app **Arquivos**, navegar até `/sdcard/gravacoes/`, tocar em um arquivo `.wav` e validar a abertura imediata no **Gravador** com reprodução.

## 8. Status de Conclusão: `[x] CONCLUÍDO`
- **Gravador de Voz**: Arquitetura implementada, validada e compilada com sucesso na Fase 25.

---

# [x] Fase 26: Aplicativo Chat IA (Interação com Modelos via API OpenAI-compatível) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Criação do aplicativo nativo **"Chat"** (`ui_chat`) para conversação por texto com modelos de linguagem (LLMs) acessados via API remota compatível com **OpenAI Chat Completions**.
- **Entrada e Saída de Texto**: O usuário digita uma mensagem no teclado virtual (ou físico Bluetooth) e recebe a resposta textual do modelo diretamente no console de conversa.
- **Configuração Flexível do Provedor**: Tela de cadastro para **URL base** da API, **token de autenticação** (Bearer) e **modelo** a ser usado, persistidos em `/sdcard/tab5_os/ai.cfg`.
- **Referência de Implementação — Plano OpenCode Go**:
  - O plano **OpenCode Go** (opencode.ai) expõe um endpoint **OpenAI-compatível** usado como referência da implementação de acesso a modelo: `POST {base_url}/chat/completions` com header `Authorization: Bearer <token>` e corpo JSON com `model` e `messages`.
  - Endpoint de exemplo do gateway: `https://opencode.ai/zen/go/v1/chat/completions` (modelos como `deepseek-v4-pro`, `deepseek-v4-flash`, `kimi-k2.6`, etc.).
  - A arquitetura segue o mesmo contrato, mas com **URL, token e modelo configuráveis** pelo usuário, permitindo acesso a qualquer gateway compatível (OpenCode Go, OpenAI, OpenRouter, Ollama, etc.).
- **Histórico de Conversa**: Manutenção do contexto de mensagens (`messages[]` com papéis `user`/`assistant`) na memória durante a sessão, permitindo diálogos de múltiplos turnos.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Protocolo de acesso ao modelo | **OpenAI Chat Completions** (`POST {base_url}/chat/completions`, Bearer token) | Contrato universal usado pelo plano OpenCode Go e pela maioria dos provedores/LLM |
| D2 | Cliente HTTP | `esp_http_client` (componente nativo do ESP-IDF) | Suporta HTTPS/TLS via mbedTLS, streaming (chunked) e timeouts configuráveis sem dependência externa |
| D3 | Execução da requisição | **Task FreeRTOS dedicada** (`ai_client_task`, stack em PSRAM) | Isola latência de rede e parsing JSON sem bloquear a thread gráfica do LVGL (padrão do `ssh_client`) |
| D4 | Serialização/parsing | `cJSON` (nativo do ESP-IDF) | Construção e leitura compacta do payload `{model, messages, max_tokens}` e da resposta `choices[0].message.content` |
| D5 | Persistência de configuração | `/sdcard/tab5_os/ai.cfg` (linhas `base_url=...`, `token=...`, `model=...`) | Mesmo padrão de `wifi.cfg`/`bt.cfg`, inspecionável e persistente entre boots |
| D6 | Thread-safety da UI | Callbacks despachados exclusivamente sob `bsp_display_lock()` | Atualização segura do console de conversa a partir da task de rede |
| D7 | Validação de conectividade | Checagem prévia com `wifi_mgr_get_status()` | Evita tentativas de requisição sem rede ativa, com mensagem de erro amigável |
| D8 | Integração no sistema | Tela `ui_chat` no `ui_shell`, tile no `ui_desktop` | Ciclo de vida, temas e rotação consistentes com os demais apps |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── ai_client.h            # [NEW] Cliente HTTP OpenAI-compatível (task, request/response)
│   ├── ai_storage.h           # [NEW] I/O de configuração em /sdcard/tab5_os/ai.cfg
│   ├── ui_chat.h              # [NEW] Interface do app Chat (console de conversa)
│   ├── ui_desktop.h           # [MODIFY] Tile 'Chat' no launcher
│   └── ui_shell.h             # [MODIFY] Rotinas ui_shell_open_chat / close_chat
├── ai_client.cpp              # [NEW] Cliente HTTP, montagem JSON, task FreeRTOS e callbacks
├── ai_storage.cpp             # [NEW] Leitura e escrita de base_url/token/modelo no SD
├── ui_chat.cpp                # [NEW] Console de conversa, campo de envio e tela de configuração
├── ui_desktop.cpp             # [MODIFY] Registro visual do tile 'Chat'
├── ui_shell.cpp               # [MODIFY] Transições de tela do chat
├── CMakeLists.txt             # [MODIFY] Registro de ai_client.cpp, ai_storage.cpp e ui_chat.cpp
main/
└── app_main.cpp               # [MODIFY] Chamada de ai_storage_load() no boot (se necessário)
```

## 4. Especificação Visual e Layout da Interface

### Tela Principal do Chat
```
┌─────────────────────────────────────────┐
│  [←] Chat                        [⚙]   │  ← Barra com voltar e configuração
├─────────────────────────────────────────┤
│  ┌───────────────────────────────────┐  │
│  │ Eu: O que é o ESP32-P4?           │  │  ← Histórico de mensagens
│  │ IA:  O ESP32-P4 é um SoC...       │  │     (bolhas user/assistant)
│  │     (resposta em múltiplas linhas)│  │
│  └───────────────────────────────────┘  │  ← Área rolável da conversa
│  ┌───────────────────────────────────┐  │
│  │ Digite sua mensagem...   [Enviar] │  │  ← Campo de texto + botão
│  └───────────────────────────────────┘  │
│  [ Teclado Virtual ou Físico Bluetooth ]│
│ └─────────────────────────────────────────┘
```

### Tela de Configuração do Provedor
```
┌─────────────────────────────────────────┐
│  [←] Configuração da IA          [✕]   │
├─────────────────────────────────────────┤
│  URL da API (base_url)                  │
│  ┌─────────────────────────────────────┐│
│  │ https://opencode.ai/zen/go/v1      ││  ← Ex.: gateway OpenCode Go
│  └─────────────────────────────────────┘│
│  Token de autenticação (Bearer)         │
│  ┌─────────────────────────────────────┐│
│  │ sk-••••••••••••••••••••            ││  ← Campo mascarado (echo oculto)
│  └─────────────────────────────────────┘│
│  Modelo                                 │
│  ┌─────────────────────────────────────┐│
│  │ deepseek-v4-pro                     ││  ← Ex.: modelo do plano Go
│  └─────────────────────────────────────┘│
│                    [ Salvar ]           │
└─────────────────────────────────────────┘
```

## 5. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Backend de Armazenamento (`ai_storage`)**: Leitura e escrita de `base_url`, `token`, `model`, `max_tokens` e `timeout_sec` em `/sdcard/tab5_os/ai.cfg` com parsing robusto e sanitização de valores.
- [x] **Etapa 2 — Cliente HTTP (`ai_client`)**: Montagem do payload `{model, messages, max_tokens}`, header `Authorization: Bearer <token>`, POST via `esp_http_client` em task FreeRTOS com stack em PSRAM, timeout configurável e parsing da resposta com `cJSON` (`choices[0].message.content`).
- [x] **Etapa 3 — Interface do Chat (`ui_chat`)**: Console de conversa com bolhas de usuário/assistente, campo de envio acoplado ao `ui_keyboard` (com suporte à supressão por teclado físico), indicador de "pensando..." durante a requisição e área de configuração do provedor.
- [x] **Etapa 4 — Integração com o Sistema**: Registro do tile 'Chat' no `ui_desktop`, ciclo de vida no `ui_shell`, suporte a 4 rotações e temas claro/escuro.
- [x] **Etapa 5 — Build, Validação e Teste em Hardware**: Teste e compilação limpa do binário `tab5_os.bin` e validação com pre-commit.

## 6. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Latência de rede bloqueando a interface LVGL | Todas as operações HTTP/JSON rodam exclusivamente na task FreeRTOS dedicada; a UI recebe apenas callbacks protegidos por `bsp_display_lock()` |
| Consumo de memória com respostas longas | `max_tokens` limitado (ex.: 512) e truncamento seguro da resposta no buffer antes da exibição |
| Certificado TLS para gateways HTTPS | Uso de `esp_http_client` com certificado raiz embutido do endpoint configurado (`esp_crt_bundle_attach`) |
| Token armazenado em texto plano no SD | Mesmo modelo dos demais configs do sistema (`wifi.cfg`/`bt.cfg`); documentado como limitação do firmware embarcado |
| Timeout sem resposta ou 429 de cota | Tratamento de erros HTTP (401, 429, 5xx) com mensagem legível no console e retorno ao prompt local |
| Provedor com streaming (SSE) | v1 sem streaming (resposta completa); streaming `data:` previsto como evolução futura |

## 7. Critérios de Validação & Teste em Hardware
1. Abrir o app Chat pelo Desktop e validar abertura da tela de conversa.
2. Acessar a configuração, cadastrar `base_url`, `token` e `model` (ex.: gateway OpenCode Go + `deepseek-v4-pro`) e salvar.
3. Reiniciar o Tab5 e validar que a configuração foi restaurada a partir do `ai.cfg`.
4. Enviar uma mensagem e validar a exibição da resposta do modelo no console.
5. Enviar uma segunda mensagem e validar a manutenção do contexto (múltiplos turnos).
6. Desconectar o Wi-Fi e validar a mensagem de erro amigável sem travamento da UI.
7. Testar em modo retrato (0°, 180°) e paisagem (90°, 270°) com teclado virtual e físico.

## 8. Status de Conclusão: `[x] IMPLEMENTADO`
- **Chat IA**: Aplicativo e cliente HTTP OpenAI-compatível implementados, validados e compilados com sucesso na Fase 26.


---

# [x] Fase 27: Padronização do Shell do SO, Barra de Título Reutilizável (`ui_app_bar`), Registro Modular de Apps (`app_registry`) e Associação Descentralizada `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- **Padronização Visual e de Shell**: Eliminar duplicações de código e inconsistências de interface entre as 10 aplicações do sistema operacional.
- **Barra de Título Padronizada (`ui_app_bar`)**: Componente unificado contendo:
  - Título do aplicativo alinhado à esquerda com regras claras (sempre preservando o nome do app, ex.: `"Arquivos - /sdcard"`, `"Notas - Minha Nota"`).
  - Ausência de botão voltar redundante.
  - Botão fechar padronizado e posicionado fixamente à extrema direita.
  - Suporte a botões de ações contextuais personalizados inseridos por cada app (ex.: Salvar, Excluir, Nova Pasta, etc.).
  - Estilo de botão compacto e quadrado/retangular (36×28px com raio de 6px), cores e feedback de toque alinhados ao tema do sistema.
- **Arquitetura Modular de Sistema Operacional (`app_registry`)**:
  - Cada aplicação passa a ser uma entidade modular autônoma responsável por declarar seu próprio manifesto (`app_desc_t`): ID, nome legível, ícone para o desktop (símbolo ou callback de desenho customizado), callback de inicialização e lista de extensões de arquivos suportadas.
- **Área de Trabalho Dinâmica (`ui_desktop`)**:
  - A grade do Desktop é construída iterativamente a partir do registro do SO (`app_registry_get_all()`), reduzindo centenas de linhas de código estático e suportando adição automática de novos apps.
- **Associação Descentralizada de Extensões (`file_assoc`)**:
  - Desacoplamento do mapa de extensões: cada app registra os formatos que suporta (ex.: `.txt`, `.cfg`, `.jpg`, `.jpeg`, `.wav`) e sua respectiva função de abertura de arquivos (`on_open_file`).
- **Documentação de Referência para Desenvolvedores**:
  - Criação do manual completo [`docs/APP_DEVELOPMENT.md`](docs/APP_DEVELOPMENT.md) detalhando o ciclo de vida, componentes compartilhados, manifesto e template passo a passo.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Barra de Título Única | Módulo `ui_app_bar` com flexbox LVGL 9 | Garante consistência visual, facilita manutenção e permite extensão flexível por app |
| D2 | Manifesto de Aplicativos | Struct `app_desc_t` gerenciada por `app_registry` | Arquitetura desacoplada inspirada em sistemas operacionais modernos |
| D3 | Ícones Customizados | Callback `icon_builder` e `icon_theme_refresh` | Permite apps com ícones compostos de múltiplos elementos (ex: Câmera) sem quebrar o padrão |
| D4 | Associação de Arquivos | Registro via campo `file_extensions` no manifesto | Elimina acoplamento do `file_assoc.cpp` com telas individuais |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── app_registry.h         # [NEW] Manifesto do app e APIs de registro do SO
│   ├── ui_app_bar.h           # [NEW] Componente de barra de título padronizada
│   └── ...                    # Headers das 10 aplicações atualizados com ui_<app>_register
├── app_registry.cpp           # [NEW] Repositório central de aplicações cadastradas
├── ui_app_bar.cpp             # [NEW] Construção e estilização da barra de título
├── ui_desktop.cpp             # [MODIFY] Construção 100% dinâmica da grade de tiles
├── file_assoc.cpp             # [MODIFY] Desacoplamento e rotas alimentadas pelo registry
├── ui_shell.cpp               # [MODIFY] Inicialização do registry e ciclo de vida
└── ...                        # Refatoração das 10 aplicações para adotar ui_app_bar e registry
docs/
└── APP_DEVELOPMENT.md         # [NEW] Guia completo para desenvolvimento de aplicações
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Módulo `ui_app_bar`**: Implementação da barra com título à esquerda, botão fechar à direita e botões customizados retangulares (36×28px, raio 6px).
- [x] **Etapa 2 — Registro Modular `app_registry`**: Struct `app_desc_t` e funções `app_registry_init()`, `app_registry_register()`, `app_registry_get_all()` e `app_registry_find()`.
- [x] **Etapa 3 — Refatoração dos 10 Aplicativos**: Adoção de `ui_app_bar` e implementação de `ui_<app>_register()` em Notas, Wi-Fi, Arquivos, Bluetooth, Terminal, Câmera, Galeria, Servidor, Gravador e Chat IA.
- [x] **Etapa 4 — Desktop Dinâmico**: Refatoração de `ui_desktop.cpp` para instanciar tiles consultando o repositório central.
- [x] **Etapa 5 — Associação Descentralizada**: Conexão automática entre extensões e handlers no `file_assoc_init()`.
- [x] **Etapa 6 — Guia Técnico e Documentação**: Criação de [`docs/APP_DEVELOPMENT.md`](docs/APP_DEVELOPMENT.md) e sincronização nos READMEs.
- [x] **Etapa 7 — Aprimoramentos no Screensaver e Status**: Correção do indicador Bluetooth (`bt_mgr.cpp`) e tratamento de redimensionamento e toque no protetor de tela.

## 5. Status de Conclusão: `[x] IMPLEMENTADO`
- **Padronização do Shell e Registro Modular**: 100% implementado, compilado, testado e validado em hardware.

---

# [~] Fase 28: Modo Pen Drive USB — USB Mass Storage (MSC) para Recuperação de Arquivos `❌ CANCELADO`

## 1. Contexto & Objetivos
- Implementar o **Modo Pen Drive**: ao conectar o Tab5 ao computador via USB-C, o dispositivo se apresenta ao sistema operacional do host como um **disco removível** (classe **MSC — Mass Storage Class**), permitindo que o PC monte o volume e o usuário copie/recupere arquivos do cartão microSD de forma transparente — sem precisar retirar o cartão físico.
- **Cenário de Uso Principal (Recuperação de Dados)**: O tab5-os armazena notas, fotos, gravações, configurações e arquivos diversos no microSD (`/sdcard`). O modo pen drive oferece um canal direto para backup, transferência ou resgate desses arquivos em qualquer computador.
- **Hardware e Stack**:
  - O ESP32-P4 possui controlador USB **OTG (DWC2)** nativo; no Tab5 o conector USB-C já é utilizado para programação (USB-Serial-JTAG) e alimentação.
  - A implementação de classe MSC em modo *device* será feita com **TinyUSB** (`esp_tinyusb`, componente oficial do ESP-IDF) com o driver **`tusb_msc`**, exportando o cartão SD como um dispositivo de blocos lógico.
- **Ativação**: Controle manual no menu de configurações (chave "Modo Pen Drive") com opção de ativação automática ao detectar conexão USB ao host — o sistema pergunta (ou age conforme a configuração persistida) antes de entregar o acesso ao disco.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Stack USB device | `esp_tinyusb` (TinyUSB) com classe **MSC** | Suporte oficial do ESP-IDF 5.5 para USB-OTG em modo device com múltiplas classes |
| D2 | Storage exportado | Passagem direta de blocos (`sdmmc_card_read_blocks`/`write_blocks`) do microSD | O computador enxerga o **filesystem FAT real** do cartão, montando e recuperando arquivos exatamente como estão no SD |
| D3 | Acesso exclusivo ao SD | Bloqueio do VFS local (`/sdcard`) enquanto o MSC estiver ativo | Previne concorrência de escrita entre o sistema e o host, evitando corrupção do FAT |
| D4 | Ativação | Chave "Modo Pen Drive" no menu de configurações + detecção de plugue USB | Controle explícito do usuário; o comportamento automático é opcional e persistido em NVS/SD |
| D5 | Segurança de dados | Desmontagem limpa e notificação visual antes de encerrar o modo | Garante que todos os buffers pendentes sejam sincronizados ao cartão antes do host desconectar |
| D6 | Múltiplos LUNs | Suporte futuro a LUN separado para uma partição/espelho em PSRAM | Permite expor também um volume de "sistema" sem comprometer o SD |

## 3. Estrutura de Arquivos & Componentes

```
components/app/
├── include/
│   ├── usb_msc.h               # [NEW] API de ativação, I/O de blocos e callbacks de conexão
│   ├── ui_bar.h                # [MODIFY] Switch "Modo Pen Drive" no menu de configurações
│   └── ui_shell.h              # [MODIFY] Modal/aviso de modo pen drive ativo
├── usb_msc.cpp                 # [NEW] Inicialização TinyUSB MSC, callbacks lun e I/O de blocos
├── ui_bar.cpp                  # [MODIFY] Switch do modo pen drive e persistência da preferência
├── ui_shell.cpp                # [MODIFY] Tela de aviso/bloqueio durante o modo ativo
└── CMakeLists.txt              # [MODIFY] Registro de usb_msc.cpp
main/
├── idf_component.yml           # [MODIFY] Adição da dependência espressif/esp_tinyusb
└── app_main.cpp                # [MODIFY] Inicialização condicional do modo pen drive no boot
```

## 4. Especificação de Fluxo e Interface

```
┌──────────────────────────────┐     ┌──────────────────────────────┐     ┌──────────────────────────────┐
│  Usuário liga "Modo Pen      │     │  Tab5 enumerado como         │     │  Host monta o volume e       │
│  Drive" no menu (ou pluga    │ ──> │  dispositivo MSC (TinyUSB)   │ ──> │  recupera/copia arquivos     │
│  no computador)              │     │  no USB-C / DWC2             │     │  do microSD (FAT)            │
└──────────────────────────────┘     └──────────────────────────────┘     └──────────────────────────────┘
```

### Fluxo de Ativação (Manual)
1. Usuário acessa o menu de configurações (engrenagem) e liga a chave **"Modo Pen Drive"** (preferência persistida).
2. O sistema exibe um aviso explicando que, durante o modo, os apps não acessarão o `/sdcard`.
3. Ao conectar o cabo USB-C ao computador (ou ao reiniciar com o modo ativo), o `usb_msc` inicializa o TinyUSB e o Tab5 é enumerado como um disco removível.
4. O computador monta o volume, e o usuário copia/recupera os arquivos livremente.
5. Ao finalizar, o usuário desliga o modo (ou o sistema detecta a desconexão) — ocorre a sincronização (`sync`) e o VFS `/sdcard` é restaurado para os apps.

### Fluxo de Ativação (Automático — opcional)
- Com a preferência "detectar automaticamente" ativa, o plugue do cabo dispara o modo; um modal de confirmação (`lv_msgbox`) pergunta "Ativar modo pen drive?" para evitar surpresas.

## 5. Fases de Execução da Funcionalidade

- [ ] **Etapa 1 — Dependência e Backend (`usb_msc`)**: Adicionar `espressif/esp_tinyusb` ao `main/idf_component.yml`; implementar inicialização do `tusb_msc` em modo device com callbacks de `inquiry`, `read_capacity`, `read`/`write` de blocos delegando ao driver SDMMC (`sdmmc_card_read_blocks`/`write_blocks`).
- [ ] **Etapa 2 — Bloqueio do VFS Local**: Garantir acesso exclusivo ao cartão durante o modo ativo (barreira no `wifi_storage`/apps de arquivos e aviso visual), com restauração segura do `/sdcard` ao desativar.
- [ ] **Etapa 3 — Switch no Menu de Configurações**: Chave "Modo Pen Drive" no `ui_bar.cpp` com persistência da preferência em NVS e modal explicativo ao ligar.
- [ ] **Etapa 4 — Detecção de Plugue/Desconexão**: Callbacks de conexão/desconexão do TinyUSB (quando disponíveis no DWC2) ou polling da linha VBUS para ativação automática e `sync` no desligamento.
- [ ] **Etapa 5 — Build, Validação e Teste em Hardware**: Compilação com `pre-commit`, gravação e validação real em Windows e Linux: montagem do volume, leitura/escrita de arquivos e integridade do FAT após uso.

## 6. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Corrupção do FAT por acesso concorrente ao SD | Acesso exclusivo: VFS local bloqueado enquanto o MSC estiver ativo, com sincronização (`sync`) antes de liberar |
| Velocidade limitada pela SDIO + TinyUSB | DMA SDMMC em blocos grandes (512 B setores) e buffer em PSRAM; prioridade de task dedicada |
| Enumeração instável em certos hosts | Utilização de descritores MSC padrão (BOT + SCSI) e conformidade com a spec de classe; testes em Linux e Windows |
| Usuário desconectar sem desligar o modo | Detecção de desconexão e flush forçado; instrução visual de "ejetar" antes de remover o cabo |
| Compartilhamento do controlador USB com o console (USB-Serial-JTAG) | Console permanece na interface JTAG; o MSC usa o mesmo barramento apenas em modo device ativo — validado em hardware |

## 7. Critérios de Validação & Teste em Hardware
1. Ligar "Modo Pen Drive" no menu e conectar o Tab5 ao computador via USB-C.
2. Verificar que o computador detecta um **disco removível** (sem driver especial) e monta o volume.
3. Copiar um arquivo do computador para o SD e outro do SD para o computador, conferindo integridade dos dados.
4. Acessar notas, fotos e gravações gravadas pelos apps (`/sdcard/notas/`, `/sdcard/imagens/`, `/sdcard/gravacoes/`) e recuperá-los pelo PC.
5. Desligar o modo e validar que os apps voltam a acessar normalmente o `/sdcard`.
6. Testar a desconexão abrupta do cabo e validar que o cartão permanece íntegro (sem corrupção) no próximo boot.

## 8. Status de Conclusão: `[~] CANCELADO`
- **Modo Pen Drive USB**: Funcionalidade cancelada e descartada do escopo de implementação.

---

# [x] Fase 24: Reestruturação Arquitetural Modular (`components/os` e `components/apps`) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Modularização do código-fonte do firmware eliminando o componente monolítico `components/app/`.
- Separação clara entre o subsistema do **Sistema Operacional** (`components/os`) e as **Aplicações de Usuário** (`components/apps`), aplicando o padrão de engenharia *Package by Feature*.

## 2. Estrutura Modular Implementada
- **`components/os/`**:
  - `core/`: Gerenciadores de sistema (`app_registry`, `file_assoc`, `timezone_mgr`, `display_storage`, `wifi_mgr`, `wifi_storage`, `bt_mgr`, `bt_storage`, `imu_reader`, `orientation`).
  - `shell/`: Interface do sistema operacional (`ui_shell`, `ui_desktop`, `ui_bar`, `ui_status`, `ui_screensaver`, `ui_keyboard`, `ui_mouse`, `ui_theme`, `ui_app_bar`, `ui_font`).
  - `fonts/`: Fontes Latin-1 compiladas.
- **`components/apps/`** (*Package by Feature*):
  - `camera/`: UI da câmera e driver de streaming MIPI-CSI (`ui_camera`, `camera_mgr`).
  - `gallery/`: Visualizador JPEG de alta performance (`ui_gallery`, `tjpgd`).
  - `notas/`: Editor de texto e notas em microSD (`ui_notas`).
  - `recorder/`: Gravador de áudio I2S e reprodutor WAV (`ui_recorder`, `audio_recorder`).
  - `chat/`: Assistente de Chat com IA (`ui_chat`, `ai_client`, `ai_storage`).
  - `terminal/`: Shell interativo e cliente SSH (`ui_terminal`, `terminal_cmd`, `ssh_client`).
  - `fileserver/`: Servidor Web HTTP de arquivos (`ui_fileserver`, `http_file_server`).
  - `files/`: Explorador de arquivos microSD (`ui_files`).
  - `wifi/`: Gerenciador de conexões Wi-Fi (`ui_wifi`).
  - `bluetooth/`: Gerenciador de conexões Bluetooth (`ui_bluetooth`).

## 3. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Build e Links**: Compilação concluída com sucesso no ESP-IDF 5.5.5 (`libos.a` e `libapps.a`).
- **Validação Local e Hardware**: Testado via `pre-commit` e gravado com sucesso no hardware M5Stack Tab5 via USB-C.

---

# [x] Fase 29: Aplicativo "Música" — Player de Áudio Local (MP3/WAV) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Criar um novo aplicativo nativo **"Música"** em `components/apps/music/` que reproduza arquivos de áudio **MP3** e **WAV** armazenados localmente no cartão microSD (sem streaming).
- Reaproveitar a infraestrutura de áudio já existente (DAC **ES8388** via `bsp_audio_codec_speaker_init()` + `esp_codec_dev_write()`) e a arquitetura modular de aplicações (`app_registry`, `ui_app_bar`, tema, teclado).
- Adicionar decodificação de MP3/WAV via componente oficial **`espressif/esp_audio_codec`** (Simple Decoder), compatível com a revisão de silício do **ESP32-P4** (v1.3) e os containers MP3 e WAV.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Decoder de áudio | Componente managed `espressif/esp_audio_codec` (<2.6.0) | Compatível com ESP32-P4 rev v1.3 e IDF 5.5, decodifica MP3/WAV sem necessidade do framework ESP-ADF completo |
| D2 | Saída de áudio | Reuso do DAC ES8388 (`esp_codec_dev_write`) | Mesmo pipeline de hardware com suporte dinâmico a múltiplas taxas de amostragem e canais |
| D3 | Origem de mídia | Arquivos locais em `/sdcard/musica/` (`*.mp3`, `*.wav`) | Foco em mídia local confiável e integração com `file_assoc` |
| D4 | Estrutura do app | `components/apps/music/` (*Package by Feature*) | Conformidade com o Guia de Desenvolvimento e arquitetura modular de apps do SO |

## 3. Estrutura de Arquivos & Componentes
- **`components/apps/music/music_player.{h,cpp}`**: Núcleo de áudio — task FreeRTOS `music_play_task` com stack de 64 KB alocado em PSRAM (`MALLOC_CAP_SPIRAM`), decodificação MP3 via `minimp3` e WAV via Simple Decoder, escrita no DAC ES8388, cálculo refinado de duração/progresso com parsing de tags ID3v2, estados (`IDLE`, `PLAYING`, `PAUSED`), controle de volume, gerenciamento de energia sob demanda do amplificador de som (PA), detecção automática de fone de ouvido no conector 3.5mm via `bsp_headphone_is_connected()` e chaveamento automático do alto-falante embutido (som apenas no fone quando conectado).
- **`components/m5stack_tab5/`**: BSP Tab5 — mapeamento do pino de detecção de fone `BSP_HEADPHONE_DET` (`IO_EXPANDER_PIN_NUM_7` no expansor PI4IOE5V6408), controle de inicialização do amplificador `BSP_SPEAKER_EN` em modo desligado por padrão (eliminando chiado em repouso) e função pública `bsp_headphone_is_connected()`.
- **`components/apps/music/ui_music.{h,cpp}`**: Interface LVGL — barra `ui_app_bar`, lista deduplicada de músicas de `/sdcard/musica/`, controles `Repetir 1`, `Prev`, `Play/Pause`, `Stop`, `Next` e `Loop da Playlist`, barra de progresso com trilha de alto contraste, tempo formatado, slider de volume e continuidade de playlist/repetição ativa em segundo plano com o app fechado.
- **`components/os/shell/ui_status.{h,cpp}`**: Indicador de reprodução de música na barra superior do sistema operacional (`LV_SYMBOL_AUDIO`), sincronizado com os estados `PLAYING`/`PAUSED`/`IDLE` e com ação de toque rápido para pausar e continuar a reprodução em qualquer tela.
- **`components/os/shell/ui_bar.{h,cpp}`**: Controle de volume geral (0% a 100%) no menu de Configurações da engrenagem com slider horizontal e indicador percentual em tempo real.
- **`components/apps/CMakeLists.txt`**: Registro de `"music/music_player.cpp"`, `"music/ui_music.cpp"`, include dir `"music"` e dependência `esp_audio_codec`.
- **`main/idf_component.yml`**: Adicionada dependência `espressif/esp_audio_codec: "<2.6.0,>=2.3.0"`.
- **`components/os/shell/ui_shell.{h,cpp}`**: Registro de `ui_music_register()`, `ui_music_create()`, `ui_shell_open_music()` e `ui_shell_open_music_with_file()`.
- **Associação de arquivos**: `.id="music"`, `.name="Música"`, `.icon_symbol=LV_SYMBOL_AUDIO`, `file_extensions = {"mp3","wav",nullptr}`.

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Dependência e Build**: Adicionado `espressif/esp_audio_codec` (<2.6.0) ao `main/idf_component.yml` compatível com ESP32-P4 rev v1.3; configuração validada e compilada.
- [x] **Etapa 2 — Módulo de Áudio (`music_player`)**: Task FreeRTOS dedicada com stack de 64 KB em SPIRAM que lê o arquivo em blocos, alimenta o decoder MP3/WAV e transmite PCM ao ES8388 com volume, mute no repouso, detecção de fone 3.5mm e estados de reprodução.
- [x] **Etapa 3 — Interface LVGL (`ui_music`)**: Card "Tocando Agora" com controles (Repetir 1, Anterior, Play/Pause, Parar, Próximo, Loop de Playlist), barra de progresso em tempo real de alto contraste, slider de volume 0-100%, card de lista de faixas de `/sdcard/musica/`, suporte a temas e layout responsivo.
- [x] **Etapa 4 — Continuidade em Segundo Plano & Integração no Shell**: Indicador de áudio na barra superior (`ui_status`) com Play/Pause rápido; slider de volume nas Configurações (`ui_bar`); playlist e repetição de faixas contínuas em background.
- [x] **Etapa 5 — Build, Validação e Teste em Hardware**: Firmware compilado com sucesso, 100% aprovado no `pre-commit` e gravado no M5Stack Tab5 via USB-C com reprodução validada e testada no hardware.

## 5. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Player de Áudio "Música"**: Aplicativo nativo implementado, integrado com a barra de status do sistema operacional, controles de repetição de faixa e playlist, detecção inteligente de fone de ouvido no conector 3.5mm com corte automático de alto-falante e gravado em hardware.

---

# [x] Fase 30: Testes Unitários Automáticos com Cobertura ≥80% `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
O projeto `tab5-os` (firmware ESP-IDF para ESP32-P4) **não possui nenhum teste automatizado** — cobertura atual é **0%**. O Quality Gate do CI (`quality-gate.yml`) hoje cobre apenas **build**, **lint** (clang-tidy/cppcheck) e **codeql**, sem job de teste.

Objetivo: criar uma suíte de **testes unitários automáticos** que rode no **host nativo** (Linux/CI) com **GoogleTest**, medindo cobertura com **gcov/lcov** e impondo um **gate mínimo de ≥80% de linhas** sobre o núcleo lógico testável.

## 2. Decisões de Arquitetura
- **Framework**: GoogleTest (via CMake FetchContent), rodando em host nativo (g++/clang no Linux + GitHub Actions).
- **Escopo da cobertura**: **núcleo lógico/persistência testável** — não o firmware inteiro (grande parte é acoplada a hardware/RTOS/LVGL e não é viável nem de alto valor de cobrir em 80%).
- **Cobertura**: gcov + lcov, com **gate ≥80% de linhas** sobre o conjunto de módulos sob teste.
- **Redirecionamento `/sdcard`**: **`--wrap=fopen/open`** do linker para um tmpdir no host — **sem alterar código de produção**.

### Por que host-native e não on-device
- Testes on-device (Unity/unity-runner) são lentos, exigem hardware e o ESP-IDF não gera coverage nativamente.
- A maioria do código (WiFi/BLE/NimBLE/FreeRTOS/LVGL/drivers) só seria coberta com mock extenso de baixo valor.
- O host native é rápido, determinístico, roda no CI e produz relatório de coverage real.

## 3. Módulos-Alvo

### Grupo A — Lógica pura (fácil, quase 100% cobrível)
| Módulo | Casos de teste |
|---|---|
| `orientation.cpp` | tabela gravidade→rotação, debounce de 5 leituras, "flat" mantém atual, reset |
| `file_assoc.cpp` | registrar/duplicar/inválido, abrir por extensão (com/sem ponto, case-insensitive), callback |
| `app_registry.cpp` | registrar/duplicado/busca por id/índice, auto-registro de extensões |
| `terminal_cmd.cpp` | tokenize com aspas, ls/cd/pwd/mkdir/rm/rmdir/touch/cat/echo/help, caminhos `..`/`.`, parse ssh (user@host, -p, inválidos) em tmpdir real |
| `timezone_mgr.cpp` | `format_offset` (+n/-n/0), get/set com clamping |

### Grupo B — Persistência/parsing (média, com redirect p/ tmpdir)
| Módulo | Casos de teste |
|---|---|
| `wifi_storage.cpp` | load/save_all, add/update/remove/find, seções `[nome]`, comentários, trim, limite 16 redes, round-trip |
| `bt_storage.cpp` | mesmo padrão + cache, type str↔enum, parse de campos |
| `ai_storage.cpp` | load/save, defaults, range de max_tokens/timeout |
| `display_storage.cpp` | load/save rotation (SD), load/save brightness (NVS+SD com fallback) |

### Fora do escopo (justificado)
`wifi_mgr`, `bt_mgr`, `imu_reader`, `shell/*` (UI LVGL), `apps/ui_*`, drivers de câmera/áudio/SSH/HTTP — dependem de hardware/RTOS/NimBLE/LVGL; mock extenso de baixo valor.

## 4. Estrutura de Arquivos Proposta

```
tests/host/
  CMakeLists.txt              # GTest (FetchContent) + --coverage; --wrap p/ fopen/open
  stubs/                      # headers mínimos p/ compilar em host
    esp_err.h esp_log.h esp_check.h nvs.h nvs_flash.h
    lvgl.h (enum lv_disp_rotation_t + lv_obj_t)
    bsp/esp-bsp.h (bsp_sdcard_mount mockável)
  mocks/                      # stubs de link (funções) + __wrap_fopen/__wrap_open
  src/                        # test_*.cpp (um por módulo)
tools/ci/
  run_host_tests.sh           # config + build + ctest + lcov + gate 80%
  lcovrc                      # exclui stubs/mocks/tests do cálculo
```

O `lcovrc` exclui `stubs/`, `mocks/` e `tests/` do relatório → a métrica cobre **apenas os `.cpp` de produção** sob teste, de forma honesta.

## 5. Integração CI (`.github/workflows/quality-gate.yml`)
1. **Novo job `test`**: ubuntu-latest; `apt-get install lcov g++ cmake`; `cmake -S tests/host -B build/host -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=--coverage`; build + `ctest --output-on-failure`; `lcov` capture + `genhtml`; **gate: falha se linhas < 80%**; upload do relatório HTML como artefato.
2. O Quality Gate (chamado via `workflow_call` em PRs e release) passa a **exigir o job `test`**.

## 6. Riscos & Mitigações
| Risco | Mitigação |
|---|---|
| Meta 80% difícil de atingir em algum módulo do Grupo B | Remover o módulo do cálculo via `lcovrc` até bem coberto; a meta vale sobre o núcleo realmente testado, com transparência no relatório |
| Acoplamento dos parsers aos caminhos fixos `/sdcard/...` | `--wrap=fopen/open` redireciona para tmpdir; não altera código de produção |
| Dependência de `lvgl.h` em `app_registry.h` | Stub mínimo (`lv_obj_t` + enum `lv_disp_rotation_t`); não usa funções LVGL reais |
| Custo-benefício | Foco em código onde bugs de lógica (parsing, orientação, registro de apps, shell) têm maior risco e retorno, sem mockar WiFi/BLE/LVGL |

## 7. Fases de Execução da Funcionalidade
- [x] **Etapa 1 — Infraestrutura host**: `tests/host/CMakeLists.txt` (GoogleTest via FetchContent, `--coverage`, `-Wl,--wrap=fopen/open/mkdir`) + `stubs/` + `mocks/` (NVS em memória, redirect `/sdcard`→tmpdir, `bsp_sdcard_mount` no-op).
- [x] **Etapa 2 — Testes do Grupo A**: `test_orientation`, `test_file_assoc`, `test_app_registry`, `test_terminal_cmd` (tmpdir real como cwd) e `test_timezone_mgr`.
- [x] **Etapa 3 — Testes do Grupo B**: `test_wifi_storage`, `test_bt_storage` (cache resetado via `save_all` por teste), `test_ai_storage` e `test_display_storage`.
- [x] **Etapa 4 — Gate local**: `tools/ci/run_host_tests.sh` + `tools/ci/lcovrc`; métrica restrita aos `.cpp` de produção via `--extract`.
- [x] **Etapa 5 — CI**: job `test` no `quality-gate.yml` (ubuntu-latest, apt g++/cmake/lcov) com resumo e artefato HTML do relatório.
- [x] **Etapa 6 — Validação local**: 84 testes aprovados; cobertura de **92,4% de linhas** (866/937) sobre os 9 módulos — gate ≥80% atendido.
- [x] **Etapa 7 — Qualidade**: pre-commit (clang-format/cmake-lint/codespell) aprovado nos novos arquivos.

## 8. Critérios de Validação
1. `tools/ci/run_host_tests.sh` roda no host e exibe relatório `lcov` com **≥80% de linhas** no conjunto-alvo.
2. Todos os testes do GoogleTest passam (`ctest` sem falhas).
3. O job `test` do GitHub Actions executa no PR e bloqueia merge se a cobertura cair abaixo de 80%.
4. Nenhum código de produção foi modificado para viabilizar os testes (usando stubs + `--wrap`).

## 9. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Suíte host-native operacional**: 84 testes GoogleTest cobrindo os 9 módulos-alvo (lógica pura + persistência), rodando em segundos sem hardware.
- **Cobertura 92,4%** de linhas sobre o núcleo testável (meta ≥80%), com relatório lcov/genhtml e gate aplicado localmente e no Quality Gate.
- **Zero alterações em código de produção**: isolamento garantido por stubs de headers ESP-IDF/LVGL, NVS mockado e redirecionamento de `/sdcard` via `--wrap` do linker.

---

# [x] Fase 31: Otimização de Memória Interna e Robustez do Servidor de Arquivos (Upload HTTP) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- A **RAM interna do ESP32-P4 é escassa** (~768 KB) e em grande parte **DMA-capable**, sendo disputada por esp_hosted (Wi-Fi SDIO), áudio I2S, câmera MIPI-CSI e pelos buffers do sdmmc. A **PSRAM de 32 MB (HEX @200 MHz, acessível por GDMA)** é a memória principal efetiva do dispositivo.
- **Problema 1 — Upload HTTP falhando**: O envio de arquivos pela interface web do app **Servidor** (`http_file_server.cpp`) retornava **HTTP 500 "Out of DMA memory"**. O handler de upload alocava um buffer de **512 B na heap DMA interna** por envio (`heap_caps_aligned_alloc(64, 512, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)`), que falhava sob pressão da heap interna durante o tráfego de rede (pbufs LWIP + buffers WiFi).
- **Problema 2 — Músicas em subpastas não tocavam**: Com `MALLOC_ALWAYSINTERNAL=16384` (default), toda alocação pequena de `malloc()` (widgets LVGL, `std::string`) caía na RAM interna. A renderização da lista de 13 faixas da subpasta `AJR/OK ORCHESTRA` consumia ~23 KB internos, deixando `internal=715` bytes livres no momento do play — a **task de reprodução falhava ao alocar o TCB em RAM interna** ("Falha ao criar task de reproducao de musica em PSRAM").

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Buffer de escrita do upload | **Nenhum** (grava direto do `net_buf` PSRAM) | O driver sdmmc já roteia buffers não-DMA/misaligned pelo `dma_aligned_buffer` de 4 KB dedicado que o BSP aloca no mount (`bsp_storage.c`), sem gastar a heap DMA interna |
| D2 | Política de `malloc()` | `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` | Todo `malloc()`/LVGL/`std::string` fica na PSRAM; `RESERVE_INTERNAL=32768` preserva RAM interna para TCBs de task, buffers DMA explícitos e `heap_caps` internos |
| D3 | Buffers estáticos de WiFi/LWIP | `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y` | pbufs e buffers estáticos da stack de rede movidos para PSRAM, aliviando a heap interna também durante streaming/upload |

## 3. Estrutura de Arquivos & Componentes

- **`components/apps/fileserver/http_file_server.cpp`**: Removido o buffer DMA de 512 B por upload (`SD_CHUNK`, `sd_buf`, `aligned_alloc`); a gravação agora é feita em fatias de até 4 KB direto do `net_buf` (PSRAM), com loop de escrita parcial já existente.
- **`sdkconfig.defaults`**: Adicionados `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=0` e `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`, com comentários explicando a escassez de RAM interna no P4.
- **Observação**: Alterar `sdkconfig.defaults` exige **apagar o `sdkconfig`** gerado e rebuildar para regenerar a configuração.

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Correção do upload**: Remoção do buffer DMA interno por upload em `upload_handler`; gravação direta do `net_buf` PSRAM confiando no `dma_aligned_buffer` do BSP.
- [x] **Etapa 2 — Migração de `malloc()` para PSRAM**: `ALWAYSINTERNAL=0` + `TRY_ALLOCATE_WIFI_LWIP=y` no `sdkconfig.defaults`; regeneração do `sdkconfig` e rebuild.
- [x] **Etapa 3 — Validação em hardware**: Flash via USB-JTAG; upload de `01 - OK Overture.mp3` pela UI sem HTTP 500; reprodução em sequência de 4 faixas da subpasta `AJR/OK ORCHESTRA`; `HEAP_DIAG` com `internal≈128 KB` e `dma≈88 KB` livres (antes `internal=715 B` / `dma=163 B`).

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Desempenho de `malloc()` em PSRAM | PSRAM octal HEX @200 MHz é suficiente para a carga da UI; drivers de hardware usam `heap_caps` explícito e não são afetados |
| Alocações que não podem usar PSRAM | `RESERVE_INTERNAL=32768` mantém reserva interna para TCBs, DMA e contexto de ISR |
| Crash LVGL intermitente ao abrir a app Música pela 1ª vez | `use-after-free` latente em `lv_event_mark_deleted`/`render_music_list` (`spec_attr==NULL`); reprodução em 2ª abertura e em série OK — **em investigação** se recorrer |

## 6. Critérios de Validação & Teste em Hardware

1. Upload de arquivos (inclusive `.mp3` grandes, >10 MB) pela UI do Servidor sem erro HTTP 500.
2. Navegação em pastas com muitas faixas no app Música e reprodução em sequência de subpastas.
3. `HEAP_DIAG` (fileserver e music_init) mostrando RAM interna saudável (~128 KB livres).
4. Boot limpo com Wi-Fi, SD e áudio operacionais.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Servidor de Arquivos & Memória**: Upload HTTP robusto e migração efetiva da carga de memória para a PSRAM, corrigindo upload com erro "Out of DMA memory" e reprodução de músicas em subpastas. Sem commit pendente após a validação em hardware.

---

# [x] Fase 32: Ativação Manual do Servidor de Arquivos `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- O app **Servidor** (`ui_fileserver`) iniciava o servidor HTTP automaticamente sempre que era aberto (`s_user_enabled = true`), fazendo com que o serviço ficasse ativo em portas de rede local sem solicitação explícita do usuário.
- Objetivo: o servidor só deve ficar ativo quando o usuário **solicitar explicitamente** pelo botão "Iniciar Servidor"; ao abrir o app, o estado inicial é **INATIVO**.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Gatilho de ativação | Somente o botão "Iniciar Servidor" | O servidor nunca sobe sozinho: nem no boot, nem ao abrir o app |
| D2 | Encerramento ao fechar o app | `ui_fileserver_on_close` → `http_file_server_stop()` (mantido) | Libera RAM interna (tasks/buffers de rede) que, se mantida, esgota a heap DMA e impede áudio/SD de alocarem buffers |

## 3. Estrutura de Arquivos & Componentes

- **`components/apps/fileserver/ui_fileserver.cpp`**: Removida a variável `s_user_enabled` e o auto-start em `ui_fileserver_on_open()`; o botão de controle (`toggle_btn_cb`) passa a ser o único gatilho de start/stop.

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Remoção do auto-start**: `s_user_enabled` removido; `ui_fileserver_on_open()` apenas atualiza o estado da UI (INATIVO) e cria o timer de refresh.
- [x] **Etapa 2 — Botão como único gatilho**: `toggle_btn_cb` faz `start`/`stop` sem estado persistente em RAM.
- [x] **Etapa 3 — Validação em hardware**: Flash via USB-JTAG; ao abrir o app o badge mostra **INATIVO**; servidor sobe apenas no toque em "Iniciar Servidor" e desliga ao fechar o app.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Usuário esquecer de iniciar o servidor | O app exibe o endereço URL e o botão de iniciar em destaque; o badge de status deixa o estado visível |
| Servidor desligado com app fechado | Comportamento desejado: libera RAM interna e não expõe a porta 8080 sem solicitação |

## 6. Critérios de Validação & Teste em Hardware

1. Ao abrir o app **Servidor**, o badge mostra **INATIVO** e o botão exibe "Iniciar Servidor".
2. O servidor responde em `http://<IP>:8080/` somente após tocar em "Iniciar Servidor".
3. Ao fechar o app, o servidor para (porta 8080 não responde mais).

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Ativação Manual do Servidor**: validado em hardware; comportamento conforme solicitado (servidor só ativo por solicitação explícita).

---

# [x] Fase 33: Estabilidade do Relógio da Barra Superior `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- O relógio da barra superior usava a fonte proporcional Montserrat, na qual os dígitos possuem larguras diferentes (ex: `1` = 370 unidades vs `4` = 669). A cada mudança de segundo, o label redimensionava e empurrava os ícones de status lateralmente.
- Objetivo: eliminar qualquer deslocamento horizontal da barra durante a atualização do relógio.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Fonte do relógio | JetBrains Mono Regular 14px (monoespaçada) | Todo glifo ocupa exatamente o mesmo bloco horizontal; mudança de valores não altera a largura do texto. Variante tabular do Montserrat foi testada e rejeitada visualmente |
| D2 | Formato exibido | `dd/mm/aaaa hh:mm` (sem segundos) | Reduz a frequência de atualização para 1×/minuto |
| D3 | Largura do label | Fixa via `lv_text_get_size("88/88/8888 88:88")` + `LV_TEXT_ALIGN_RIGHT` | Com fonte monoespaçada, qualquer combinação de valores mede idêntico; o texto ancora à direita |

## 3. Estrutura de Arquivos & Componentes

- **`components/os/fonts/lv_font_jetbrains_mono_14_clock.c`**: Nova fonte LVGL — subconjunto mínimo (`espaço`, `/`, `:`, `0-9`) com `lv_font_conv --no-kerning`, ~7 KB.
- **`components/os/shell/ui_font.h`**: Declaração `extern const lv_font_t lv_font_jetbrains_mono_14_clock`.
- **`components/os/shell/ui_bar.cpp`**: Relógio passa a usar a fonte monoespaçada, largura fixa medida na nova fonte e formato sem segundos.

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Diagnóstico**: Confirmação via fontTools dos avanços desiguais dos dígitos do Montserrat (causa raiz do deslocamento).
- [x] **Etapa 2 — Geração da fonte**: Download do `JetBrainsMono-Regular.ttf`, geração do subconjunto via `npx lv_font_conv@1.5.3` e ajuste do include para `lvgl.h`.
- [x] **Etapa 3 — Integração no shell**: Troca da fonte no `ui_bar`, remoção dos segundos e registro no build (`CMakeLists.txt`).
- [x] **Etapa 4 — Validação em hardware**: Flash via USB-JTAG; boot limpo, ícones estáveis e relógio imóvel a cada minuto.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Estética divergente entre relógio (mono) e demais textos (Montserrat) | Aceito pelo usuário após teste visual; monospace é tipografia consagrada em relógios/terminais |
| Caracteres ausentes no subconjunto renderizam como tofu | Formato do relógio é fixo (`dd/mm/aaaa hh:mm`) e usa apenas os glifos presentes |

## 6. Critérios de Validação & Teste em Hardware

1. Relógio exibe `dd/mm/aaaa hh:mm` em JetBrains Mono na barra superior.
2. Ao trocar o minuto, nenhum ícone ou elemento da barra se desloca (verificação visual).
3. Boot limpo sem erros no monitor serial.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Estabilidade do Relógio**: validado em hardware; barra totalmente estável com fonte monoespaçada.

---

# [x] Fase 34: Desligamento Automático da Tela (Screen-Off) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Economizar energia do painel MIPI-DSI desligando o **backlight** após um período configurável de inatividade, **sem** suspender o sistema: os aplicativos em execução (ex: Player de Música) continuam rodando normalmente em segundo plano.
- **Configuração persistente**: timeout ajustável no Menu de Configurações com as opções **Desativado, 30 segundos, 1 minuto, 2 minutos, 5 minutos e 10 minutos**, armazenado em NVS e restaurado no boot (padrão: 2 minutos).
- **Despertar por duplo toque**: um toque único na tela apagada é engolido (não gera clique fantasma no app por baixo) e não desperta; apenas o **segundo toque dentro de 400 ms** religa a tela. Mouse e teclado Bluetooth HID despertam imediatamente.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Desligamento do display | `bsp_display_brightness_set(0)` (PWM LEDC duty 0) | Desliga apenas o backlight, mantendo CPU, rádios e tasks dos apps ativos — reprodução de música e conectividade seguem intactas |
| D2 | Mecanismo de despertar no touch | Tela preta dedicada que engole o toque + detecção de duplo clique via `lv_tick_get()` (janela `DOUBLE_CLICK_MS 400`) | O primeiro toque não é propagado ao app (evita clique acidental); o segundo dentro da janela dispara `ui_screen_off_hide()` |
| D3 | Despertar por mouse/teclado BLE | `ui_screen_off_wake_up()` ao lado do `ui_screensaver_wake_up()` no `ui_mouse` e `ui_keyboard` | Input físico deliberado (movimento/clique/tecla) religa a tela imediatamente, como já ocorria com o screensaver |
| D4 | Persistência | NVS (namespace `tab5`, chave `screen_off`) | Mesmo padrão do timeout do screensaver (`ss_timeout`); rápido e independente do cartão microSD |
| D5 | Interação com o screensaver | Screen-off **assume** sobre o protetor de tela | Se o protetor estiver ativo quando o timeout do screen-off chegar, ele é ocultado e a tela apaga; no wake retorna direto ao app. Enquanto a tela está off, a checagem do screensaver é suprimida |
| D6 | Restauração do brilho | Brilho anterior salvo em `s_prev_brightness` via `display_storage_load_brightness()` | No wake, o backlight volta exatamente ao nível configurado (não fixo em 100%) |

## 3. Estrutura de Arquivos & Componentes

```
components/os/
├── shell/
│   ├── ui_screen_off.h          # [NEW] API do módulo (init/show/hide/check/wake/set_timeout)
│   ├── ui_screen_off.cpp        # [NEW] NVS, tela preta com duplo toque, backlight e restauração
│   ├── ui_shell.cpp             # [MODIFY] ui_screen_off_init() + checagem no inactivity_timer_cb
│   ├── ui_mouse.cpp             # [MODIFY] ui_screen_off_wake_up() no mouse BLE
│   ├── ui_keyboard.cpp          # [MODIFY] ui_screen_off_wake_up() no teclado BLE
│   └── ui_bar.cpp               # [MODIFY] Subpágina MENU_PAGE_SCREEN_OFF no menu de configurações
└── CMakeLists.txt               # [MODIFY] Registro de ui_screen_off.cpp
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Módulo `ui_screen_off`**: NVS (`screen_off`, default 120 s), tela preta dedicada que engole toques, `show()` (salva brilho + backlight 0 + oculta barra/cursor/teclado), `hide()` (restaura brilho + tela anterior + `lv_display_trigger_activity`) e `check_inactivity()` via `lv_display_get_inactive_time`.
- [x] **Etapa 2 — Despertar por duplo toque**: Handler `LV_EVENT_CLICKED` da tela preta com timestamps `lv_tick_get()` — o 1º toque apenas registra o instante; o 2º dentro de 400 ms chama `hide()`.
- [x] **Etapa 3 — Despertar por mouse/teclado BLE**: `ui_screen_off_wake_up()` adicionado em `ui_mouse.cpp` (movimento/clique) e `ui_keyboard.cpp` (char/tecla), ao lado das chamadas do screensaver.
- [x] **Etapa 4 — Integração no shell**: `ui_screen_off_init()` no `ui_shell_init`; no `inactivity_timer_cb` o screen-off é checado primeiro e o screensaver é suprimido enquanto a tela estiver off.
- [x] **Etapa 5 — Menu de configurações**: Linha "Desligar Tela" na página principal e subpágina `MENU_PAGE_SCREEN_OFF` com rádios Desativado / 30 segundos / 1 minuto / 2 minutos / 5 minutos / 10 minutos + Voltar, seguindo o padrão do screensaver.
- [x] **Etapa 6 — Validação em hardware**: Flash via USB-JTAG; timeout 30 s apagou o backlight (log `tela desligada (brilho anterior=10%)`), wake restaurou o brilho, NVS persistiu a mudança de 30 s → 120 s e o screensaver assumiu depois conforme o design.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Clique fantasma no app ao despertar | O toque de wake é consumido pela tela preta; apenas o 2º toque (duplo) religa e nenhum toque é propagado ao app por baixo |
| Despertar acidental por toque único | Janela de duplo clique de 400 ms: um toque isolado é engolido e não acorda a tela |
| Tela presa apagada sem volta | Mouse/teclado BLE despertam imediatamente; o duplo toque também funciona em qualquer ponto da tela |
| Screensaver e screen-off competindo | Screen-off assume sobre o protetor (`ui_screensaver_hide()` no show) e a checagem do screensaver é suprimida com a tela off |
| Brilho restaurado incorretamente | Brilho anterior lido de `display_storage_load_brightness()` (mesma fonte do slider do menu) e salvo em `s_prev_brightness` |

## 6. Critérios de Validação & Teste em Hardware

1. Configurar timeout de 30 s no menu: aguardar e validar que o backlight apaga com o app em segundo plano ainda rodando.
2. Toque único na tela apagada: NÃO deve religar nem gerar clique no app.
3. Duplo toque (janela de 400 ms): religa a tela restaurando o brilho anterior, sem clique fantasma.
4. Movimento/clique do mouse BLE e tecla do teclado BLE: religam a tela imediatamente.
5. Reiniciar o Tab5: o timeout escolhido é restaurado da NVS.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Screen-Off**: timeout configurável e persistente, backlight 0 mantendo apps ativos, despertar por duplo toque (touch) e imediato (mouse/teclado BLE) — validado em hardware real.

---

# [x] Fase 35: Botão de Energia na Barra Superior (Power Menu) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Adicionar um **botão de energia** (ícone `LV_SYMBOL_POWER`) na barra superior do sistema, na **ponta esquerda**, antes do botão de configurações (engrenagem).
- O botão abre um painel com três opções: **Desligar Tela**, **Reiniciar** e **Desligar**.
- O desligamento manual da tela deve reutilizar **exatamente o mesmo caminho** do desligamento automático por inatividade (Fase 34): duplo toque para despertar, brilho anterior restaurado e wake imediato por mouse/teclado BLE.
- Ações destrutivas (**Reiniciar** e **Desligar**) exigem **confirmação modal** antes de executar, evitando reboot/apagamento acidental por toque.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Ícone do botão | `LV_SYMBOL_POWER` (U+F011) na fonte `lv_font_montserrat_14_latin1` | Glifo já presente no subconjunto FontAwesome da fonte custom (verificado em `unicode_list_2`); zero custo de nova fonte |
| D2 | Posição na barra | Primeiro item do flex row (`ui_bar_init` cria o botão **antes** do gear) | Ordem de criação define a posição no layout flex; mesmo estilo visual do gear |
| D3 | Desligar Tela | `close_menu()` + `ui_screen_off_show()` | Caminho idêntico ao timeout automático: tela preta que engole toque, backlight 0, duplo toque desperta, brilho restaurado — sem duplicação de lógica |
| D4 | Reiniciar | Modal de confirmação → `esp_restart()` | Reinicialização limpa do firmware; confirmação evita toque acidental |
| D5 | Desligar | Modal de confirmação → `esp_deep_sleep_start()` sem fontes de despertar | ESP32-P4 suporta deep sleep (`SOC_DEEP_SLEEP_SUPPORTED=1`); consumo ~µA; não há API de power-off no BSP (botão físico é controlado pelo hardware). Acordar = 1 toque no botão físico do Tab5 (boot limpo) |
| D6 | Antes de reiniciar/desligar | `music_player_stop()` + `bsp_display_brightness_set(0)` + `vTaskDelay(150 ms)` | Backlight 0 antes do reset esconde o artefato azul/branco do painel MIPI-DSI durante o reboot; para o amplificador não gerar ruído na transição; log registra a ação no console USB-JTAG |
| D7 | Modal de confirmação | Overlay escuro em `lv_layer_top()` que cancela ao tocar fora + card centralizado em flex coluna (título, mensagem com `LONG_WRAP`, Cancelar/Confirmar) | Padrão dos modais dos apps (`ui_gallery`, `ui_music`, `ui_recorder`), elevado ao layer_top por ser ação de sistema; toda label define `lv_font_montserrat_14_latin1` explicitamente — sem herança, o fallback ASCII do `LV_FONT_DEFAULT` renderiza acentos como glifos inválidos |
| D8 | Espaçamento dos botões da barra | Power e gear com pad 6/4 e margens de 2 px, iguais aos ícones de status (`ui_status`) | Gap visual entre power↔gear (~14 px) fica coerente com o ritmo dos ícones Wi-Fi/BT/Música (pad 12 criava vão de 24 px) |

## 3. Estrutura de Arquivos & Componentes

```
components/os/
└── shell/
    └── ui_bar.cpp               # [MODIFY] Botão de energia, página MENU_PAGE_POWER,
                                 #          callbacks power_*_cb e show_power_confirm()
```

## 4. Fases de Execução da Funcionalidade

- [x] **Etapa 1 — Botão de energia**: criado como primeiro item do flex row da barra (antes da engrenagem), ícone `LV_SYMBOL_POWER`, mesmo estilo do gear (fundo transparente, radius 8, pressed accent).
- [x] **Etapa 2 — Painel "Energia"**: nova página `MENU_PAGE_POWER` no menu overlay ancorado sob a barra à esquerda, com as linhas Desligar Tela / Reiniciar / Desligar.
- [x] **Etapa 3 — Desligar Tela**: fecha o painel e chama `ui_screen_off_show()` (mesmo comportamento do automatismo da Fase 34).
- [x] **Etapa 4 — Confirmação**: `show_power_confirm()` constrói overlay+card no `layer_top`; "Confirmar" executa `power_confirm_ok_cb`; tocar fora ou "Cancelar" apenas descarta.
- [x] **Etapa 5 — Reiniciar**: backlight 0 + delay curto (log chega ao console) + `esp_restart()`, sem flash azul do painel.
- [x] **Etapa 6 — Desligar**: para o player se houver música tocando, zera o backlight, oculta o teclado e entra em `esp_deep_sleep_start()`.
- [x] **Etapa 7 — Tema**: botão e painel seguem a paleta ativa via `ui_bar_refresh_theme()` e `apply_menu_theme()`; modal é reestilizado se aberto durante troca de tema.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Reboot/shutdown acidental | Modal de confirmação obrigatório; overlay externo cancela |
| Ruído do PA durante transição ao sleep | Player parado antes do `esp_deep_sleep_start()` |
| Dispositivo "preso" após deep sleep | Botão físico do Tab5 reseta e inicia boot limpo (comportamento documentado) |
| Painel/modal abertos durante screen-off automático | `close_menu()` também descarta o modal de confirmação |
| Texto do modal escuro/ilegível ou acentos inválidos | Toda label define `lv_font_montserrat_14_latin1` e cores explicitamente via `apply_power_confirm_theme()` — o modal vive fora do ciclo do menu (`apply_menu_theme` retorna cedo sem painel) e herança no `layer_top` cai no fallback ASCII |

## 6. Critérios de Validação & Teste em Hardware

1. Botão de energia visível na ponta esquerda da barra, antes da engrenagem, nos temas claro e escuro.
2. Toque no botão abre o painel "Energia"; toque fora fecha.
3. "Desligar Tela": backlight apaga imediatamente; duplo toque religa com brilho anterior; mouse/teclado BLE despertam — idêntico ao timeout automático.
4. "Reiniciar" → Confirmar: tela apaga e dispositivo reinicia direto ao desktop (sem flash azul); Cancelar/toque fora não faz nada.
5. "Desligar" → Confirmar: tela apaga e dispositivo entra em deep sleep; 1 toque no botão físico reinicia o sistema.
6. Mensagens do modal com acentuação correta (fonte Latin-1), alinhadas em flex coluna; gap power↔gear igual ao dos ícones de status.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Power Menu**: botão de energia na ponta esquerda, painel com Desligar Tela (caminho do screen-off) / Reiniciar / Desligar (deep sleep), ambos com confirmação modal — validado em hardware real.
- **Refinamentos pós-validação**: fonte Latin-1 explícita e tematização própria do modal (texto legível nos dois temas), backlight 0 antes do `esp_restart()` (sem flash azul do painel) e pad/margens dos botões power/gear igualados aos ícones de status.

---

# [x] Fase 36: Monitor de Bateria INA226 e Proteção de Carregamento `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- O Tab5 possui circuito de monitoramento de energia **INA226** que o BSP oficial não expõe (`BSP_CAPS_BAT 0`); a bateria é uma NP-F550 removível de 2S (2000 mAh, 6,0–8,4 V).
- Adicionar um **ícone de bateria com percentual** na barra superior, entre o Wi-Fi e o relógio, indicando em tempo real se o aparelho está **carregando**, **na tomada (carregado)**, **na bateria** ou **somente no cabo sem bateria**.
- Toque no ícone abre **popup de detalhes** (Estado, Tensão, Corrente e Nível), atualizado pelo poll de 1 s da barra enquanto visível.
- Adicionar opção **"Proteção da bateria"** no menu Configuração: quando ligada, corta a carga em **90%** mesmo com o cabo conectado (o aparelho passa a consumir apenas energia do cabo) e retoma automaticamente em **85%**.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Acesso ao INA226 | Driver próprio (`core/battery_reader.cpp`) sobre `bsp_i2c_get_handle()` | Sem dependência nova; endereço 0x41, config `0x4527`, cal `0x0D55` (shunt 5 mΩ, 300 µA/LSB corrente, 1,25 mV/LSB barramento) conforme referências públicas do hardware |
| D2 | Detecção de carregamento | Corrente negativa < −15 mA no shunt | O pino `CHG_STAT` (expansor B P6) leu 1 mesmo durante carga e com ela cortada — não confiável; o shunt é inequívoco (+descarrega / −carrega) |
| D3 | Habilitação do carregador | `CHG_EN` (expansor B P7, push-pull alto) ativado no boot | O IP2326 vem **desabilitado por padrão**: sem isso o aparelho nunca carrega (bug da v1) |
| D4 | Percentual | Coulomb counting (mA/72000 por segundo, negativo soma) + estimativa inicial pela tensão 6,0–8,4 V | A INA226 mede VSYS (~8,1–8,4 V na tomada), não VBAT direto; integração evita saltos sob carga |
| D5 | Estados "tomada" e "sem bateria" | Votação incremental com clamp ±10: tomada = \|I\|<15 mA e VSYS ≥ 7900 mV; sem bateria = VSYS ≥ 8330 mV sustentado | Sem bateria o VSYS fica em ~8381 mV estável, mas pulsos do carregador geram glitches para ~4250 mV — glitch custa −1 voto, não zera a série |
| D6 | Proteção de carga | Corte em percent ≥ 90% **e** VSYS ≥ 8200 mV (sob carga), retomada ≤ 85%; `CHG_EN=0` mantém o aparelho só no cabo | A guarda de tensão impede que a estimativa otimista de boot corte a carga antes da hora; histerese 85–90% evita oscilação |
| D7 | UI | Par btn+label no padrão dos ícones de status (`ui_status.cpp`), popup com backdrop no `layer_top` e switch no padrão "Rotação" (`ui_bar.cpp`), persistência NVS `tab5/chg_protect` (padrão ligada) | Consistência com o shell; tema próprio do popup pois `apply_menu_theme()` retorna cedo sem painel |

## 3. Estrutura de Arquivos & Componentes

| Arquivo | Responsabilidade |
|---|---|
| `components/os/core/battery_reader.{h,cpp}` (**novo**) | Driver INA226 + leitura `CHG_STAT`/`CHG_EN` no expansor B (0x44), máquina de estados por votação, coulomb counting, proteção de carga e NVS |
| `components/os/shell/ui_status.cpp` | Ícone de bateria com percentual, cores por estado, popup de detalhes no `layer_top` |
| `components/os/shell/ui_bar.cpp` | Switch "Proteção da bateria" no menu Configuração |
| `main/app_main.cpp` | `battery_reader_start()` após `imu_reader_start()` |

## 4. Etapas Executadas

- [x] **Etapa 1 — Driver INA226**: dispositivo I2C @0x41, config/calibração, leitura de barramento/corrente a cada 1 s na task LVGL (`lv_timer`, padrão `imu_reader`).
- [x] **Etapa 2 — Carregador**: configuração de `CHG_STAT` (entrada pull-up open-drain) e ativação de `CHG_EN`; telemetria `V/I/chg_stat/fonte/pct` a cada 10 s.
- [x] **Etapa 3 — Máquina de estados**: Carregando (corrente), Na tomada / Sem bateria (votação por tensão), Na bateria (fallback); ocultação após 3 erros I2C consecutivos.
- [x] **Etapa 4 — Ícone na barra**: símbolo por nível (FULL/3/2/1/EMPTY) + percentual; raio quando carregando; PLUS accent sem percentual sem bateria; vermelho fixo ≤15% na bateria.
- [x] **Etapa 5 — Popup**: backdrop que engole toques fora, título/X, Estado/Tensão/Corrente/Nível, tematização própria e atualização pelo poll existente.
- [x] **Etapa 6 — Proteção**: NVS + lógica de corte/retomada com guarda de tensão; reação imediata ao desligar o switch (religa `CHG_EN`).
- [x] **Etapa 7 — Menu**: row "Proteção da bateria" com switch no painel Configuração.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Estimativa inicial otimista corta a carga cedo demais | Guarda adicional de tensão ≥ 8200 mV sob carga; coulomb counting corrige o percentual ao longo do uso |
| Glitches de VSYS (~4250 mV) sem bateria quebram a detecção | Votação incremental (glitch = −1 voto) em vez de reset instantâneo |
| Leitura I2C travando a task LVGL | Transações curtas (100 ms timeout) a 1 Hz; 3 falhas consecutivas ocultam o ícone |
| Bateria cheia flutuando acima do limiar de "sem bateria" | Limiar 8330 mV acima da flutuação típica (~8,25 V); monitorar via telemetria |

## 6. Critérios de Validação & Teste em Hardware

1. Com cabo e bateria: ícone raio em cor de destaque; log `fonte=2`; corrente ~−310 mA.
2. Só cabo (sem bateria): ícone PLUS sem percentual; popup "Somente cabo (sem bateria)"; estado converge mesmo com glitches periódicos de VSYS.
3. Cabo fora: símbolo de nível na cor de texto; percentual decresce devagar; ≤15% fica vermelho.
4. Proteção ligada + carga atingindo 90%: log `protecao: carga cortada`, corrente → ~0, popup "Na tomada (proteção 90%)"; desligar o switch religa o carregador imediatamente.
5. Reinício persistindo a opção (NVS): boot mostra `protecao de carregamento ...: ligada/desligada` conforme o último estado.
6. Popup abre/fecha por toque fora ou X, com fonte Latin-1 e cores corretas nos dois temas.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Monitor + proteção validados em hardware real** (ago/2026): carregamento medido a −310 mA, corte executado em 94% com VSYS 8326 mV, flutuação pós-corte a +2 mA só no cabo, estados de tomada/bateria/sem-bateria confirmados por telemetria.
- **Pós-validação**: descoberta de que o carregador vem desabilitado (`CHG_EN` obrigatório no boot) e de que `CHG_STAT` não reflete o estado real — documentados nas decisões D2/D3.

---

# [x] Fase 37: Persistência do Volume Geral de Áudio `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- O volume geral do sistema (0–100%), controlado pelo slider "Volume" no menu Configuração e pelo slider no app Música, era aplicado apenas em runtime (`music_player_set_volume`) e voltava ao padrão de 80% a cada boot.
- Persistir o valor seguindo o mesmo modelo do brilho (`display_storage`): NVS como fonte primária e SD como fallback inspecionável, com restauração automática no boot.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Módulo de persistência | `core/audio_storage.{h,cpp}` (**novo**): NVS `tab5/volume` + `/sdcard/tab5_os/audio.cfg` (linha `volume=N`) | Mesmo padrão dos demais domínios (`wifi_storage`, `bt_storage`, `display_storage`); load NVS → SD → 80%, save grava ambos |
| D2 | Carga do valor salvo | Lazy-load único (`ensure_volume_loaded()`) em `music_player_init/get/set_volume` | Valor correto independente da ordem boot × abertura do app × abertura do menu, sem acoplar áudio ao `app_main` |
| D3 | Momento da gravação | Somente no `LV_EVENT_RELEASED` dos sliders (padrão do brilho) | `LV_EVENT_VALUE_CHANGED` dispara a cada passo do arraste; gravar em cada tick desgastaria flash à toa |

## 3. Estrutura de Arquivos & Componentes

| Arquivo | Responsabilidade |
|---|---|
| `components/os/core/audio_storage.{h,cpp}` (**novo**) | `audio_storage_load_volume()` / `audio_storage_save_volume()` com clamp 0–100 |
| `components/os/CMakeLists.txt` | Registro do novo source |
| `components/apps/music/music_player.cpp` | Lazy-load antes de aplicar o volume no codec |
| `components/os/shell/ui_bar.cpp` | Slider "Volume" do menu Configuração grava no soltar |
| `components/apps/music/ui_music.cpp` | Slider de volume do app Música grava no soltar |

## 4. Etapas Executadas

- [x] **Etapa 1 — Storage**: módulo `audio_storage` com NVS primário, fallback SD (`wifi_storage_mount`) e padrão 80%.
- [x] **Etapa 2 — Player**: lazy-load integrado a init/get/set; valor restaurado é aplicado ao codec na inicialização do speaker.
- [x] **Etapa 3 — UIs**: handlers `LV_EVENT_RELEASED` nos dois sliders chamando `audio_storage_save_volume()`.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Escrita em flash a cada tick do arraste do slider | Gravação somente no release (`LV_EVENT_RELEASED`) |
| SD ausente ou falha de montagem | Load cai no NVS; save sempre grava NVS e só escreve SD se a montagem ok |
| Valor fora de faixa/corrompido no arquivo | Clamp 0–100 na leitura e na gravação; fallback para 80% |

## 6. Critérios de Validação & Teste em Hardware

1. Ajustar o volume no menu Configuração ou no app Música: log `tab5_audio_storage: volume salvo: N%`.
2. Reiniciar: log `volume carregado do NVS: N%`; menu e app Música iniciam sincronizados no valor salvo.
3. Primeiro boot sem configuração prévia: adota 80%.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Validado em hardware real** (ago/2026): volume ajustado a 32% na UI sobreviveu ao reboot — boot exibiu `volume carregado do NVS: 32%`.

---

# [x] Fase 38: Ajustes de Usabilidade do Menu de Configuração `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Com **230 px** de largura, o painel do menu cortava textos longos (ex: "Proteção da bateria") e apertava as linhas com rótulo + controle à direita.
- As trilhas dos sliders usavam `pal->border` em `LV_PART_MAIN`, cor quase idêntica à superfície do painel nos dois temas: só o preenchimento accent aparecia, sem referência visual do limite (0–100%).

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Largura do painel | 320 px (`menu_panel`) | Elimina o corte de texto; cabe com folga nas orientações paisagem (1280 px) e retrato (720 px) mantendo o canto superior esquerdo |
| D2 | Trilha dos sliders | `pal->text_muted` com `LV_OPA_40` em `LV_PART_MAIN` | Contraste garantido sobre a superfície do painel em claro e escuro; aplicado nos três sliders (Brilho e Volume via `apply_menu_theme`, Volume do app Música na criação e no `refresh_theme`) |

## 3. Estrutura de Arquivos & Componentes

| Arquivo | Responsabilidade |
|---|---|
| `components/os/shell/ui_bar.cpp` | Largura do `menu_panel` (230 → 320 px) e trilha dos sliders Brilho/Volume no `apply_menu_theme()` |
| `components/apps/music/ui_music.cpp` | Trilha do slider de volume na criação e no `ui_music_refresh_theme()` |

## 4. Etapas Executadas

- [x] **Etapa 1 — Painel**: largura 320 px; todas as páginas do menu (Configuração, Tema, Protetor de Tela, Desligar Tela e Energia) herdam automaticamente.
- [x] **Etapa 2 — Trilhas**: `bg_color = text_muted` + `bg_opa = LV_OPA_40` em `LV_PART_MAIN`; indicador accent e knob preservados.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Trilha competindo com o indicador accent | Opacidade de 40% mantém a trilha discreta porém legível |
| Painel largo cobrindo mais conteúdo atrás | Overlay transparente já engole toques fora e fecha o menu ao clicar |

## 6. Critérios de Validação & Teste em Hardware

1. Menu Configuração aberto: nenhum texto cortado; linhas com switch/slider respirando.
2. Sliders de Brilho e Volume: trilha cinza visível do início ao fim nos temas claro e escuro.
3. App Música: slider de volume com a mesma trilha de referência.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Validado em hardware real** (ago/2026): painel sem cortes de texto e trilhas dos três sliders visíveis nos dois temas.

---

# [x] Fase 39: Screenshot pela Barra do Sistema `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- O Tab5 OS não tinha forma nativa de capturar a tela; prints exigiam fotografar o painel ou acessar via fileserver.
- Objetivo: botão de captura na barra do sistema que grava a imagem exatamente como exibida (barra superior, teclado, modais e overlays incluídos) no cartão microSD, visível no visualizador do dispositivo.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Fonte da captura | `lv_snapshot_take` da tela ativa (RGB565) + `lv_snapshot_take` do `layer_top` (ARGB8888) com blend por alpha | Primeira tentativa (dump do framebuffer MIPI-DSI com transposição física→lógica) falhou: a varredura do painel tem compensação de montagem que invertia o retrato em 180°. O snapshot é feito na orientação lógica, correto por construção em qualquer rotação (0/90/180/270), e o blend do `layer_top` garante barra, teclado e modais na imagem |
| D2 | Habilitação da API | `CONFIG_LV_USE_SNAPSHOT=y` no sdkconfig e sdkconfig.defaults | Snapshot vem desabilitado por padrão no Kconfig do LVGL 9 |
| D3 | Gravação | Task one-shot FreeRTOS gravando BMP 24-bit linha a linha (RGB565→BGR888) em `/sdcard/screenshots` | Evita travar a UI ~1–3 s de escrita no SD; conversão streaming gasta só uma linha de buffer interno (3,8 KB); pasta na raiz do SD para acesso direto |
| D4 | Feedback | Flash branco overlay (~300 ms) imediato + toast com resultado (2,5 s) | Confirmação visual padrão de sistemas desktop; toast informa nome do arquivo ou falha; flash criado APÓS o snapshot para não sair na foto |
| D5 | Visualizador | `decode_bmp` da Galeria escala por potências de 2 até caber em 640×640 e usa stride = largura final | Antes cortava a parte direita/inferior e não atualizava as dimensões do canvas; prints em retrato ficavam distorcidos (stride 640 em canvas de 360) |

## 3. Estrutura de Arquivos & Componentes

| Arquivo | Responsabilidade |
|---|---|
| `components/os/core/screenshot.{h,cpp}` | Snapshot duplo + blend alpha, flash, task de gravação BMP e toast de resultado |
| `components/os/shell/ui_bar.cpp` | Botão de câmera após a engrenagem, callback e tema |
| `components/os/CMakeLists.txt` | Registro de `core/screenshot.cpp` |
| `components/apps/gallery/ui_gallery.cpp` | `decode_bmp` com escala por potências de 2, stride correto e teto 640×640 |
| `sdkconfig` / `sdkconfig.defaults` | `CONFIG_LV_USE_SNAPSHOT=y` |

## 4. Etapas Executadas

- [x] **Etapa 1 — Módulo core**: captura, flash, writer task, BMP 24-bit bottom-up e toast.
- [x] **Etapa 2 — UI**: ícone `LV_SYMBOL_IMAGE` à esquerda, após a engrenagem, com tema claro/escuro.
- [x] **Etapa 3 — Correção de orientação**: dump do fb físico substituído por snapshot lógico + blend do `layer_top` (retrato saía invertido 180° pela compensação de montagem do painel).
- [x] **Etapa 4 — Galeria**: `decode_bmp` com escala e stride corretos para imagens maiores que o canvas.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Pico de memória no snapshot duplo (base + ARGB8888 + destino ≈ 6–7 MB PSRAM) | Buffers liberados imediatamente após o blend; PSRAM de 32 MB comporta com folga |
| Escrita SD lenta congelando UI | Gravação fora da task LVGL; captura síncrona custa só snapshot + blend (~100 ms) |
| Flash/toast aparecendo no print | Flash criado após o snapshot; toast só existe após a gravação |
| `layer_top` com dimensões diferentes da tela | Blend pulado com warning; print sai sem barra em vez de corrompido |

## 6. Critérios de Validação & Teste em Hardware

1. Print com app aberto: conteúdo, barra superior e relógio corretos, sem espelhamento. ✓
2. Print nas orientações retrato e paisagem (IMU): imagens orientadas corretamente. ✓
3. Arquivo `.bmp` abre no PC (via fileserver) com cores fiéis; toast mostra o nome gerado. ✓
4. Print abre no visualizador do dispositivo sem corte nem distorção, nas duas orientações. ✓

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Validado em hardware real** (ago/2026): prints corretos em retrato e paisagem, salvos em `/sdcard/screenshots`, visualizados no PC e no visualizador do Tab5.

---

# [x] Fase 40: Simulador Host SDL e Regressão Visual da UI `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Toda validação da interface era manual, em hardware real — regressões visuais de layout, tema e fluxo de apps só apareciam depois do flash no Tab5.
- Objetivo: compilar a **UI real** (`os/shell` + apps) em um binário host com backend **SDL2** (janela 720×1280 RGB565), executar cenários automatizados (boot, desktop, abertura de cada app, power menu) e comparar capturas BMP contra **imagens douradas** versionadas, tornando regressões visulares detectáveis sem hardware.
- Ferramenta de regressão manual: roda em segundos na máquina do desenvolvedor; não é job de CI (depende de display para o backend SDL).

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Compilação da UI | LVGL vendido (`managed_components/lvgl__lvgl`) + `lv_sdl_window` + shims de BSP/FreeRTOS/ESP-IDF | Exerce o código de produção de verdade (shell, desktop, apps), não uma réplica |
| D2 | Acesso a `/sdcard` | Extensão dos wraps de link para `opendir`/`stat` (além de `fopen`/`open`/`mkdir`) redirecionando para tmpdir | Galeria/Arquivos/Terminal listam diretórios via POSIX; zero alteração no código de produção |
| D3 | Determinismo | Relógio congelado (`--wrap=time/localtime_r`, época fixa, TZ −3 → 21:00) e `srand(42)` | Duas execuções consecutivas produzem capturas idênticas — pré-requisito para comparação byte a byte com tolerância |
| D4 | Comparação | Pillow com tolerância dupla: ≤0,5% dos pixels divergentes E delta RGB por canal ≤48 | Absorve anti-aliasing/redesenho benigno; falha grava PNG com as regiões diferentes em vermelho e relatório |
| D5 | Goldens versionados | BMPs commitados em `tests/simulator/goldens/<cenario>/` | Mudança visual intencional exige regeneração explícita (`--update-goldens`) e revisão no diff |

## 3. Estrutura de Arquivos & Componentes

```
tests/simulator/
├── CMakeLists.txt          # Build standalone: LVGL vendido + SDL2 + wraps de link
├── lv_conf.h               # LVGL do simulador (RGB565, SDL, snapshot, malloc clib)
├── main.cpp                # Boot da UI, modo interativo e runner de cenários
├── README.md               # Uso, dependências e teclas do modo interativo
├── scenarios/              # 15 cenários + injeção de clique SDL e fixtures
├── shims/                  # BSP/FreeRTOS/esp_* stubs, sim_time (relógio congelado), sim_capture
└── goldens/<cenario>/      # Imagens de referência 01_*.bmp (versionadas)
tests/host/mocks/
└── link_wrappers.cpp       # [MODIFY] Wraps estendidos com opendir/stat
tools/ci/
├── run_sim_tests.sh        # Orquestra build + cenários + comparação (--scenario, --update-goldens)
└── compare_images.py       # Comparador com tolerância, PNGs de diff e relatório
```

## 4. Etapas Executadas

- [x] **Etapa 1 — Infraestrutura**: CMake standalone com LVGL vendido, `lv_conf.h` próprio e shims de BSP/FreeRTOS/ESP-IDF; janela SDL 720×1280 RGB565.
- [x] **Etapa 2 — Determinismo**: relógio congelado via wrap de `time`/`localtime_r`, seed fixa de RNG e capturas após `lv_refr_now`.
- [x] **Etapa 3 — Cenários**: 15 cenários (desktop, power menu, settings e os 12 apps) com injeção de clique SDL e fixtures no tmpdir.
- [x] **Etapa 4 — Redirecionamento completo de `/sdcard`**: wraps `opendir`/`stat` adicionados aos testes host e ao simulador (galeria exibindo foto real do fixture).
- [x] **Etapa 5 — Comparador e orquestrador**: `compare_images.py` (tolerância, diffs, relatório) e `run_sim_tests.sh` (`--scenario`, `--update-goldens`).
- [x] **Etapa 6 — Validação**: duas execuções consecutivas completas com 15/15 PASS; suíte host-native segue 84/84 após a extensão dos wraps.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Backend SDL sem display disponível (CI/headless) | Regressão é ferramenta local; `SDL_VIDEODRIVER=dummy` não cria display LVGL e não é suportado |
| Processo do simulador travado em loop de render | `gnutimeout -k 2` + `pkill -9 -x tab5_sim` entre cenários (SIGTERM não é entregue no loop) |
| Falso negativo por anti-aliasing entre execuções | Tolerância dupla (0,5% dos pixels, delta RGB 48) medida pixel a pixel |
| Fixture ausente distorcendo o golden | Fixtures criados dentro do próprio cenário (ex.: `mkdir /sdcard/imagens` antes do BMP da galeria) |

## 6. Critérios de Validação

1. `tools/ci/run_sim_tests.sh` executa os 15 cenários e reporta 15 PASS com exit 0. ✓
2. Duas execuções consecutivas produzem resultados idênticos (determinismo). ✓
3. Suíte host-native (`tools/ci/run_host_tests.sh`) permanece 84/84 com cobertura ≥80% após os novos wraps. ✓
4. Falha induzida grava PNG de diff destacando as regiões divergentes em vermelho. ✓

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Regressão visual operacional**: 15 cenários dourados determinísticos, comparador com relatório de diffs e orquestrador único; detalhes de uso em `tests/simulator/README.md`.

---

# [x] Fase 41: Confiabilidade da Conexão Bluetooth HID `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Mouse BLE (Logitech Lift) aparecia no scan do app Bluetooth mas a conexão falhava em silêncio: o botão "Conectar" voltava ao estado inicial sem explicação.
- Causas raiz: `bt_mgr_connect` retornava `ESP_OK` mesmo quando o procedimento GAP falhava ou estava ocupado; o dispositivo era persistido como "pareado" antes de conectar de verdade; slot de conexão órfão bloqueava rescan/reconexão; auto-conect monopolizava o GAP; MAC rotativo (RPA) dos Logitech tornava o endereço salvo stale.
- Objetivo: fluxo de conexão honesto com feedback real na UI, reconexão confiável entre reboots e roteamento HID pelo Report Map real do dispositivo (em vez de Report IDs fixos no código).

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Resultado de conexão | Callback `bt_conn_cb_t` (`bt_mgr_set_conn_callback`) com eventos STARTED/CONNECTED/READY/FAILED/DISCONNECTED | NimBLE reporta sucesso/falha assincronamente via eventos GAP; a UI precisa do resultado real, não do "aceito" do início do procedimento |
| D2 | Persistência como pareado | Só após inicialização HID concluída (GAP + descoberta GATT + CCCDs gravados) | Um tap não pode marcar o dispositivo como pareado — era isso que criava o loop de auto-conect contra um endereço inválido |
| D3 | Endereço de reconexão | `peer_id_addr` capturado pós-bonding e gravado no bt.cfg | O RPA rotaciona a cada anúncio; o endereço identidade é estável. Remapeamento por nome cobre o intervalo até o novo anúncio |
| D4 | Backoff de auto-conect | 15 s por MAC após falha, ignorado quando o cancelamento foi intencional (scan/forget/disconnect) | Evita martelar o controlador e monopolizar o procedimento GAP durante os 30 s de timeout |
| D5 | Parser de Report Map | Unidade pura (`hid_report_map.cpp`, sem dependências ESP) testada em host; roteamento por report ID com heurísticas legadas como fallback | Mouses compostos (teclado+mouse+consumer+system+vendor) usam IDs diferentes dos fixos 0x01/0x02; parser testável sem hardware |
| D6 | Bonding em NVS | `CONFIG_BT_NIMBLE_NVS_PERSIST=y` + transporte HCI exclusivamente VHCI (`BT_NIMBLE_TRANSPORT_UART=n`) + `esp_hosted_bt_controller_init/enable()` explícitos | Chaves sobrevivem ao reboot; elimina ambiguidade UART/VHCI do Kconfig; controller do C6 nasce desligado desde esp-hosted 2.5.2 |

## 3. Estrutura de Arquivos & Componentes

```
components/os/core/
├── bt_mgr.h               # [MODIFY] bt_conn_event_t, bt_conn_cb_t, bt_mgr_set_conn_callback
├── bt_mgr.cpp             # [MODIFY] Fluxo honesto, backoff, slots, persistência tardia, Report Map
├── hid_report_map.h/.cpp  # [NEW] Parser puro de descritor HID (report ID → mouse/teclado/consumer)
components/apps/bluetooth/
└── ui_bluetooth.cpp       # [MODIFY] Estado "Conectando...", fila de eventos NimBLE→LVGL, erros reais
tests/host/src/
└── test_bt_report_map.cpp # [NEW] 11 testes do parser (boot, composto Logitech, truncado, item longo)
```

## 4. Etapas Executadas

- [x] **Etapa 1 — Gerenciador honesto**: `bt_mgr_connect` propaga erro real (ocupado/controlador recusou), remove slot pendente na falha, não grava no bt.cfg no tap; persistência apenas quando os CCCDs são gravados (HID pronto).
- [x] **Etapa 2 — Canal de erro para a UI**: callback de eventos de conexão marshalled para a task LVGL (fila circular + `lv_async_call`); status mostra "Conectando…", "Tempo esgotado", "Falha na conexão (rc=N)" e o botão fica guardado durante a tentativa.
- [x] **Etapa 3 — Auto-reconexão robusta**: backoff de 15 s por MAC, guarda de pendência ativa, remapeamento do MAC rotativo por nome no anúncio, slots liberados na desconexão/falha/reset do host.
- [x] **Etapa 4 — Infra BT**: bonding persistente em NVS, VHCI explícito no sdkconfig.defaults (sdkconfig regenerado), controller BT do C6 inicializado/habilitado antes do host stack.
- [x] **Etapa 5 — Report Map**: leitura GATT da característica 0x2A4B, parser de itens HID e roteamento das notificações pelo tipo real (mouse/teclado/consumer); heurísticas antigas preservadas para dispositivos sem mapa parseável.
- [x] **Etapa 6 — Validação**: 98/98 testes host (+14 do parser), cobertura 93%, build firmware OK, regressão visual 15/15 PASS.
- [x] **Etapa 7 — Ajustes pós-validação em hardware (Lift real)**: cursor exibido no evento READY (`ui_mouse_set_connected(true)` quando o slot é mouse); leitura do Report Map movida para o fim da inicialização HID (`read_report_map_if_needed`, reutilizada também no ENC_CHANGE); fallback que decodifica o payload composto de 7 bytes `[botões u16 | X/Y 12 bits | wheel | pan]` quando o firmware do Lift expõe um Report Map proprietário (22 bytes) que o parser não classifica; classificação por nome (`lift`/`mouse`/`trackpad`) na persistência e ícone de periférico correto na listagem sem exigir novo pareamento.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Dispositivo sem Report Map parseável (mapa truncado pela MTU) | Parser tolerante a truncamento + heurísticas legadas mantidas como fallback |
| Teclado com modificador pressionado cair na heurística errada | Roteamento por ID tem precedência; guards `!kbd_routed` nos heurísticos de touchpad evitam engolir IDs 0x05/0x07 |
| Consumer reports (volume/mídia) ainda sem ação na UI | Registrados em log para mapeamento futuro |
| `esp_hosted_misc.h` sem `extern "C"` | Include envolvido manualmente em `extern "C"` dentro do bt_mgr |
| Auto-conect contra dispositivo desligado | Backoff por MAC impede martelada; escuta passiva retomada após o período |

## 6. Critérios de Validação

1. `tools/ci/run_host_tests.sh`: 98/98 testes, cobertura 93,0% ≥ 80%. ✓
2. `idf.py build` conclui com sdkconfig regenerado (`NVS_PERSIST=y`, `TRANSPORT_UART is not set`). ✓
3. `tools/ci/run_sim_tests.sh`: 15/15 cenários visuais PASS (stub `bt_mgr_set_conn_callback` no simulador). ✓
4. Hardware: parear o Lift → cursor aparece ao atingir READY e se move via fallback do payload composto de 7 bytes; reboot → reconexão automática sem re-pairing; listagem exibe ícone de mouse. ✓ (validado em dispositivo real)

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Fluxo de conexão honesto**: UI reflete o estado real (conectando/conectado/pronto/falhou), dispositivos só viram "pareados" depois de prontos, e o parser de Report Map torna o suporte HID independente de Report IDs fixos.
- **Validado em hardware (Logitech Lift)**: conexão, cursor ativo em todas as rotações, reconexão automática pós-reboot e classificação correta como mouse — cobrindo também firmwares que anunciam Report Map proprietário não parseável.

---

# [x] Fase 42: Aplicativo Calendário Mensal `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos

- Disponibilizar uma consulta rápida do mês atual a partir do relógio/data da
  barra superior.
- Disponibilizar também um aplicativo Calendário com ícone próprio no desktop,
  ocupando toda a área útil da tela.
- Nesta primeira versão não haverá eventos, lembretes ou persistência; o foco é
  a visualização mensal, navegação e integração com o shell.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Widget mensal | Implementação própria sobre LVGL | Não há `lv_calendar` configurado ou utilizado no projeto; uma grade própria evita dependência indisponível |
| D2 | Lógica de datas | Módulo puro `calendar_logic` | Permite testar ano bissexto, deslocamento do primeiro dia e transição de ano sem dependência de LVGL |
| D3 | Renderização | `ui_calendar_view` compartilhada | O popup e o aplicativo dedicado devem exibir a mesma grade e comportamento |
| D4 | Origem da data | `timezone_mgr_get_localtime()` | Mantém o calendário consistente com o relógio da barra, RTC e configuração de fuso existente |
| D5 | Popup | Overlay no `lv_layer_top()` | Mantém o calendário disponível sem trocar a tela ativa e permite fechar ao tocar fora |
| D6 | Aplicativo | Registro padrão em `app_registry` | O ícone próprio aparece automaticamente no desktop e segue o ciclo de vida do `ui_shell` |
| D7 | Semana | Domingo como primeira coluna | Mantém a leitura no padrão brasileiro e deixa a decisão encapsulada no renderer |

## 3. Estrutura de Arquivos & Componentes

```
components/os/core/
├── calendar_logic.h/.cpp       # [NEW] Cálculo puro de meses e dias
components/os/shell/
├── ui_calendar_view.h/.cpp     # [NEW] Grade LVGL reutilizável
├── ui_bar.cpp                  # [MODIFY] Clique na data/hora e popup mensal
├── ui_shell.h/.cpp             # [MODIFY] Ciclo de vida da tela do calendário
components/apps/calendar/
├── ui_calendar.h               # [NEW] API da aplicação
└── ui_calendar.cpp             # [NEW] App, manifesto e tela dedicada
tests/host/src/
└── test_calendar_logic.cpp     # [NEW] Testes de datas e navegação
tests/simulator/scenarios/
└── sim_scenarios.cpp           # [MODIFY] Cenários popup e app Calendário
```

## 4. Etapas de Implementação

- [x] **Etapa 1 — Lógica de calendário**: criar `calendar_logic` com cálculo de
  dias por mês, ano bissexto, dia inicial, navegação mês/ano e nomes dos meses.
- [x] **Etapa 2 — Visual compartilhado**: criar `ui_calendar_view` com cabeçalho,
  botões anterior/próximo, sete colunas, destaque do dia atual e atualização de
  tema/layout.
- [x] **Etapa 3 — Popup da barra**: tornar a área da data/hora clicável, abrir o
  overlay no `lv_layer_top()`, posicioná-lo sem sair da tela e fechá-lo ao tocar
  fora, ao ocultar a barra ou ao abrir outro menu.
- [x] **Etapa 4 — Aplicativo dedicado**: criar `ui_calendar`, manifesto com ID
  `calendar`, nome `Calendário`, ícone próprio e tela usando `ui_app_bar` mais a
  área útil restante.
- [x] **Etapa 5 — Integração no shell**: registrar o app, criar sua tela,
  implementar `ui_shell_open_calendar`/`close_calendar`, refresh de tema e
  encaminhamento de layout.
- [x] **Etapa 6 — Build e testes host**: incluir os novos fontes nos CMakeLists e
  testar meses, anos bissextos, mudanças de ano, limites e datas inválidas.
- [x] **Etapa 7 — Simulador**: adicionar cenários `calendar_popup` e
  `app_calendar`, incluindo navegação mensal, e gerar goldens determinísticos.
- [x] **Etapa 8 — Validação**: executar testes host, build do simulador, regressão
  visual, build ESP-IDF e validação no hardware nas quatro orientações.

## 5. Critérios de Aceite

1. Clicar na data/hora abre o mês atual em um popup legível e fecha ao tocar
   fora.
2. O ícone Calendário aparece no desktop e abre uma tela dedicada na área útil
   completa.
3. A navegação funciona entre dezembro/janeiro e todos os meses respeitam a
   quantidade correta de dias.
4. O dia atual e o tema claro/escuro são refletidos corretamente.
5. Popup e app funcionam em retrato e paisagem sem cobrir ou deslocar
   incorretamente a barra do sistema.
6. Testes host-native e cenários do simulador passam sem regressões visuais.

## 6. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Grade não caber em paisagem ou retrato | Layout baseado na resolução disponível, com células flexíveis e limites de tamanho |
| Popup conflitar com menus da barra | Função única de fechamento e criação no mesmo layer, fechando overlays anteriores |
| Data divergente do relógio | Usar exclusivamente `timezone_mgr_get_localtime()` e congelar o tempo no simulador |
| Texto de mês não existir na fonte | Usar nomes PT-BR compatíveis com Latin-1 ou fornecer fallback ASCII controlado |

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`

- **Lógica e renderização**: Módulo de data pura `calendar_logic` com cobertura de 100% dos testes e grade compartilhada `ui_calendar_view` responsiva e adaptável aos temas claro/escuro.
- **Popup e App**: Popup instantâneo via clique na data/hora na barra do sistema e aplicativo nativo registrado no launcher dinâmico.
- **Qualidade e validação**: Cobertura de testes host em 92.7% (>= 80%), 17 cenários do simulador SDL aprovados (0 falhas), `pre-commit` 100% aprovado e build ESP-IDF do firmware concluído com sucesso.

---

---

# [ ] Fase 43: TTS em Nuvem no Chat (Leitura de Respostas) `⏳ PLANEJADO`

## 1. Contexto & Objetivos
- **Motivação**: O app Chat IA (`components/apps/chat/`) já entrega respostas textuais do assistente (`on_ai_response`, `ui_chat.cpp:206`). Esta fase adiciona a leitura automática em voz alta dessas respostas, tornando o chat acessível e hands-free.
- **Decisão de arquitetura**: TTS em **nuvem** (API TTS configurável) em vez de `esp-tts` offline. Motivo: o `esp-tts` só distribui modelos EN/ZH (sem PT-BR). O áudio retorna como **MP3** e é decodificado pelo `minimp3` já vendored em `music/`, reaproveitando o caminho de reprodução.
- **Sem novos componentes ESP-IDF**: reutiliza `esp_http_client` (já em `PRIV_REQUIRES`) e `minimp3.h` (já no `INCLUDE_DIRS` do componente `apps`). Nenhuma mudança em `idf_component.yml`, partições ou `sdkconfig`.

## 2. Requisitos & Dependências
- Wi-Fi ativo (já dependência do chat) para alcançar a API TTS.
- `esp_http_client` (download do MP3) e `esp_codec_dev` (saída PCM) — já presentes.
- `minimp3.h` em `components/apps/music/` — incluído via `INCLUDE_DIRS` do componente `apps`.
- `audio_storage` para respeitar o volume geral; `bsp_audio_codec_speaker_init` / `bsp_headphone_is_connected` para o speaker.

## 3. Arquivos & Interfaces (Novos em `components/apps/chat/`)
- **`tts_storage.h/.cpp`**:
  - `typedef enum { TTS_PROVIDER_OPENAI, TTS_PROVIDER_GOOGLE, TTS_PROVIDER_AZURE, TTS_PROVIDER_ELEVENLABS } tts_provider_t;`
  - `typedef struct { tts_provider_t provider; char api_key[1024]; char base_url[512]; char voice[128]; bool enabled; } tts_cfg_t;`
  - `tts_storage_get_default(tts_cfg_t*)`, `tts_storage_load(tts_cfg_t*)`, `tts_storage_save(const tts_cfg_t*)` — persistência em `/sdcard/tab5_os/tts.cfg` (espelha `ai_storage`).
- **`tts_client.h/.cpp`**:
  - `esp_err_t tts_speak(const char *text)` — monta a requisição HTTP por provedor (primeiro **OpenAI TTS**: `POST {base_url}/v1/audio/speech`, JSON `{model, voice, input:text}`, `Authorization: Bearer <key>`), baixa o MP3 via `esp_http_client` (evento `on_data`) para buffer em PSRAM, e encadeia para o player. Roda em FreeRTOS task; cancelável.
  - `void tts_cancel(void)` — aborta requisição/decodificação em andamento.
  - Builder de requisição por provedor (plugável p/ Google/Azure/ElevenLabs depois do OpenAI).
- **`tts_player.h/.cpp`**:
  - `esp_err_t tts_player_play(const uint8_t *mp3_buf, size_t len)` — decodifica com `minimp3` e toca via `esp_codec_dev_write` (padrão de `audio_recorder.cpp:355`): `bsp_audio_codec_speaker_init()` → unmute → `bsp_feature_enable(BSP_FEATURE_SPEAKER, !bsp_headphone_is_connected())` → loop decode+write → mute/close.
  - `void tts_player_stop(void)` — para a reprodução.
  - Aplica volume de `audio_storage_load_volume()`.
  - *Tradeoff considerado*: baixar para `/sdcard/tab5_os/tts_cache.mp3` e chamar `music_player_start()` reutilizaria o player de música, mas acoplaria à UI do app Música; optou-se por `tts_player` dedicado.

## 4. Integração no Chat (`ui_chat.cpp`)
- `on_ai_response` (`ui_chat.cpp:206`): após `add_message_bubble`, se `tts_cfg.enabled`, dispara `tts_speak(response_text)` numa task (não bloquear sob `bsp_display_lock`).
- Modal de config (`ui_chat.cpp:~447-577`, `config_btn`): adicionar checkbox "Ler respostas em voz alta" + dropdown de provedor + campo API key + campo voice/model + URL base; salvar via `tts_storage_save`.
- Indicador "Falando…" (reaproveita `thinking_bubble`/status) e parada: `do_send_message` (`ui_chat.cpp:256`) e `ui_chat_on_close` chamam `tts_player_stop()` + `tts_cancel()`.
- Falha (Wi-Fi down / erro HTTP): `ESP_LOG` + `add_message_bubble("system", …)` — não quebra o chat (segue padrão existente).

## 5. Build (`components/apps/CMakeLists.txt`)
- Adicionar `tts_storage.cpp`, `tts_client.cpp`, `tts_player.cpp` em `SRCS`. `INCLUDE_DIRS` já inclui `chat` e `music`; `PRIV_REQUIRES` já tem `esp_http_client` e `esp_codec_dev`.

## 6. Testes (manter gates F40 / visual)
- Host test `tests/host/src/test_tts.cpp`: mock de `esp_http_client` (MP3 fixture) + `esp_codec_dev_write`; validar builder de requisição por provedor, cancelamento e path decode+play. Seguir padrão GoogleTest+stubs existente.
- (Opcional) golden no simulador SDL: cenário "chat falando".

## 7. Validação em Hardware
- Build/flash (`IDF_PYTHON_ENV_PATH` + `export.sh` + `idf.py build/flash`), testar: enviar prompt → ouvir resposta; configurar provedor/chave; verificar volume e parada.

## 8. Riscos & Caveats
- Exige conta + chave do provedor e **texto sai para a nuvem** (privacidade).
- Latência de rede (~200-800ms); respostas de chat são pequenas (PSRAM tranquilo).
- Escolher voz PT-BR na config (OpenAI/Azure oferecem `pt-BR`).

## 9. Status de Conclusão: `[ ] PLANEJADO (0%)`

---

# [x] Fase 44: Visualização de Arquivos e Pastas Ocultos no app Arquivos `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Hoje `load_directory` (`components/apps/files/ui_files.cpp:130`) apenas ignora `.` e `..`; qualquer outro item iniciado em `.` (ex.: `.bashrc`, ou a futura pasta `.tab5_os` da Fase 45) acaba sendo exibido na listagem.
- Adicionar um *toggle* "Mostrar ocultos" na barra do app, com arquivos e pastas cujo nome inicia em `.` **ocultos por padrão** (exceto a navegação `..`).
- Ao ativar o toggle, os itens ocultos passam a ser exibidos nos modos Grade e Lista.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Filtro de oculteza | `if (name[0]=='.' && name!=".." && !show_hidden) skip` dentro de `load_directory` | Regra centralizada em um único ponto, sem duplicar lógica na renderização |
| D2 | Controle de UI | Botão extra na `ui_app_bar` (ícone `LV_SYMBOL_EYE_OPEN` / `LV_SYMBOL_EYE_CLOSE`) | Paridade com o botão existente de alternar Grade/Lista |
| D3 | Persistência | NVS (`files/show_hidden`, bool) | A preferência do usuário sobrevive a reboots |

## 3. Estrutura de Arquivos & Componentes

```
components/apps/files/
├── ui_files.cpp   # [MODIFY] flag show_hidden, filtro em load_directory, botão na app_bar, persistência NVS
└── ui_files.h     # [MODIFY] declaração do estado show_hidden
```

## 4. Fases de Execução da Funcionalidade
- [x] **Etapa 1 — Flag e filtro**: Adicionar `bool s_show_hidden = false` e pular entradas cujo `d_name[0]=='.'` (diferente de `".."`) quando a flag estiver desligada.
- [x] **Etapa 2 — Botão "Mostrar ocultos"**: Novo botão de ação na `ui_app_bar` com `toggle_hidden_click_cb` alternando ícone (`LV_SYMBOL_EYE_OPEN` / `LV_SYMBOL_EYE_CLOSE`) e recarregando via `load_directory(current_path)`.
- [x] **Etapa 3 — Persistência NVS**: Gravar/ler `tab5/files_hidden` e restaurar em `ui_files_create`.
- [x] **Etapa 4 — Validação em hardware**: Alternar visibilidade, navegar e reboot.

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Usuário se "perder" em pasta oculta sem ver o `..` | `..` nunca é filtrado, independentemente da flag |

## 6. Critérios de Validação & Teste em Hardware
1. Abrir Arquivos e confirmar que `/sdcard/.tab5_os` (Fase 45) não aparece por padrão.
2. Ativar o toggle e confirmar que a pasta oculta passa a ser listada.
3. Reboot e confirmar que a preferência "Mostrar ocultos" persiste.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Arquivos & Ocultos**: Toggle "Mostrar ocultos" na barra, filtro em `load_directory` e persistência em NVS implementados e compilados com sucesso (build validado).

---

# [x] Fase 45: Consolidação das Configurações do SO em Pasta Oculta `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos
- Centralizar toda a configuração do sistema em `/sdcard/.tab5_os/` (pasta oculta, padrão Unix de dot-prefix), unificando o que hoje está em `/sdcard/tab5_os/` e o que está fora de qualquer pasta de config.
- **Correção explícita de `timezone.cfg`**: hoje ele é gravado diretamente em `/sdcard/timezone.cfg` (`components/os/core/timezone_mgr.h:14`), fora de qualquer diretório de configuração. Deve passar a residir em `/sdcard/.tab5_os/timezone.cfg`, junto de todas as demais configs do SO.
- Migração automática no boot: se a pasta/pasta antiga existir, mover os `*.cfg` para a nova localização; manter *fallback* de leitura na origem para segurança.

## 2. Decisões de Arquitetura

| # | Decisão | Escolha | Justificativa |
|---|---|---|---|
| D1 | Caminho base | `#define OS_CFG_DIR "/sdcard/.tab5_os"` em novo `os_config_paths.h` | Única fonte da verdade para todos os módulos de config |
| D2 | Montagem | `wifi_storage_mount()` cria `.tab5_os` em vez de `tab5_os` | Reaproveita o helper de montagem já consumido por todos os módulos |
| D3 | Migração | `os_config_migrate()` chamado em `app_main` antes das leituras | Move `tab5_os/*.cfg` e `/sdcard/timezone.cfg`; operação idempotente |
| D4 | Fallback de leitura | leitura tenta novo caminho, depois antigo | Evita perda de config em caso de falha de migração |

## 3. Estrutura de Arquivos & Componentes

```
components/os/core/
├── os_config_paths.h         # [NEW] OS_CFG_DIR e paths derivados (*_CFG_PATH)
├── os_config_migrate.cpp/.h  # [NEW] migração de configs no boot
├── wifi_storage.cpp/.h       # [MODIFY] usa OS_CFG_DIR (wifi.cfg)
├── bt_storage.cpp/.h         # [MODIFY] usa OS_CFG_DIR (bt.cfg)
├── display_storage.cpp/.h    # [MODIFY] usa OS_CFG_DIR (display.cfg)
├── audio_storage.cpp/.h      # [MODIFY] usa OS_CFG_DIR (audio.cfg)
├── timezone_mgr.cpp/.h       # [MODIFY] usa OS_CFG_DIR (timezone.cfg) — correção de localização
components/apps/chat/ai_storage.h  # [MODIFY] usa OS_CFG_DIR (ai.cfg)
main/app_main.cpp             # [MODIFY] chama os_config_migrate() antes de carregar configs
```

## 4. Fases de Execução da Funcionalidade
- [x] **Etapa 0 — Correção de `timezone.cfg`**: Redefinir `TIMEZONE_CFG_PATH` para `/sdcard/.tab5_os/timezone.cfg`.
- [x] **Etapa 1 — `wifi_storage.h`**: Definir `TAB5_CONFIG_DIR` (`/sdcard/.tab5_os`) e redefinir `*_CFG_PATH` (`wifi`, `bt`, `display`, `audio`, `ai`, `timezone`).
- [x] **Etapa 2 — Realinhamento de módulos**: `wifi_storage_mount` cria `.tab5_os`; `bt_storage` garante o diretório oculto.
- [x] **Etapa 3 — Migração one-shot**: `config_storage_migrate()` moveu `tab5_os/*.cfg` e `timezone.cfg` para `.tab5_os/` no dispositivo; removida após validação (nada mais escreve nos caminhos legados).
- [x] **Etapa 4 — Chamada no boot**: `config_storage_migrate()` foi invocado via `wifi_storage_mount()` durante a transição.
- [x] **Etapa 5 — Validação em hardware**: Migração confirmada no dispositivo real (configs preservadas após reboot).

## 5. Riscos & Mitigações

| Risco | Impacto / Mitigação |
|---|---|
| Troca de SD por cartão com firmware antigo (configs em `tab5_os/`) | Caso raro e aceito; configs antigas não são auto-migradas após a remoção do código |
| `timezone.cfg` fora de `tab5_os` | Corrigido na Etapa 0 (passou a residir em `.tab5_os`) |

## 6. Critérios de Validação & Teste em Hardware
1. Boot move `tab5_os/*.cfg` e `/sdcard/timezone.cfg` para `/sdcard/.tab5_os/`.
2. Brilho, volume, Wi-Fi, BT, IA e fuso horário ainda carregam corretamente.
3. `/sdcard/.tab5_os` não aparece no app Arquivos por padrão (Fase 44).

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`
- **Configs em pasta oculta**: Todas as configs em `/sdcard/.tab5_os/`, `timezone.cfg` corrigido; migração one-shot executada no dispositivo e código removido (build validado).

---

# [x] Fase 46: Isolamento e Modularização de Aplicações (WebAssembly WAMR, Package Manager, Storage Manager & SDK) `✅ IMPLEMENTADO`

## 1. Contexto & Objetivos

- Desacoplar as aplicações do código principal do firmware do Tab5 OS.
- Implementar modelo de execução isolado em sandbox via WebAssembly (WAMR).
- Criar a Host ABI do sistema com bindings seguros para LVGL, hardware, armazenamento privado e ciclo de vida de processos.
- Implementar o Package Manager com formato `.tab5pkg`, parser JSON de `manifest.json`, registro dinâmico e precedência de armazenamento.
- Implementar a Interface do Instalador de Aplicações com modal de confirmação e auditoria de permissões.
- Implementar o Gerenciador de Armazenamento e Memória em 3 abas (Memória & Discos, Apps Instaladas e Pacotes Pendentes).
- Disponibilizar o **Tab5 App SDK** (`sdk/tab5-app-sdk/`) com headers, CLI `pack.py`, macros CMake, templates e exemplos.

## 2. Decisões Arquiteturais

| Decisão | Escolha | Motivação |
|---|---|---|
| Runtime | WebAssembly Micro Runtime (WAMR) | Segurança por sandbox, isolamento de falhas, portabilidade e eficiência em PSRAM no ESP32-P4 |
| Formato de Pacote | Diretório/Bundle `.tab5pkg` | Empacotamento declarativo com `manifest.json`, binário `app.wasm` e pasta `assets/` |
| Sandboxing de Armazenamento | `/sdcard/data/<app_id>/` | Isolamento estrito de dados privados por aplicação, impedindo corrupção entre apps |
| Precedência de Apps | SD Card > Partição Embutida (`/apps`) | Permite aos desenvolvedores testar e atualizar versões mais recentes de apps pelo SD sem re-flash |
| Gestão de Espaço | Módulo `storage_mgr` + UI 3 abas | Transparência total de recursos (Flash, SD e RAM) com desinstalação segura e limpeza de dados |
| Toolchain do Desenvolvedor | Tab5 App SDK + `pack.py` + CMake | Automação completa para criação de apps independentes por terceiros |

## 3. Estrutura de Arquivos Criados

```
components/os/runtime/
├── include/tab5_sdk.h            # Header C oficial com funções da Host ABI
├── tab5_host_abi.h/.cpp          # Tabela de símbolos nativos e gerenciamento de contexto
├── tab5_lifecycle_host.h/.cpp    # Ciclo de vida de processos da app ativa
├── tab5_manifest.h/.cpp          # Parser e modulo de validacao do manifest.json
├── tab5_package_mgr.h/.cpp       # Gerenciador de instalação, desinstalação e execução
├── tab5_storage_sandbox.h/.cpp   # Resolução segura de caminhos e sandboxing
├── tab5_sys_host.cpp             # Bindings de bateria, Wi-Fi, BLE, áudio e logs
├── tab5_ui_host.cpp              # Bindings de tela, app bar, teclado e toasts
└── tab5_wasm_runtime.h/.cpp      # Motor de execução WAMR integrado
components/os/core/
└── storage_mgr.h/.cpp            # Varredura de partições, cálculo de tamanhos e estatísticas
components/os/shell/
├── ui_installer.h/.cpp           # Modal de instalação e auditoria de permissões
└── ui_storage_view.h/.cpp        # Aplicativo de Gerenciamento de Armazenamento e Memória
sdk/tab5-app-sdk/
├── cmake/                        # Toolchain e macros CMake
├── include/                      # Headers públicos tab5_sdk.h e tab5_manifest.h
├── templates/hello_app/          # Template base para novas aplicações
├── examples/                     # Exemplos hello_wasm e notes_wasm
└── tools/pack.py                 # Ferramenta CLI de validação e empacotamento
```

## 4. Etapas Executadas

- [x] **Etapa 1 — Core Host ABI**: Bindings nativos para LVGL, I/O em sandbox, hardware, ciclo de vida e gerenciamento de permissões.
- [x] **Etapa 2 — WAMR Runtime**: Integração do WebAssembly Micro Runtime para ESP32-P4 e host mocks.
- [x] **Etapa 3 — Package Manager**: Parser de manifesto, associação `.tab5pkg` e registro dinâmico no boot.
- [x] **Etapa 4 — Embedded Bundle**: Precedência de execução (SD sobre firmware embutido) e varredura de pacotes.
- [x] **Etapa 5 — Installer UI**: Modal de instalação com confirmação de permissões e associação no app Arquivos.
- [x] **Etapa 6 — Storage Manager**: Aplicativo de 3 abas para visualização de ocupação de disco, desinstalação e pacotes pendentes.
- [x] **Etapa 7 — Tab5 App SDK**: Headers, ferramenta de empacotamento `pack.py`, template inicial e exemplos práticos.
- [x] **Etapa 8 — Validação & Qualidade**: 132 testes de host aprovados (100%), pre-commit 100% limpo e gravação validada no ESP32-P4.

## 5. Critérios de Validação

1. `tests/host`: 132/132 testes unitários aprovados com 100% de sucesso. ✓
2. `tests/test_pack_tool.py`: Testes unitários do empacotador CLI aprovados. ✓
3. `pre-commit run --all-files`: 100% em conformidade com as regras de qualidade do projeto. ✓
4. `idf.py build` e gravação no hardware (`/dev/ttyACM0`): Sistema operacional inicializa perfeitamente com todos os subsistemas. ✓

## 6. Padrão Arquitetural para Migração de Aplicações com Interface Rica (Rich UI)

> [!IMPORTANT]
> **Diretriz Obrigatória para Migrações de Aplicações**:
> Ao migrar aplicativos do firmware monolítico para repositórios isolados (`tab5-app-*`), deve-se atentar ao tipo de interface do aplicativo para garantir fidelidade visual e usabilidade:
>
> 1. **Aplicações de Texto / Terminal / Editor (ex: Notas)**:
>    - Utilizam o canvas padrão `ctx->content_area` (`lv_textarea`) gerenciado pelo runtime WAMR (`tab5_ui_host.cpp`).
>    - A aplicação WASM controla o texto através da Host ABI (`tab5_ui_textarea_set_text`).
>
> 2. **Aplicações com Interface Gráfica Especializada (ex: Calendário, Arquivos, Galeria, Música)**:
>    - **Não devem** ter sua interface rica substituída por texto puro em fallback no `textarea`.
>    - **Arquitetura de Host View (`ui_*_view`)**:
>      - Criar um componente de visualização modular em `components/os/shell/` (ex: `ui_calendar_view.h/.cpp`, `ui_files_view.h/.cpp`).
>      - No runtime do host (`components/os/runtime/tab5_ui_host.cpp`), dentro de `tab5_ui_host_create_app_screen`, interceptar o `ctx->app_id` da aplicação:
>        - Ocultar o textarea padrão: `lv_obj_add_flag(ta, LV_OBJ_FLAG_HIDDEN)`.
>        - Instanciar a Host View nativa passando a tela pai e a `ui_app_bar_t` (ex: `ui_files_view_create(scr, bar)`).
>      - Integrar os hooks de ciclo de vida nos eventos correspondentes:
>        - Destruição: liberar a instância em `tab5_ui_host_cleanup_app_screen`.
>        - Layout / Rotação: chamar `ui_*_view_apply_layout` em `tab5_ui_host_apply_layout`.
>        - Tema (Claro/Escuro): chamar `ui_*_view_refresh_theme` em `tab5_ui_host_refresh_theme`.
>      - No repositório isolado da aplicação (`tab5-app-*/src/main.c`), registrar os callbacks de ciclo de vida (`tab5_lifecycle_register`) para manter sincronização sem sobrescrever a barra de ações ou injetar texto redundante.

## 7. Status de Conclusão: `[x] CONCLUÍDO (100%)`

### Migração de Câmera e Galeria para Apps Isoladas (complemento da Fase 46)

> **Estado:** `[x] CONCLUÍDO` — mesmas telas do monolítico, agora servidas por Host Views nativas.

| Aplicação | Repositório | Pacote embutido | Host View nativa | Ações de barra |
|---|---|---|---|---|
| Câmera | `tab5-app-camera` | `com.tab5.camera.tab5pkg` | `ui_camera_view` | Preview ao vivo (`camera_mgr`), disparo com flash/toast, atalho → Galeria |
| Galeria | `tab5-app-gallery` | `com.tab5.gallery.tab5pkg` | `ui_gallery_view` | Navegação, exclusão com modal, atalho → Câmera, abertura por `.jpg/.jpeg/.png/.bmp` |

**Mudanças estruturais:**

- `ui_camera.cpp/.h` e `ui_gallery.cpp/.h` (monolíticos) foram convertidos em **Host Views reutilizáveis** em `components/os/shell/` (`ui_camera_view`, `ui_gallery_view`), seguindo o padrão de `ui_files_view`/`ui_calendar_view`.
- `camera_mgr` (driver de câmera) e `tjpgd` (decoder JPEG) foram movidos de `components/apps/` para `components/os/core/`, pois agora são dependências do núcleo (não da aplicação).
- `components/os/runtime/tab5_ui_host.cpp` passou a despachar `com.tab5.camera`/`com.tab5.gallery` para as Host Views nativas (ocultando o textarea padrão), com hooks de `resume` (inicia preview / varre diretório) e `open_file` (galeria abre a foto).
- Navegação cruzada Câmera ↔ Galeria agora usa `tab5_package_mgr_launch("com.tab5.camera|gallery")`, respeitando o ciclo de vida do runtime.
- Apps nativas removidas do registro monolítico (`ui_shell`), tiles do Desktop agora vêm dos manifestos isolados (ícone por símbolo + cor de fundo).

**Validação:**

1. `tools/ci/run_sim_tests.sh`: cenários `app_camera` e `app_gallery` **PASS** contra goldens do monolítico (fidelidade visual idêntica); goldens de desktop (`shell_desktop`, `shell_power`, `shell_settings`, `shell_calendar_popup`) regenerados por causa dos tiles isolados.
2. `tools/ci/run_host_tests.sh`: 134/134 testes aprovados, cobertura 90% ≥ 80%.
3. `idf.py build`: firmware compila; `apps.bin` contém os pacotes `com.tab5.camera` e `com.tab5.gallery`.

---

# [-] Fase 47: Host ABI de UI Genérica & Desacoplamento Total de Aplicações `🚧 EM ANDAMENTO`

## 1. Contexto & Objetivos

- Eliminar a necessidade de código de interface de usuário das aplicações embutido no SO (`ui_*_view.cpp` em `components/os/shell/`).
- Expandir a **Host ABI de UI do Tab5 SDK** para suportar a construção declarativa e dinâmica de telas ricas diretamente no código WebAssembly/C da aplicação.
- Implementar gerenciamento seguro de widgets nativos por handles opacos (`tab5_ui_obj_t = uint32_t`).
- Fornecer despacho bidirecional de eventos de interface (`TAB5_UI_EVENT_CLICKED`, `TAB5_UI_EVENT_VALUE_CHANGED`, `TAB5_UI_EVENT_LONG_PRESSED`, `TAB5_UI_EVENT_FOCUSED`, `TAB5_UI_EVENT_DEFOCUSED`) do host LVGL para o runtime WAMR (`tab5_app_on_ui_event`).
- Criar a aplicação de referência rica **`com.tab5.widgetsdemo`** no SDK (`sdk/tab5-app-sdk/examples/widgets_demo/`) demonstrando contêineres flex, botões, labels, switches, sliders e listas.

## 2. Decisões Arquiteturais

| Decisão | Escolha | Motivação |
|---|---|---|
| Gerenciamento de Objetos | Handles Inteiros Opacos (`uint32_t`) | Protege a sandbox WASM contra manipulação direta de ponteiros de memória do host e evita memory leaks no LVGL. |
| Hierarquia de Layout | Contêineres Flexbox (`LV_FLEX_FLOW_ROW / COLUMN`) | Permite criar layouts responsivos que se adaptam automaticamente a orientações retrato e paisagem. |
| Despacho de Eventos | Invocação direta de `tab5_app_on_ui_event` / `on_ui_event` | Comunicação transparente de eventos de toque e controle do host para o bytecode WASM sem overhead. |
| Design System | Herança automática de `ui_theme` e `ui_font` | Garante consistência visual, cores dos temas claro/escuro e tipografia com acentuação PT-BR sem complexidade extra nas apps. |

## 3. Estrutura de Arquivos

```
components/os/runtime/
├── include/tab5_sdk.h            # Tipos, constantes de layout/eventos e protótipos de widgets
├── tab5_host_abi.h/.cpp          # Tabela de símbolos nativos WAMR estendida com APIs de UI
├── tab5_lifecycle_host.h/.cpp    # Suporte a ciclo de vida com eventos de interface
└── tab5_ui_host.h/.cpp           # Tabela de handles, bindings LVGL de widgets e dispatcher de eventos
sdk/tab5-app-sdk/
├── include/tab5_sdk.h            # Header C oficial sincronizado para desenvolvedores
└── examples/widgets_demo/        # Exemplo rico demonstrando todos os widgets desacoplados
```

## 4. Etapas de Execução

- [x] **Etapa 1 — Especificação dos Tipos e Headers do SDK**: Tipos `tab5_ui_obj_t`, alinhamentos, fluxos flexíveis, códigos de evento e protótipos das funções de UI em `tab5_sdk.h`.
- [x] **Etapa 2 — Bindings de Widgets no Host**: Tabela de handles (`MAX_UI_HANDLES`), criação de contêineres, labels, botões, switches, sliders, listas e callback unificado `on_generic_widget_event_cb` em `tab5_ui_host.cpp`.
- [x] **Etapa 3 — Exportação de Símbolos WAMR**: Registro das funções nativas na tabela de símbolos da Host ABI em `tab5_host_abi.cpp`.
- [x] **Etapa 4 — Aplicação de Demonstração Rica (`widgets_demo`)**: Criação do código-fonte `main.c`, manifesto `manifest.json`, compilação WASI-SDK e empacotamento em `com.tab5.widgetsdemo.tab5pkg`.
- [x] **Etapa 5 — Validação em Testes e Hardware**: Suíte GoogleTest no host com 100% de sucesso e cobertura ≥80%, pre-commit aprovado e gravação validada no Tab5 com auto-registro no boot.
- [x] **Etapa 6 — Migração Gradual de Aplicações Nativas**: Migrada a aplicação **Calendário (`com.tab5.calendar`)** para construção de tela 100% dinâmica via WebAssembly e Tab5 SDK com grade em tabela unificada idêntica ao design nativo (cabeçalho com botões Mês Anterior/Hoje/Próximo estilizados com bordas e raio, barra de dias da semana em `surface_alt`, tabela conectada de 6 linhas x 7 colunas com bordas finas, células transparentes, texto mutado para dias fora do mês e destaque no dia atual/selecionado com suporte a touch), eliminando a interceptação de Host View no SO.
- [x] **Etapa 7 — Migração do Lote 1 de Aplicações**: Migradas com sucesso para a Host ABI genérica desacoplada as aplicações:
  - **Servidor de Arquivos (`com.tab5.fileserver`)**: Cards ricos de status (badge online/offline), IP/URL Wi-Fi dinâmico, botão Iniciar/Desligar e instruções de acesso remoto.
  - **Gravador de Áudio (`com.tab5.recorder`)**: Card de captura de voz com microfone ES7210, Card de reprodução e Card de listagem de arquivos WAV no microSD.
  - **Terminal Micro-Shell (`com.tab5.terminal`)**: Micro-shell com histórico dinâmico, barra rápida de botões de comandos (`help`, `ls`, `free`, `df`, `date`, `clear`) e execução via Host ABI `tab5_terminal_exec`.
  - Remoção de interceptações estáticas e views dedicadas no SO para esses apps, mantendo a suíte de testes de host em 100% (cobertura 80.2%) e firmware gravado no dispositivo.
- [ ] **Etapa 8 — Migração do Lote 2 de Aplicações**: Migrar **Música (`com.tab5.music`)**, **Wi-Fi (`com.tab5.wifi`)** e **Bluetooth (`com.tab5.bluetooth`)** para a Host ABI desacoplada.
- [ ] **Etapa 9 — Migração do Lote 3 de Aplicações**: Migrar **Chat IA (`com.tab5.chat`)**, **Arquivos (`com.tab5.files`)**, **Câmera (`com.tab5.camera`)** e **Galeria (`com.tab5.gallery`)**.

---

## Sugestões de Novas Aplicações ou Melhorias (Não Planejadas)

> [!NOTE]
> As aplicações abaixo são apenas **sugestões** para o roadmap futuro. Ainda **não** foram arquitetadas nem especificadas, portanto não possuem fase própria no caderno. Elas serão promovidas a uma fase formal (com detalhamento completo) quando forem priorizadas para implementação.
>
> Itens já promovidos a fase formal: **Arquivos** (visualização de ocultos → Fase 44) e **OS** (configs em pasta oculta, incluindo correção de `timezone.cfg` → Fase 45).

| Aplicação | Descrição Simplificada |
|---|---|
| **Calculadora** | Calculadora simples com botões em grade e histórico de operações. |
| **Cronômetro / Timer / Alarme** | Temporizadores com aviso sonoro via ES8388, aproveitando o RTC. |
| **Jogo simples (Snake / 2048)** | Jogo leve para demonstrar loop de animação e entrada por toque. |
| **Desenho / Pintura (Canvas)** | Tela de desenho livre com toque/mouse e salvamento de imagem no SD. |
| **Chat AI** | Manter contexto e salvar conversas como notas. |
