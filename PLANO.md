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
