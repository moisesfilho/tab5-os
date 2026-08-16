# Caderno Geral de Arquitetura e Planos de Implementação — tab5-os

Documento mestre de planejamento técnico, decisões de engenharia, arquitetura de software e roadmap evolutivo do sistema operacional **tab5-os** para o dispositivo **M5Stack Tab5 (ESP32-P4 + ESP32-C6 companion)**.

---

## Legenda de Marcações de Implementação

- `[x]` **`✅ IMPLEMENTADO`**: Funcionalidade codificada, integrada, testada com `pre-commit` e validada em hardware real.
- `[-]` **`🚧 EM ANDAMENTO`**: Funcionalidade com desenvolvimento em progresso ou testes parciais.
- `[ ]` **`⏳ PLANEJADO`**: Funcionalidade arquitetada e especificada, aguardando início de implementação.

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
| `[ ]` | **Fase 21** | Cliente SSH Remoto no Terminal | Conectividade / CLI | Task FreeRTOS, `david-cermak/libssh`, PTY xterm, auth por senha/chave |

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

# [ ] Fase 21: Cliente SSH Remoto no Terminal `⏳ PLANEJADO`

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

- [ ] **Etapa 1 — Dependência e Backend `ssh_client`**: Adicionar `david-cermak/libssh` ao `main/idf_component.yml`, criar `ssh_client.h` e `ssh_client.cpp` com criação de sessão, conexão, PTY `xterm`, autenticação por senha e I/O não bloqueante com stack em PSRAM.
- [ ] **Etapa 2 — Comando `ssh` no `terminal_cmd`**: Parsing de `user`, `host`, `port` em `terminal_cmd.cpp`, validação de conectividade Wi-Fi e despacho.
- [ ] **Etapa 3 — Integração com `ui_terminal`**: Estados `LOCAL_SHELL`, `SSH_PASSWORD_PROMPT` e `SSH_SESSION`, callbacks `rx_cb`/`state_cb` thread-safe sob `bsp_display_lock()` e restauração de prompt ao sair.
- [ ] **Etapa 4 — Build, Validação e Gravação**: Atualizar `CMakeLists.txt`, checar `pre-commit`, compilar com `idf.py build` e validar em hardware conectando a servidor Linux na rede local.

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

## 8. Status de Conclusão: `[ ] PLANEJADO`
- **Cliente SSH**: Arquitetura planejada e pronta para implementação na Fase 21.
