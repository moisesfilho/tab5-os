#include "sim_scenarios.hpp"

#include <cstdio>
#include <sys/stat.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "lvgl.h"
#include "SDL.h"
#include "src/drivers/sdl/lv_sdl_private.h"

#include "ui_shell.h"
#include "ui_keyboard.h"
#include "ui_bar.h"
#include "app_registry.h"
#include "tab5_package_mgr.h"

/* ------------------------------------------------------------------ */
/* Injecao de eventos SDL (mesmo caminho do touch real no device)      */
/* ------------------------------------------------------------------ */

namespace {

uint32_t window_id()
{
    /* O id da janela vive nos dados privados do driver SDL (versao vendida
     * no managed_components; o simulador e fixado a ela). */
    lv_display_t *disp = lv_display_get_default();
    if (disp == nullptr) {
        return UINT32_MAX;
    }
    const lv_sdl_window_t *wd = (const lv_sdl_window_t *)lv_sdl_backend_get_display_data(disp);
    if (wd == nullptr || wd->window == nullptr) {
        return UINT32_MAX;
    }
    return SDL_GetWindowID(wd->window);
}

} // namespace

namespace simact {

void click(int x, int y)
{
    uint32_t win = window_id();

    SDL_Event motion = {};
    motion.type = SDL_MOUSEMOTION;
    motion.motion.windowID = win;
    motion.motion.x = x;
    motion.motion.y = y;
    SDL_PushEvent(&motion);

    SDL_Event down = {};
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.windowID = win;
    down.button.button = SDL_BUTTON_LEFT;
    down.button.state = SDL_PRESSED;
    down.button.clicks = 1;
    down.button.x = x;
    down.button.y = y;
    SDL_PushEvent(&down);

    SDL_Event up = down;
    up.type = SDL_MOUSEBUTTONUP;
    up.button.state = SDL_RELEASED;
    SDL_PushEvent(&up);
}

void type_text(const char *text)
{
    for (const char *p = text; *p != '\0'; p++) {
        ui_keyboard_inject_char(*p);
    }
}

/* Cria um BMP de teste (degradê 320x240) para cenarios de galeria/arquivos. */
void make_test_bmp(const char *path)
{
    const int w = 320;
    const int h = 240;
    FILE *f = fopen(path, "wb");
    if (f == nullptr) {
        return;
    }
    const uint32_t row_size = ((w * 3 + 3) / 4) * 4;
    const uint32_t data_size = row_size * h;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B';
    hdr[1] = 'M';
    auto put32 = [&hdr](int off, uint32_t v) {
        hdr[off] = (uint8_t)(v & 0xFF);
        hdr[off + 1] = (uint8_t)((v >> 8) & 0xFF);
        hdr[off + 2] = (uint8_t)((v >> 16) & 0xFF);
        hdr[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    };
    put32(2, 54 + data_size);
    put32(10, 54);
    put32(14, 40);
    put32(18, w);
    put32(22, h);
    hdr[26] = 1;
    hdr[28] = 24;
    put32(34, data_size);
    fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(row_size, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            row[x * 3 + 0] = (uint8_t)(x * 255 / w);
            row[x * 3 + 1] = (uint8_t)(y * 255 / h);
            row[x * 3 + 2] = (uint8_t)(128);
        }
        fwrite(row.data(), 1, row_size, f);
    }
    fclose(f);
}

void seed_files_fixtures(void)
{
    mkdir("/sdcard/imagens", 0755);
    make_test_bmp("/sdcard/imagens/paisagem.bmp");

    FILE *txt = fopen("/sdcard/leia-me.txt", "w");
    if (txt != nullptr) {
        fprintf(txt, "tab5_os simulador\n");
        fclose(txt);
    }
}

} // namespace simact

/* ------------------------------------------------------------------ */
/* Acoes dos cenarios                                                  */
/* ------------------------------------------------------------------ */

namespace {

void act_open_wifi(void)
{
    ui_shell_open_wifi();
}

void act_close_wifi(void)
{
    ui_shell_close_wifi();
}

void act_open_files(void)
{
    simact::seed_files_fixtures();
    tab5_package_mgr_launch("com.tab5.files", nullptr);
}

void act_open_notas(void)
{
    tab5_package_mgr_launch("com.tab5.notas", nullptr);
}

void act_open_terminal(void)
{
    ui_shell_open_terminal();
}

void act_open_bluetooth(void)
{
    ui_shell_open_bluetooth();
}

void act_open_gallery(void)
{
    simact::seed_files_fixtures();
    ui_shell_open_gallery_with_file("/sdcard/imagens/paisagem.bmp");
}

void act_open_music(void)
{
    ui_shell_open_music();
}

void act_open_chat(void)
{
    ui_shell_open_chat();
}

void act_open_recorder(void)
{
    ui_shell_open_recorder();
}

void act_open_camera(void)
{
    ui_shell_open_camera();
}

void act_open_fileserver(void)
{
    ui_shell_open_fileserver();
}

void act_open_calendar(void)
{
    tab5_package_mgr_launch("com.tab5.calendar", nullptr);
}

void act_click_power(void)
{
    ui_bar_open_power_menu();
}

void act_click_gear(void)
{
    ui_bar_open_settings();
}

void act_click_clock(void)
{
    ui_bar_open_calendar_popup();
}

/* Tela dedicada ao teclado: textarea propria + attach + texto digitado. */
void act_show_keyboard(void)
{
    static lv_obj_t *scr = nullptr;
    static lv_obj_t *ta = nullptr;

    if (scr == nullptr) {
        scr = lv_obj_create(nullptr);
        lv_obj_set_size(scr, 720, 1280);
        ta = lv_textarea_create(scr);
        lv_obj_set_width(ta, 680);
        lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, UI_BAR_HEIGHT + 20);
    }

    lv_scr_load(scr);
    ui_keyboard_attach(ta);
    simact::type_text("Teste teclado 123");
}

} // namespace

/* ------------------------------------------------------------------ */
/* Registro                                                            */
/* ------------------------------------------------------------------ */

const std::vector<sim_scenario> &sim_scenarios()
{
    static const std::vector<sim_scenario> list = {
        {"shell_desktop",
         "Desktop com barra de status apos o splash",
         {
             {nullptr, 400, "01_desktop"},
         }},
        {"shell_power",
         "Painel de energia (clique no icone da barra)",
         {
             {act_click_power, 700, "01_power_menu"},
         }},
        {"shell_settings",
         "Painel de configuracoes (clique na engrenagem)",
         {
             {act_click_gear, 700, "01_settings"},
         }},
        {"shell_calendar_popup",
         "Popup de calendário mensal (clique na data/hora da barra)",
         {
             {act_click_clock, 700, "01_calendar_popup"},
         }},
        {"app_teclado",
         "Teclado PT-BR com texto digitado",
         {
             {act_show_keyboard, 700, "01_teclado"},
         }},
        {"app_notas",
         "Aplicativo Notas vazio",
         {
             {act_open_notas, 800, "01_notas"},
         }},
        {"app_files",
         "Arquivos com fixtures no SD virtual",
         {
             {act_open_files, 1200, "01_files"},
         }},
        {"app_wifi",
         "Wi-Fi conectado na rede fake",
         {
             {act_open_wifi, 800, "01_wifi"},
         }},
        {"app_bluetooth", "Bluetooth habilitado sem dispositivos", {{act_open_bluetooth, 800, "01_bt"}}},
        {"app_terminal",
         "Terminal com prompt inicial",
         {
             {act_open_terminal, 800, "01_terminal"},
         }},
        {"app_gallery",
         "Galeria exibindo BMP de teste",
         {
             {act_open_gallery, 1500, "01_gallery"},
         }},
        {"app_music", "Player parado", {{act_open_music, 800, "01_music"}}},
        {"app_chat", "Chat IA idle", {{act_open_chat, 800, "01_chat"}}},
        {"app_recorder", "Gravador idle", {{act_open_recorder, 800, "01_recorder"}}},
        {"app_camera", "Camera sem preview (mock)", {{act_open_camera, 900, "01_camera"}}},
        {"app_fileserver", "Fileserver HTTP parado", {{act_open_fileserver, 900, "01_fileserver"}}},
        {"app_calendar", "Aplicativo Calendário", {{act_open_calendar, 800, "01_calendar"}}},
    };
    return list;
}
