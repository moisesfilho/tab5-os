# Plano de Arquitetura: Host ABI de UI Genérica & Desacoplamento Total de Aplicações

> Branch: `feat/app-isolation`
> Status: Aprovado para Desenvolvimento
> Data: Setembro de 2026

---

## 1. Visão Geral e Motivação

O **Tab5 OS** introduziu na Fase 46 a execução isolada de aplicações em sandbox WebAssembly via WAMR e empacotamento declarativo `.tab5pkg`.
Contudo, para aplicações com interfaces ricas (como Câmera, Galeria, Arquivos, Calendário, Música e Wi-Fi), o sistema utilizava um padrão de *Host Views* (`ui_*_view.cpp` embutidos em `components/os/shell/` e instanciados hardcoded por `ctx->app_id` em `tab5_ui_host.cpp`).

Este plano define a **Fase 47**, cujo objetivo é a expansão da **Host ABI de UI do Tab5 SDK**, eliminando a necessidade de código de visualização de aplicativos dentro do firmware do SO e viabilizando a criação e distribuição de aplicações com interfaces gráficas ricas 100% isoladas.

---

## 2. Princípios de Design

1. **Handles Inteiros Opacos (`tab5_ui_obj_t = uint32_t`)**:
   - A sandbox WASM não manipula ponteiros de memória direta do host (`lv_obj_t*`).
   - Todos os objetos recebem IDs inteiros gerenciados por uma tabela de handles segura e contextualizada por aplicação.
2. **Ciclo de Vida Automático & Isolamento de Memória**:
   - Ao encerrar a aplicação, todos os widgets criados no escopo daquele `app_context_t` são liberados pelo host automaticamente, evitando memory leaks no LVGL.
3. **Dispatcher de Eventos Desacoplado**:
   - A aplicação registra callbacks de eventos usando IDs inteiros (`TAB5_EVENT_CLICKED`, `TAB5_EVENT_VALUE_CHANGED`, etc.) repassados através da ponte WAMR.
4. **Respeito ao Design System & Temas**:
   - Widgets criados via SDK herdam automaticamente a paleta de cores e tipografia (`ui_theme`) do Tab5 OS sem necessidade de estilização manual no WASM.

---

## 3. Especificação da Nova Host ABI (`tab5_sdk.h`)

### 3.1. Tipos e Constantes

```c
typedef uint32_t tab5_ui_obj_t;
#define TAB5_UI_INVALID_OBJ 0

typedef enum {
    TAB5_UI_ALIGN_DEFAULT = 0,
    TAB5_UI_ALIGN_TOP_LEFT,
    TAB5_UI_ALIGN_TOP_MID,
    TAB5_UI_ALIGN_TOP_RIGHT,
    TAB5_UI_ALIGN_BOTTOM_LEFT,
    TAB5_UI_ALIGN_BOTTOM_MID,
    TAB5_UI_ALIGN_BOTTOM_RIGHT,
    TAB5_UI_ALIGN_LEFT_MID,
    TAB5_UI_ALIGN_RIGHT_MID,
    TAB5_UI_ALIGN_CENTER
} tab5_ui_align_t;

typedef enum {
    TAB5_UI_FLEX_FLOW_ROW = 0,
    TAB5_UI_FLEX_FLOW_COLUMN,
    TAB5_UI_FLEX_FLOW_ROW_WRAP,
    TAB5_UI_FLEX_FLOW_COLUMN_WRAP
} tab5_ui_flex_flow_t;

typedef enum {
    TAB5_UI_EVENT_CLICKED = 1,
    TAB5_UI_EVENT_VALUE_CHANGED = 2,
    TAB5_UI_EVENT_LONG_PRESSED = 3,
    TAB5_UI_EVENT_FOCUSED = 4,
    TAB5_UI_EVENT_DEFOCUSED = 5
} tab5_ui_event_type_t;

typedef void (*tab5_ui_event_cb_t)(tab5_ui_obj_t obj, uint32_t event_type, int32_t event_val);
```

### 3.2. Funções de Layout e Hierarquia

* `tab5_ui_obj_t tab5_ui_container_create(tab5_ui_obj_t parent);`
* `tab5_err_t tab5_ui_obj_set_size(tab5_ui_obj_t obj, int32_t w, int32_t h);` (suporta px e `TAB5_UI_PCT(n)`)
* `tab5_err_t tab5_ui_obj_set_align(tab5_ui_obj_t obj, tab5_ui_align_t align, int32_t x_ofs, int32_t y_ofs);`
* `tab5_err_t tab5_ui_obj_set_flex_flow(tab5_ui_obj_t obj, tab5_ui_flex_flow_t flow);`
* `tab5_err_t tab5_ui_obj_set_flex_align(tab5_ui_obj_t obj, uint8_t main_place, uint8_t cross_place, uint8_t track_place);`
* `tab5_err_t tab5_ui_obj_set_pad(tab5_ui_obj_t obj, int32_t pad_all);`
* `tab5_err_t tab5_ui_obj_set_gap(tab5_ui_obj_t obj, int32_t gap);`
* `tab5_err_t tab5_ui_obj_add_event_cb(tab5_ui_obj_t obj, uint32_t event_code);`

### 3.3. Widgets Básicos e Especializados

* **Label:** `tab5_ui_obj_t tab5_ui_label_create(tab5_ui_obj_t parent, const char *text);`
* **Label Set Text:** `tab5_err_t tab5_ui_label_set_text(tab5_ui_obj_t obj, const char *text);`
* **Button:** `tab5_ui_obj_t tab5_ui_btn_create(tab5_ui_obj_t parent, const char *label_or_symbol);`
* **Switch:** `tab5_ui_obj_t tab5_ui_switch_create(tab5_ui_obj_t parent);`
* **Switch State:** `tab5_err_t tab5_ui_switch_set_state(tab5_ui_obj_t obj, bool checked);`
* **Switch Get State:** `bool tab5_ui_switch_get_state(tab5_ui_obj_t obj);`
* **Slider:** `tab5_ui_obj_t tab5_ui_slider_create(tab5_ui_obj_t parent, int32_t min, int32_t max);`
* **Slider Value:** `tab5_err_t tab5_ui_slider_set_value(tab5_ui_obj_t obj, int32_t val);`
* **Slider Get Value:** `int32_t tab5_ui_slider_get_value(tab5_ui_obj_t obj);`
* **List:** `tab5_ui_obj_t tab5_ui_list_create(tab5_ui_obj_t parent);`
* **List Item:** `tab5_ui_obj_t tab5_ui_list_add_btn(tab5_ui_obj_t list, const char *symbol, const char *text);`

---

## 4. Fases de Execução da Fase 47

1. **Etapa 1:** Atualização dos headers públicos do SDK (`sdk/tab5-app-sdk/include/tab5_sdk.h` e `components/os/runtime/include/tab5_sdk.h`).
2. **Etapa 2:** Implementação do subsistema de tabela de handles e bindings LVGL em `components/os/runtime/tab5_ui_host.cpp`.
3. **Etapa 3:** Integração e exportação dos novos símbolos WAMR na Host ABI (`tab5_host_abi.cpp`).
4. **Etapa 4:** Criação de template/exemplo de app rica desacoplada no SDK (`sdk/tab5-app-sdk/examples/widgets_demo`).
5. **Etapa 5:** Validação da suíte de testes de host nativos e simulador SDL.
