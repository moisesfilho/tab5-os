/* Simulador host do tab5_os: roda a UI real (os/shell + apps) sobre o
 * LVGL vendido no managed_components, em janela SDL 720x1280.
 *
 * Modos:
 *   tab5_sim --interactive [DIR]      janela interativa (S salva captura)
 *   tab5_sim --scenario NOME [--out DIR] [--update-goldens] [--window]
 *   tab5_sim --list                   lista cenarios
 *
 * No modo cenario o relogio e congelado e os backends respondem sempre
 * igual, para que as capturas sejam comparaveis entre execucoes. */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

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
#include "app_registry.h"
#include "tab5_package_mgr.h"
#include "tab5_wasm_runtime.h"
#include "tab5_wasm_dispatcher.h"

namespace {

constexpr uint32_t SIM_W = 720;
constexpr uint32_t SIM_H = 1280;
bool g_silent = false;

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

void settle(uint32_t ms)
{
    pump(ms);
    /* WASM callbacks execute on a worker.  Do not let wall-clock scheduling
     * decide whether a callback is visible in the snapshot. */
    (void)tab5_wasm_dispatcher_wait_idle(2000);
    pump(20);
    (void)tab5_wasm_dispatcher_wait_idle(2000);
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
        tab5_package_mgr_launch("com.tab5.wifi", nullptr);
        break;
    case SDLK_2:
        tab5_package_mgr_launch("com.tab5.files", nullptr);
        break;
    case SDLK_3:
        tab5_package_mgr_launch("com.tab5.notas", nullptr);
        break;
    case SDLK_4:
        tab5_package_mgr_launch("com.tab5.terminal", nullptr);
        break;
    case SDLK_5:
        tab5_package_mgr_launch("com.tab5.bluetooth", nullptr);
        break;
    case SDLK_6:
        tab5_package_mgr_launch("com.tab5.camera", nullptr);
        break;
    case SDLK_7:
        tab5_package_mgr_launch("com.tab5.gallery", nullptr);
        break;
    case SDLK_8:
        tab5_package_mgr_launch("com.tab5.fileserver", nullptr);
        break;
    case SDLK_9:
        tab5_package_mgr_launch("com.tab5.recorder", nullptr);
        break;
    case SDLK_0:
        tab5_package_mgr_launch("com.tab5.chat", nullptr);
        break;
    case SDLK_m:
        tab5_package_mgr_launch("com.tab5.music", nullptr);
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

    if (!g_silent) {
        printf("tab5_sim interativo — atalhos: 1-9/0 apps, M musica, D desktop, ");
        printf("P power, S print, ESC sair\n");
    }

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
    srand(42);
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);

    std::string out_dir = out_arg;
    if (out_dir.empty()) {
        out_dir = update_goldens ? "tests/simulator/goldens/" + name : "tests/simulator/out/" + name;
    }
    ensure_dir(out_dir);
    const std::string explicit_shot_dir = out_arg.empty() ? out_dir : out_dir + "/" + name;
    if (!out_arg.empty()) {
        /* Preserve the legacy single-scenario --out layout while also
         * providing the suite-root layout used by the visual matrix. */
        ensure_dir(explicit_shot_dir);
    }

    int idx = 1;
    for (const auto &step : found->steps) {
        if (step.action != nullptr) {
            step.action();
        }
        /* Keep the scenario's declared settle visible here: the screenshot
         * must be after both LVGL time and the dispatcher idle barrier. */
        pump(step.settle_ms);
        settle(0);
        if (step.shot_name != nullptr) {
            char path[256];
            snprintf(path, sizeof(path), "%s/%s.bmp", out_dir.c_str(), step.shot_name);
            if (!sim_capture_to_bmp(path)) {
                fprintf(stderr, "sim: falha na captura %s\n", path);
                return 1;
            }
            if (!out_arg.empty()) {
                char suite_path[256];
                snprintf(suite_path, sizeof(suite_path), "%s/%s.bmp", explicit_shot_dir.c_str(), step.shot_name);
                if (!sim_capture_to_bmp(suite_path)) {
                    fprintf(stderr, "sim: falha na captura %s\n", suite_path);
                    return 1;
                }
            }
        }
        idx++;
    }

    if (!g_silent) {
        printf("sim: cenario '%s' concluido (%s)\n", name.c_str(), out_dir.c_str());
    }
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

    if (!g_silent) {
        printf("[boot] montando sd\n");
        fflush(stdout);
    }
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

    /* Popula /apps a partir de embedded_apps_pkg para o simulador */
    DIR *d_pkg = opendir("embedded_apps_pkg");
    if (d_pkg != nullptr) {
        std::vector<std::string> package_names;
        struct dirent *entry;
        while ((entry = readdir(d_pkg)) != nullptr) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            package_names.emplace_back(entry->d_name);
        }
        closedir(d_pkg);

        /* ext4 readdir order is not an API.  The launcher grid reflects the
         * package registration order, so copy packages in the established
         * image order rather than allowing directory allocation to choose it. */
        static const std::vector<std::string> package_order = {
            "com.tab5.fileserver.tab5pkg", "com.tab5.bluetooth.tab5pkg", "com.tab5.files.tab5pkg",
            "com.tab5.chat.tab5pkg",       "com.tab5.terminal.tab5pkg",  "com.tab5.notas.tab5pkg",
            "com.tab5.calendar.tab5pkg",   "com.tab5.recorder.tab5pkg",  "com.tab5.gallery.tab5pkg",
            "com.tab5.music.tab5pkg",      "com.tab5.wifi.tab5pkg",      "com.tab5.camera.tab5pkg",
        };
        std::stable_sort(package_names.begin(), package_names.end(),
                         [&](const std::string &lhs, const std::string &rhs) {
                             const auto rank = [&](const std::string &name) {
                                 auto it = std::find(package_order.begin(), package_order.end(), name);
                                 return it == package_order.end() ? package_order.size()
                                                                  : static_cast<size_t>(it - package_order.begin());
                             };
                             const size_t left_rank = rank(lhs);
                             const size_t right_rank = rank(rhs);
                             return left_rank == right_rank ? lhs < rhs : left_rank < right_rank;
                         });

        for (const std::string &package_name : package_names) {
            std::string src = std::string("embedded_apps_pkg/") + package_name;
            std::string dst = std::string("/apps/") + package_name;
            FILE *fsrc = fopen(src.c_str(), "rb");
            if (fsrc != nullptr) {
                FILE *fdst = fopen(dst.c_str(), "wb");
                if (fdst != nullptr) {
                    char buf[4096];
                    size_t n;
                    while ((n = fread(buf, 1, sizeof(buf), fsrc)) > 0) {
                        fwrite(buf, 1, n, fdst);
                    }
                    fclose(fdst);
                }
                fclose(fsrc);
            }
        }
    }

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
    bool window_requested = false;
    bool list_requested = false;
    bool help_requested = false;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--interactive") {
            mode = "interactive";
            window_requested = true;
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
        } else if (arg == "--window") {
            window_requested = true;
        } else if (arg == "--silent") {
            g_silent = true;
        } else if (arg == "--list") {
            list_requested = true;
        } else if (arg == "-h" || arg == "--help") {
            help_requested = true;
        } else {
            fprintf(stderr, "sim: flag desconhecida '%s'\n", arg.c_str());
            return 2;
        }
    }

    if (g_silent) {
        /* Também silencia logs dos subsistemas/LVGL que escrevem diretamente
         * em stdout; erros continuam indo para stderr. */
        if (freopen("/dev/null", "w", stdout) == nullptr) {
            fprintf(stderr, "sim: nao foi possivel ativar --silent\n");
            return 1;
        }
    }

    if (list_requested) {
        if (!g_silent) {
            list_scenarios();
        }
        return 0;
    }
    if (help_requested) {
        if (!g_silent) {
            printf("uso: tab5_sim --interactive [DIR] | --scenario NOME [--out DIR] "
                   "[--update-goldens] [--window] | --list\n");
        }
        return 0;
    }

    if (mode.empty()) {
        fprintf(stderr, "sim: escolha --interactive ou --scenario (use --list)\n");
        return 1;
    }

    if (window_requested && getenv("DISPLAY") == nullptr && getenv("WAYLAND_DISPLAY") == nullptr) {
        fprintf(stderr, "sim: --window/--interactive requer DISPLAY ou WAYLAND_DISPLAY\n");
        return 1;
    }

    if (mode == "scenario" && !window_requested) {
        /* Cenários nunca dependem de X/Wayland; janela visível é opt-in. */
        SDL_SetHint("SDL_VIDEODRIVER", "dummy");
        SDL_SetHint("SDL_RENDER_DRIVER", "software");
    }

    boot_ui();

    if (mode == "interactive") {
        int result = run_interactive(out_dir);
        tab5_package_mgr_close_active();
        tab5_wasm_runtime_destroy();
        SDL_Quit();
        return result;
    }
    int result = run_scenario(scenario_name, out_dir, update_goldens);
    tab5_package_mgr_close_active();
    tab5_wasm_runtime_destroy();
    SDL_Quit();
    return result;
}
