#include "ui_notas.h"
#include "ui_shell.h"
#include "ui_keyboard.h"
#include "ui_theme.h"
#include "ui_bar.h"
#include "ui_font.h"

namespace {

lv_obj_t *notas_scr = nullptr;
lv_obj_t *notas_bar = nullptr;
lv_obj_t *notas_title = nullptr;
lv_obj_t *notas_close = nullptr;
lv_obj_t *notas_close_label = nullptr;
lv_obj_t *notas_ta = nullptr;
lv_group_t *notas_group = nullptr;
lv_timer_t *notas_cursor_timer = nullptr;

void close_cb(lv_event_t *event)
{
    (void)event;
    lv_timer_pause(notas_cursor_timer);
    lv_obj_set_style_bg_opa(notas_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    ui_shell_close_notas();
}

/* Blink determinístico do cursor: alterna a opacidade do LV_PART_CURSOR
 * (o blink nativo via anim_duration/LV_EVENT_FOCUSED nao e confiavel
 * neste firmware). O cursor interno fica estatico (anim_duration 0) e
 * segue o texto nativamente; este timer so pisca a visibilidade. */
void cursor_blink_cb(lv_timer_t *timer)
{
    lv_obj_t *ta = (lv_obj_t *)lv_timer_get_user_data(timer);
    lv_opa_t opa = lv_obj_get_style_bg_opa(ta, LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(ta, opa == LV_OPA_TRANSP ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_CURSOR);
}

/* Mostra o teclado, garante o cursor visivel e da FOCO REAL ao textarea. */
void ta_click_cb(lv_event_t *event)
{
    (void)event;
    lv_obj_set_style_bg_opa(notas_ta, LV_OPA_COVER, LV_PART_CURSOR);
    lv_timer_resume(notas_cursor_timer);
    lv_group_focus_obj(notas_ta);
    ui_keyboard_attach(notas_ta);
}

/* O textarea encosta na barra do app (janela maximizada), com margem
 * de 12px apenas na parte inferior. */
void apply_notas_layout(void)
{
    if (notas_ta == nullptr) {
        return;
    }

    int32_t h = lv_display_get_vertical_resolution(NULL);
    lv_obj_set_height(notas_ta, h - 2 * UI_BAR_HEIGHT - 12);
}

void notas_resolution_cb(lv_event_t *event)
{
    (void)event;
    apply_notas_layout();
}

/* Reaplica a paleta ativa no app. */
void apply_notas_theme(void)
{
    if (notas_scr == nullptr) {
        return;
    }

    const ui_palette_t *pal = ui_theme_get();

    lv_obj_set_style_bg_color(notas_scr, lv_color_hex(pal->background), 0);

    /* Barra do app um tom mais clara que a barra do sistema (surface_alt). */
    lv_obj_set_style_bg_color(notas_bar, lv_color_hex(pal->surface_alt), 0);
    lv_obj_set_style_border_color(notas_bar, lv_color_hex(pal->border), 0);
    lv_obj_set_style_text_color(notas_title, lv_color_hex(pal->text), 0);

    /* X transparente no repouso com circulo sutil ao redor (como a
     * engrenagem da barra do sistema), realce no pressionado mantendo o
     * contraste na barra clara. */
    lv_obj_set_style_bg_opa(notas_close, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(notas_close, 1, 0);
    lv_obj_set_style_border_color(notas_close, lv_color_hex(pal->text_muted), 0);
    lv_obj_set_style_bg_opa(notas_close, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(notas_close, lv_color_hex(pal->accent_soft), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(notas_close_label, lv_color_hex(pal->text), 0);

    lv_obj_set_style_bg_color(notas_ta, lv_color_hex(pal->surface), 0);
    lv_obj_set_style_text_color(notas_ta, lv_color_hex(pal->text), 0);
    /* Janela maximizada: sem borda externa, mesmo com foco. */
    lv_obj_set_style_border_width(notas_ta, 0, 0);
    lv_obj_set_style_radius(notas_ta, 0, 0);
    lv_obj_set_style_pad_all(notas_ta, 14, 0);
    lv_obj_set_style_text_color(notas_ta, lv_color_hex(pal->text_muted), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_bg_color(notas_ta, lv_color_hex(pal->accent), LV_PART_CURSOR);
    /* Cursor visivel por padrao quando o app esta em uso; o estado oculto
     * inicial e reaplicado no fim do create (e ao fechar o app). */
    lv_obj_set_style_bg_opa(notas_ta, LV_OPA_COVER, LV_PART_CURSOR);
    /* Afina o block cursor ~2px (espaco da fonte 14 e ~4-5px). */
    lv_obj_set_style_pad_right(notas_ta, -2, LV_PART_CURSOR);
}

} // namespace

lv_obj_t *ui_notas_create(void)
{
    notas_scr = lv_obj_create(NULL);

    /* Barra propria do app, logo abaixo da barra do sistema (que vive no
     * layer top e ocupa y=0..UI_BAR_HEIGHT). */
    notas_bar = lv_obj_create(notas_scr);
    lv_obj_set_size(notas_bar, lv_pct(100), UI_BAR_HEIGHT);
    lv_obj_align(notas_bar, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT);
    lv_obj_set_style_bg_opa(notas_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(notas_bar, 1, 0);
    lv_obj_set_style_border_side(notas_bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_radius(notas_bar, 0, 0);
    lv_obj_set_style_shadow_width(notas_bar, 0, 0);
    lv_obj_clear_flag(notas_bar, LV_OBJ_FLAG_SCROLLABLE);

    notas_title = lv_label_create(notas_bar);
    lv_label_set_text(notas_title, "Notas");
    lv_obj_set_style_text_font(notas_title, &lv_font_montserrat_14_latin1, 0);
    lv_obj_align(notas_title, LV_ALIGN_LEFT_MID, 0, 0);
    /* Flush exato: a borda de 1px do notas_bar e o padding de tema nao
     * podem mais deslocar o titulo. */
    lv_obj_set_x(notas_title, 0);

    notas_close = lv_obj_create(notas_bar);
    lv_obj_set_size(notas_close, 36, 36);
    lv_obj_align(notas_close, LV_ALIGN_RIGHT_MID, 0, 0);
    /* Flush exato: aresta direita do botao na borda direita da tela. */
    lv_obj_set_x(notas_close, lv_obj_get_width(notas_bar) - lv_obj_get_width(notas_close));
    lv_obj_set_style_radius(notas_close, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(notas_close, 0, 0);
    /* Sem scroll: remove os scrollbars e o arraste do botao. */
    lv_obj_clear_flag(notas_close, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(notas_close, close_cb, LV_EVENT_CLICKED, nullptr);

    notas_close_label = lv_label_create(notas_close);
    lv_label_set_text(notas_close_label, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_font(notas_close_label, &lv_font_montserrat_14_latin1, 0);
    lv_obj_center(notas_close_label);

    /* Area de digitacao maximizada: largura total e encostada na barra do
     * app (y = fim da segunda barra), sem margens/bordas laterais. */
    notas_ta = lv_textarea_create(notas_scr);
    lv_obj_set_width(notas_ta, lv_pct(100));
    lv_obj_align(notas_ta, LV_ALIGN_TOP_MID, 0, 2 * UI_BAR_HEIGHT);
    lv_textarea_set_placeholder_text(notas_ta, "Escreva sua nota...");
    lv_textarea_set_cursor_click_pos(notas_ta, true);
    /* Cursor interno ESTATICO (anim_duration 0): a area do cursor e sempre
     * desenhada e acompanha o texto nativamente. A piscada e feita pelo
     * timer abaixo, que alterna a opacidade do LV_PART_CURSOR. */
    lv_obj_set_style_anim_duration(notas_ta, 0, LV_PART_CURSOR);
    lv_obj_set_style_text_font(notas_ta, &lv_font_montserrat_14_latin1, LV_PART_MAIN);
    lv_obj_add_event_cb(notas_ta, ta_click_cb, LV_EVENT_CLICKED, nullptr);

    /* Grupo dedicado: da FOCO REAL (LV_EVENT_FOCUSED) ao textarea no toque,
     * necessario para o campo receber os eventos de tecla do teclado. */
    notas_group = lv_group_create();
    lv_group_add_obj(notas_group, notas_ta);

    apply_notas_layout();
    lv_display_add_event_cb(lv_display_get_default(), notas_resolution_cb,
                            LV_EVENT_RESOLUTION_CHANGED, nullptr);

    apply_notas_theme();

    /* Estado inicial: cursor oculto e timer pausado (o create roda UMA vez;
     * o close_cb reaplica TRANSP + pause a cada fechamento). */
    lv_obj_set_style_bg_opa(notas_ta, LV_OPA_TRANSP, LV_PART_CURSOR);
    notas_cursor_timer = lv_timer_create(cursor_blink_cb, 500, notas_ta);
    lv_timer_pause(notas_cursor_timer);

    return notas_scr;
}

void ui_notas_refresh_theme(void)
{
    apply_notas_theme();
}