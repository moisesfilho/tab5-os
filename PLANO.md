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
