/* Simulador host do tab5_os: roda a UI real (os/shell + apps) sobre o
 * LVGL vendido no managed_components, em janela SDL 720x1280.
 *
 * Modos:
 *   tab5_sim --interactive [DIR]      janela interativa (S salva captura)
 *   tab5_sim --scenario NOME [--out DIR] [--update-goldens]
 *   tab5_sim --list                   lista cenarios
 *
 * No modo cenario o relogio e congelado e os backends respondem sempre
 * igual, para que as capturas sejam comparaveis entre execucoes. */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#include "lvgl.h"
#include "SDL.h"

#include "sim_capture.hpp"
#include "sim_time.hpp"
#include "scenarios/sim_scenarios.hpp"

#include "nvs.h"
#include "timezone_mgr.h"
#include "wifi_storage.h"
#include "ui_shell.h"
#include "ui_keyboard.h"
#include "ui_font.h"

namespace {

constexpr uint32_t SIM_W = 720;
constexpr uint32_t SIM_H = 1280;

void pump(uint32_t ms)
{
    const uint64_t start = SDL_GetTicks();
    while (SDL_GetTicks() - start < ms) {
        uint32_t next = lv_task_handler();
        if (next > 20) {
            next = 20;
        }
        usleep(next * 1000U);
    }
}

void ensure_dir(const std::string &dir)
{
    /* mkdir -p simples (uma profundidade e suficiente aqui). */
    std::string cur;
    for (size_t i = 0; i < dir.size(); i++) {
        cur += dir[i];
        if (dir[i] == '/' && i > 0) {
            mkdir(cur.c_str(), 0755);
        }
    }
    mkdir(dir.c_str(), 0755);
}

/* ------------------------------------------------------------------ */
/* Modo interativo                                                     */
/* ------------------------------------------------------------------ */

struct InteractiveState {
    std::string out_dir;
    int shot_counter = 0;
};

int watch_interactive_keys(void *userdata, SDL_Event *event)
{
    auto *st = (InteractiveState *)userdata;
    if (event->type != SDL_KEYDOWN) {
        return 1;
    }

    const SDL_Keycode key = event->key.keysym.sym;
    if (key == SDLK_ESCAPE) {
        exit(0);
    }
    if (key == SDLK_s) {
        ensure_dir(st->out_dir);
        char name[128];
        snprintf(name, sizeof(name), "%s/manual_%03d.bmp", st->out_dir.c_str(), st->shot_counter++);
        sim_capture_to_bmp(name);
        return 1;
    }
    if (key == SDLK_k) {
        ui_keyboard_inject_key(' '); /* no-op util: evita warning de unused */
    }

    switch (key) {
    case SDLK_1:
        ui_shell_open_wifi();
        break;
    case SDLK_2:
        ui_shell_open_files();
        break;
    case SDLK_3:
        app_registry_launch("com.tab5.notas");
        break;
    case SDLK_4:
        ui_shell_open_terminal();
        break;
    case SDLK_5:
        ui_shell_open_bluetooth();
        break;
    case SDLK_6:
        ui_shell_open_camera();
        break;
    case SDLK_7:
        ui_shell_open_gallery();
        break;
    case SDLK_8:
        ui_shell_open_fileserver();
        break;
    case SDLK_9:
        ui_shell_open_recorder();
        break;
    case SDLK_0:
        ui_shell_open_chat();
        break;
    case SDLK_m:
        ui_shell_open_music();
        break;
    case SDLK_d:
        ui_shell_close_notas();
        ui_shell_close_wifi();
        ui_shell_close_files();
        ui_shell_close_bluetooth();
        ui_shell_close_terminal();
        ui_shell_close_camera();
        ui_shell_close_gallery();
        ui_shell_close_fileserver();
        ui_shell_close_recorder();
        ui_shell_close_chat();
        ui_shell_close_music();
        break;
    case SDLK_p:
        simact::click(21, 20);
        break;
    default:
        break;
    }
    return 1;
}

int run_interactive(const std::string &out_dir)
{
    simtime::set_frozen(false);

    InteractiveState st;
    st.out_dir = out_dir.empty() ? "tests/simulator/out/interactive" : out_dir;
    SDL_AddEventWatch(watch_interactive_keys, &st);

    printf("tab5_sim interativo — atalhos: 1-9/0 apps, M musica, D desktop, ");
    printf("P power, S print, ESC sair\n");

    for (;;) {
        pump(50);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Modo cenario                                                        */
/* ------------------------------------------------------------------ */

int run_scenario(const std::string &name, const std::string &out_arg, bool update_goldens);

int run_scenario(const std::string &name, const std::string &out_arg, bool update_goldens)
{
    const sim_scenario *found = nullptr;
    for (const auto &sc : sim_scenarios()) {
        if (name == sc.name) {
            found = &sc;
            break;
        }
    }
    if (found == nullptr) {
        fprintf(stderr, "sim: cenario '%s' desconhecido (--list para ver)\n", name.c_str());
        return 1;
    }

    simtime::set_frozen(true);

    std::string out_dir = out_arg;
    if (out_dir.empty()) {
        out_dir = update_goldens ? "tests/simulator/goldens/" + name : "tests/simulator/out/" + name;
    }
    ensure_dir(out_dir);

    int idx = 1;
    for (const auto &step : found->steps) {
        if (step.action != nullptr) {
            step.action();
        }
        pump(step.settle_ms);
        if (step.shot_name != nullptr) {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s.bmp", out_dir.c_str(), step.shot_name);
            if (!sim_capture_to_bmp(path)) {
                fprintf(stderr, "sim: falha na captura %s\n", path);
                return 1;
            }
        }
        idx++;
    }

    printf("sim: cenario '%s' concluido (%s)\n", name.c_str(), out_dir.c_str());
    return 0;
}

void list_scenarios();

void list_scenarios()
{
    printf("Cenarios disponiveis:\n");
    for (const auto &sc : sim_scenarios()) {
        printf("  %-18s %s (%u passos)\n", sc.name, sc.description, (unsigned)sc.steps.size());
    }
}

/* ------------------------------------------------------------------ */
/* Boot comum                                                          */
/* ------------------------------------------------------------------ */

void boot_ui()
{
    srand(42); /* esp_random deterministico */

    printf("[boot] montando sd\n");
    fflush(stdout);
    wifi_storage_mount(); /* /sdcard aponta p/ tmpdir via path_redirect */
    timezone_mgr_init();

    lv_init(); /* no device quem inicializa e o esp_lvgl_port */
    lv_display_t *disp = lv_sdl_window_create(SIM_W, SIM_H);
    if (disp == nullptr) {
        fprintf(stderr, "sim: falha ao criar janela SDL: %s\n", SDL_GetError());
        exit(1);
    }
    lv_sdl_mouse_create();
    lv_sdl_keyboard_create();

    /* Define fonte Latin-1 como padrão global */
    lv_theme_t *th = lv_theme_default_init(disp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED),
                                           false, &lv_font_montserrat_18_latin1);
    lv_display_set_theme(disp, th);

    ui_shell_init();

    /* Splash some apos 1500ms; espera um pouco mais. */
    pump(2600);
}

} // namespace

int main(int argc, char **argv)
{
    std::string mode;
    std::string scenario_name;
    std::string out_dir;
    bool update_goldens = false;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--interactive") {
            mode = "interactive";
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                out_dir = argv[++i];
            }
        } else if (arg == "--scenario" && i + 1 < argc) {
            mode = "scenario";
            scenario_name = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg == "--update-goldens") {
            update_goldens = true;
        } else if (arg == "--list") {
            list_scenarios();
            return 0;
        } else if (arg == "-h" || arg == "--help") {
            printf("uso: tab5_sim --interactive [DIR] | --scenario NOME [--out DIR] "
                   "[--update-goldens] | --list\n");
            return 0;
        }
    }

    if (mode.empty()) {
        fprintf(stderr, "sim: escolha --interactive ou --scenario (use --list)\n");
        return 1;
    }

    boot_ui();

    if (mode == "interactive") {
        return run_interactive(out_dir);
    }
    return run_scenario(scenario_name, out_dir, update_goldens);
}
