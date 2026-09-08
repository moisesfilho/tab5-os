/**
 * @file tab5_package_mgr.cpp
 * @brief Implementação do Gerenciador de Pacotes e Registro Dinâmico
 */

#include "tab5_package_mgr.h"
#include "tab5_wasm_runtime.h"
#include "tab5_lifecycle_host.h"
#include "tab5_storage_sandbox.h"
#include "app_registry.h"
#include "file_assoc.h"
#include "tab5_wasm_dispatcher.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_rom_sys.h"
static const char *TAG = "tab5_pkg_mgr";
#define LOG_I(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...)
#define LOG_W(fmt, ...)
#define LOG_E(fmt, ...)
#endif

static size_t parse_octal(const char *str, size_t max_len)
{
    size_t val = 0;
    for (size_t i = 0; i < max_len && str[i] >= '0' && str[i] <= '7'; i++) {
        val = (val << 3) + (str[i] - '0');
    }
    return val;
}

extern "C" bool tab5_package_read_manifest_from_tar(const char *tar_path, tab5_manifest_t *out_manifest)
{
    if (tar_path == nullptr || out_manifest == nullptr) {
        return false;
    }
    FILE *tar = fopen(tar_path, "rb");
    if (!tar) {
        return false;
    }

    char header[512];
    bool found = false;
    while (fread(header, 1, 512, tar) == 512) {
        if (header[0] == '\0') {
            break;
        }
        size_t file_size = parse_octal(&header[124], 12);
        size_t blocks = (file_size + 511) / 512;
        std::string name(header, 100);
        size_t nul_pos = name.find('\0');
        if (nul_pos != std::string::npos) {
            name.resize(nul_pos);
        }

        if (name.find("manifest.json") != std::string::npos && file_size > 0 && file_size < 65536) {
            std::vector<char> buf(file_size + 1, 0);
            size_t read_bytes = fread(buf.data(), 1, file_size, tar);
            if (read_bytes == file_size) {
                buf[file_size] = '\0';
                found = (tab5_manifest_parse_json(buf.data(), out_manifest) == TAB5_OK);
            }
            break;
        }
        fseek(tar, static_cast<long>(blocks) * 512, SEEK_CUR);
    }
    fclose(tar);
    return found;
}

extern "C" bool tab5_package_read_file_from_tar(const char *tar_path, const char *filename,
                                                std::vector<uint8_t> *out_bytes)
{
    if (tar_path == nullptr || filename == nullptr || out_bytes == nullptr) {
        return false;
    }
    FILE *tar = fopen(tar_path, "rb");
    if (!tar) {
        return false;
    }

    char header[512];
    bool found = false;
    while (fread(header, 1, 512, tar) == 512) {
        if (header[0] == '\0') {
            break;
        }
        size_t file_size = parse_octal(&header[124], 12);
        size_t blocks = (file_size + 511) / 512;
        std::string name(header, 100);
        size_t nul_pos = name.find('\0');
        if (nul_pos != std::string::npos) {
            name.resize(nul_pos);
        }
        if (name.rfind("./", 0) == 0) {
            name = name.substr(2);
        }
        if (name.rfind('/', 0) == 0) {
            name = name.substr(1);
        }

        if (name == filename && file_size > 0) {
            out_bytes->resize(file_size);
            size_t read_bytes = fread(out_bytes->data(), 1, file_size, tar);
            if (read_bytes == file_size) {
                found = true;
            }
            break;
        }
        fseek(tar, static_cast<long>(blocks) * 512, SEEK_CUR);
    }
    fclose(tar);
    return found;
}

extern "C" bool tab5_package_extract_tar(const char *tar_path, const char *dest_dir)
{
    if (tar_path == nullptr || dest_dir == nullptr) {
        return false;
    }
    FILE *tar = fopen(tar_path, "rb");
    if (!tar) {
        return false;
    }
    mkdir(dest_dir, 0755);

    char header[512];
    while (fread(header, 1, 512, tar) == 512) {
        if (header[0] == '\0') {
            break;
        }
        size_t file_size = parse_octal(&header[124], 12);
        size_t blocks = (file_size + 511) / 512;
        char typeflag = header[156];
        std::string filename(header, 100);
        size_t nul_pos = filename.find('\0');
        if (nul_pos != std::string::npos) {
            filename.resize(nul_pos);
        }

        if (filename.rfind("./", 0) == 0) {
            filename = filename.substr(2);
        }
        if (filename.rfind('/', 0) == 0) {
            filename = filename.substr(1);
        }

        std::string full_dest = std::string(dest_dir) + "/" + filename;
        if (typeflag == '5' || (!filename.empty() && filename.back() == '/')) {
            mkdir(full_dest.c_str(), 0755);
        } else {
            size_t slash = full_dest.find_last_of('/');
            if (slash != std::string::npos) {
                mkdir(full_dest.substr(0, slash).c_str(), 0755);
            }
            FILE *out = fopen(full_dest.c_str(), "wb");
            if (out != nullptr) {
                char buf[512];
                size_t remaining = file_size;
                while (remaining > 0) {
                    size_t to_read = (remaining < sizeof(buf)) ? remaining : sizeof(buf);
                    fread(buf, 1, sizeof(buf), tar);
                    fwrite(buf, 1, to_read, out);
                    remaining -= to_read;
                }
                fclose(out);
            } else {
                fseek(tar, static_cast<long>(blocks) * 512, SEEK_CUR);
            }
        }
    }
    fclose(tar);
    return true;
}

namespace {

struct DynamicAppEntry {
    tab5_manifest_t manifest;
    std::string install_dir;
    bool is_embedded;
    std::string id_str;
    std::string name_str;
    std::string icon_symbol_str;
    std::string icon_bg_color_str;
    std::vector<std::string> file_extensions_storage;
    std::vector<const char *> file_extensions_ptrs;
    app_desc_t desc;
    tab5_app_context_t host_ctx;
    tab5_wasm_app_instance_t wasm_inst;
};

static std::map<std::string, std::unique_ptr<DynamicAppEntry>> s_dynamic_apps;
static DynamicAppEntry *s_running_dynamic_app = nullptr;
static DynamicAppEntry *s_pending_close_app = nullptr;

static bool copy_file_contents(const char *src_path, const char *dst_path)
{
    FILE *src = fopen(src_path, "rb");
    if (src == nullptr) {
        return false;
    }
    FILE *dst = fopen(dst_path, "wb");
    if (dst == nullptr) {
        fclose(src);
        return false;
    }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        if (fwrite(buffer, 1, bytes, dst) != bytes) {
            fclose(src);
            fclose(dst);
            return false;
        }
    }

    fclose(src);
    fclose(dst);
    return true;
}

static bool remove_dir_recursive(const char *path)
{
    DIR *d = opendir(path);
    if (d == nullptr) {
        return (unlink(path) == 0);
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        std::string full_path = std::string(path) + "/" + entry->d_name;
        struct stat st;
        if (stat(full_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                remove_dir_recursive(full_path.c_str());
            } else {
                unlink(full_path.c_str());
            }
        }
    }
    closedir(d);
    return (rmdir(path) == 0);
}

static void on_dynamic_app_launch(const char *app_id)
{
    if (app_id == nullptr) {
        return;
    }
    tab5_package_mgr_launch(app_id, nullptr);
}

static void on_dynamic_app_open_file(const char *app_id, const char *filepath)
{
    if (app_id == nullptr || filepath == nullptr) {
        return;
    }
    tab5_package_mgr_launch(app_id, filepath);
}

static char s_slot_app_ids[32][64];
static size_t s_slot_count = 0;

template <size_t Index> struct AppSlot {
    static void launch()
    {
        if (Index < s_slot_count && s_slot_app_ids[Index][0] != '\0') {
            on_dynamic_app_launch(s_slot_app_ids[Index]);
        }
    }
    static void open_file(const char *filepath)
    {
        if (Index < s_slot_count && s_slot_app_ids[Index][0] != '\0') {
            on_dynamic_app_open_file(s_slot_app_ids[Index], filepath);
        }
    }
};

#define APP_SLOT_PAIR(n) {&AppSlot<n>::launch, &AppSlot<n>::open_file}

static const struct {
    app_launch_cb_t launch;
    app_open_file_cb_t open_file;
} s_trampolines[] = {APP_SLOT_PAIR(0),  APP_SLOT_PAIR(1),  APP_SLOT_PAIR(2),  APP_SLOT_PAIR(3),  APP_SLOT_PAIR(4),
                     APP_SLOT_PAIR(5),  APP_SLOT_PAIR(6),  APP_SLOT_PAIR(7),  APP_SLOT_PAIR(8),  APP_SLOT_PAIR(9),
                     APP_SLOT_PAIR(10), APP_SLOT_PAIR(11), APP_SLOT_PAIR(12), APP_SLOT_PAIR(13), APP_SLOT_PAIR(14),
                     APP_SLOT_PAIR(15), APP_SLOT_PAIR(16), APP_SLOT_PAIR(17), APP_SLOT_PAIR(18), APP_SLOT_PAIR(19),
                     APP_SLOT_PAIR(20), APP_SLOT_PAIR(21), APP_SLOT_PAIR(22), APP_SLOT_PAIR(23), APP_SLOT_PAIR(24),
                     APP_SLOT_PAIR(25), APP_SLOT_PAIR(26), APP_SLOT_PAIR(27), APP_SLOT_PAIR(28), APP_SLOT_PAIR(29),
                     APP_SLOT_PAIR(30), APP_SLOT_PAIR(31)};

} // namespace

tab5_err_t tab5_package_mgr_init(void)
{
#ifdef ESP_PLATFORM
    esp_vfs_spiffs_conf_t conf = {.base_path = TAB5_APPS_EMBEDDED_DIR,
                                  .partition_label = "apps",
                                  .max_files = 32,
                                  .format_if_mount_failed = false};
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        LOG_W("Particao /apps (spiffs) nao montada ou vazia (ret=%s)", esp_err_to_name(ret));
    } else {
        LOG_I("Particao /apps montada com sucesso");
    }
#endif
    mkdir(TAB5_APPS_DIR, 0755);
    mkdir(TAB5_APPS_INSTALLED_DIR, 0755);
    mkdir(TAB5_APPS_DATA_DIR, 0755);
    s_dynamic_apps.clear();
    memset(s_slot_app_ids, 0, sizeof(s_slot_app_ids));
    s_slot_count = 0;
    s_running_dynamic_app = nullptr;
    s_pending_close_app = nullptr;
    return TAB5_OK;
}

static tab5_err_t register_dynamic_app_entry(const tab5_manifest_t &manifest, const std::string &install_dir,
                                             bool is_embedded)
{
    std::string id = manifest.id;
    auto it = s_dynamic_apps.find(id);
    if (it != s_dynamic_apps.end()) {
        DynamicAppEntry *existing = it->second.get();
        // Se a app existente for embutida e a nova for do SD, compara versões
        if (existing->is_embedded && !is_embedded) {
            int cmp = tab5_manifest_version_compare(manifest.version, existing->manifest.version);
            if (cmp > 0) {
                LOG_I("App %s do SD (v%s) tem precedencia sobre embutida (v%s)", id.c_str(), manifest.version,
                      existing->manifest.version);
                existing->manifest = manifest;
                existing->install_dir = install_dir;
                existing->is_embedded = false;
                existing->host_ctx.permissions = manifest.permissions;
                return TAB5_OK;
            }
            LOG_I("App embutida %s (v%s) e mais recente que SD (v%s), mantendo embutida", id.c_str(),
                  existing->manifest.version, manifest.version);
            return TAB5_OK;
        }
        return TAB5_OK; // Já registrado
    }

    size_t slot = s_slot_count;
    if (slot >= sizeof(s_trampolines) / sizeof(s_trampolines[0])) {
        LOG_E("Limite maximo de apps dinamicas atingido");
        return TAB5_ERR_NO_MEM;
    }

    strncpy(s_slot_app_ids[slot], manifest.id, sizeof(s_slot_app_ids[slot]) - 1);
    s_slot_count++;

    auto entry = std::make_unique<DynamicAppEntry>();
    entry->manifest = manifest;
    entry->install_dir = install_dir;
    entry->is_embedded = is_embedded;
    entry->id_str = manifest.id;
    entry->name_str = manifest.name;
    entry->icon_symbol_str = manifest.icon_symbol[0] ? manifest.icon_symbol : "#";
    entry->icon_bg_color_str = manifest.icon_bg_color[0] ? manifest.icon_bg_color : "";

    for (int i = 0; i < manifest.file_assoc_count; i++) {
        entry->file_extensions_storage.push_back(manifest.file_associations[i]);
    }
    for (const auto &ext : entry->file_extensions_storage) {
        entry->file_extensions_ptrs.push_back(ext.c_str());
    }
    entry->file_extensions_ptrs.push_back(nullptr);

    memset(&entry->desc, 0, sizeof(entry->desc));
    entry->desc.id = entry->id_str.c_str();
    entry->desc.name = entry->name_str.c_str();
    entry->desc.icon_symbol = entry->icon_symbol_str.c_str();
    entry->desc.icon_bg_color = entry->icon_bg_color_str.c_str();
    entry->desc.file_extensions = entry->file_extensions_ptrs.data();
    entry->desc.on_launch = s_trampolines[slot].launch;
    entry->desc.on_open_file = s_trampolines[slot].open_file;

    memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
    strncpy(entry->host_ctx.app_id, manifest.id, sizeof(entry->host_ctx.app_id) - 1);
    strncpy(entry->host_ctx.app_name, manifest.name, sizeof(entry->host_ctx.app_name) - 1);
    entry->host_ctx.permissions = manifest.permissions;
    entry->host_ctx.state = TAB5_APP_STATE_UNINITIALIZED;

    s_dynamic_apps[id] = std::move(entry);

    app_registry_register(&s_dynamic_apps[id]->desc);
    LOG_I("App dinamicamente registrada: %s (%s, embutida=%d)", manifest.id, manifest.name, (int)is_embedded);
    return TAB5_OK;
}

tab5_err_t tab5_package_mgr_install(const char *source_path, char *out_app_id, size_t id_buf_size)
{
    if (source_path == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    tab5_manifest_t manifest = {};
    bool is_tar = false;

    // 1. Tenta carregar direto do arquivo .tab5pkg / TAR
    if (tab5_package_read_manifest_from_tar(source_path, &manifest)) {
        is_tar = true;
    } else {
        std::string manifest_file = std::string(source_path) + "/manifest.json";
        tab5_err_t err = tab5_manifest_load_from_file(manifest_file.c_str(), &manifest);
        if (err != TAB5_OK) {
            err = tab5_manifest_load_from_file(source_path, &manifest);
            if (err != TAB5_OK) {
                LOG_E("Falha ao carregar manifesto de instalacao: %s", source_path);
                return err;
            }
        }
    }

    if (!tab5_manifest_is_valid(&manifest)) {
        LOG_E("Manifesto de %s e invalido", source_path);
        return TAB5_ERR_INVALID_ARG;
    }

    std::string target_dir = std::string(TAB5_APPS_INSTALLED_DIR) + "/" + manifest.id;
    mkdir(target_dir.c_str(), 0755);

    if (is_tar) {
        tab5_package_extract_tar(source_path, target_dir.c_str());
    } else {
        std::string manifest_file = std::string(source_path) + "/manifest.json";
        std::string dest_manifest = target_dir + "/manifest.json";
        copy_file_contents(manifest_file.c_str(), dest_manifest.c_str());

        std::string src_wasm = std::string(source_path) + "/" + manifest.entry;
        std::string dest_wasm = target_dir + "/" + manifest.entry;
        copy_file_contents(src_wasm.c_str(), dest_wasm.c_str());
    }

    // Cria diretório de dados privados da app
    std::string data_dir = std::string(TAB5_APPS_DATA_DIR) + "/" + manifest.id;
    mkdir(data_dir.c_str(), 0755);

    // Registra dynamic entry
    register_dynamic_app_entry(manifest, target_dir, false);

    if (out_app_id != nullptr && id_buf_size > 0) {
        strncpy(out_app_id, manifest.id, id_buf_size - 1);
        out_app_id[id_buf_size - 1] = '\0';
    }

    LOG_I("App %s instalada com sucesso em %s", manifest.id, target_dir.c_str());
    return TAB5_OK;
}

tab5_err_t tab5_package_mgr_uninstall(const char *app_id, bool delete_user_data)
{
    if (app_id == nullptr || app_id[0] == '\0') {
        return TAB5_ERR_INVALID_ARG;
    }

    // Se estiver em execução, fecha antes
    if (s_running_dynamic_app && s_running_dynamic_app->id_str == app_id) {
        tab5_package_mgr_close_active();
    }

    // Remove do registry
    app_registry_unregister(app_id);
    s_dynamic_apps.erase(app_id);

    // Remove pasta instalada
    std::string target_dir = std::string(TAB5_APPS_INSTALLED_DIR) + "/" + app_id;
    remove_dir_recursive(target_dir.c_str());

    if (delete_user_data) {
        std::string data_dir = std::string(TAB5_APPS_DATA_DIR) + "/" + app_id;
        remove_dir_recursive(data_dir.c_str());
    }

    LOG_I("App %s desinstalada com sucesso", app_id);
    return TAB5_OK;
}

static int scan_directory_and_register(const char *base_dir, bool is_embedded)
{
    int count = 0;
    DIR *d = opendir(base_dir);
    if (d == nullptr) {
        return 0;
    }

    struct dirent *entry;
    std::set<std::string> inspected_dirs;

    while ((entry = readdir(d)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        std::string name = entry->d_name;
        size_t slash_pos = name.find('/');
        std::string pkg_name = (slash_pos != std::string::npos) ? name.substr(0, slash_pos) : name;

        if (inspected_dirs.count(pkg_name) > 0) {
            continue;
        }
        inspected_dirs.insert(pkg_name);

        std::string app_dir = std::string(base_dir) + "/" + pkg_name;
        tab5_manifest_t manifest = {};

        // Caso 1: Arquivo .tab5pkg
        if (pkg_name.length() > 8 && pkg_name.substr(pkg_name.length() - 8) == ".tab5pkg") {
            if (tab5_package_read_manifest_from_tar(app_dir.c_str(), &manifest)) {
                if (register_dynamic_app_entry(manifest, app_dir, is_embedded) == TAB5_OK) {
                    count++;
                }
            }
        } else {
            // Caso 2: Pasta descompactada
            std::string manifest_path = app_dir + "/manifest.json";
            if (tab5_manifest_load_from_file(manifest_path.c_str(), &manifest) == TAB5_OK) {
                if (register_dynamic_app_entry(manifest, app_dir, is_embedded) == TAB5_OK) {
                    count++;
                }
            }
        }
    }
    closedir(d);
    return count;
}

int tab5_package_mgr_scan_and_register_all(void)
{
    int embedded_count = scan_directory_and_register(TAB5_APPS_EMBEDDED_DIR, true);
    int sd_count = scan_directory_and_register(TAB5_APPS_INSTALLED_DIR, false);
    (void)embedded_count;
    (void)sd_count;
    LOG_I("Varredura de apps concluida: %d embutidas, %d instaladas no SD", embedded_count, sd_count);
    return static_cast<int>(s_dynamic_apps.size());
}

tab5_err_t tab5_package_mgr_get_app_info(const char *app_id, tab5_installed_app_info_t *out_info)
{
    if (app_id == nullptr || out_info == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    auto it = s_dynamic_apps.find(app_id);
    if (it == s_dynamic_apps.end()) {
        return TAB5_ERR_NOT_FOUND;
    }

    out_info->manifest = it->second->manifest;
    strncpy(out_info->install_path, it->second->install_dir.c_str(), sizeof(out_info->install_path) - 1);
    out_info->is_embedded = it->second->is_embedded;
    return TAB5_OK;
}

extern "C" tab5_err_t tab5_package_mgr_launch_direct(const char *app_id, const char *open_file_path)
{
    if (app_id == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    auto it = s_dynamic_apps.find(app_id);
    if (it == s_dynamic_apps.end()) {
        LOG_E("App nao encontrada no package manager: %s", app_id);
        return TAB5_ERR_NOT_FOUND;
    }

    DynamicAppEntry *entry = it->second.get();
    tab5_app_context_t *active_ctx = tab5_host_get_active_app();

    // A relaunch of the already active entry must not overwrite its instance
    // or context.  It is still valid to deliver a file request to it.
    if ((s_running_dynamic_app == entry || active_ctx == &entry->host_ctx) && entry->wasm_inst.is_running) {
        if (open_file_path != nullptr) {
            return tab5_lifecycle_host_open_file(active_ctx != nullptr ? active_ctx : &entry->host_ctx, open_file_path);
        }
        return TAB5_OK;
    }

    DynamicAppEntry *previous_app = s_running_dynamic_app;
    tab5_app_context_t *previous_ctx = active_ctx;

    LOG_I("Lançando aplicativo dinâmico: %s (dir=%s)", app_id, entry->install_dir.c_str());

    // Prepara o contexto, mas só o torna ativo depois que o módulo foi carregado.
    memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
    strncpy(entry->host_ctx.app_id, entry->manifest.id, sizeof(entry->host_ctx.app_id) - 1);
    strncpy(entry->host_ctx.app_name, entry->manifest.name, sizeof(entry->host_ctx.app_name) - 1);
    entry->host_ctx.permissions = entry->manifest.permissions;
    entry->host_ctx.state = TAB5_APP_STATE_UNINITIALIZED;

    // Carrega WASM
    tab5_err_t wasm_err = TAB5_ERR_NOT_FOUND;
    if (entry->install_dir.length() > 8 && entry->install_dir.substr(entry->install_dir.length() - 8) == ".tab5pkg") {
        std::vector<uint8_t> wasm_bytes;
        if (tab5_package_read_file_from_tar(entry->install_dir.c_str(), entry->manifest.entry, &wasm_bytes)) {
            LOG_I("Lidos %zu bytes de %s do TAR %s", wasm_bytes.size(), entry->manifest.entry,
                  entry->install_dir.c_str());
            wasm_err = tab5_wasm_load_from_bytes(wasm_bytes.data(), wasm_bytes.size(), entry->manifest.stack_size,
                                                 entry->manifest.heap_size, &entry->host_ctx, &entry->wasm_inst);
        } else {
            LOG_W("Arquivo %s nao encontrado dentro do pacote %s", entry->manifest.entry, entry->install_dir.c_str());
        }
    } else {
        std::string wasm_file = entry->install_dir + "/" + entry->manifest.entry;
        wasm_err = tab5_wasm_load_from_file(wasm_file.c_str(), entry->manifest.stack_size, entry->manifest.heap_size,
                                            &entry->host_ctx, &entry->wasm_inst);
    }

    if (wasm_err != TAB5_OK) {
        LOG_E("Bytecode Wasm nao carregado (%s, err=%d)", entry->manifest.entry, (int)wasm_err);
        memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
        return wasm_err;
    }

    entry->host_ctx.is_wasm = true;
    entry->host_ctx.wasm_instance = &entry->wasm_inst;

    // O preflight ocorre antes de qualquer criação de tela. Assim, um pacote
    // sem entrypoint suportado não entra no caminho de rollback LVGL.
    const char *entrypoint = nullptr;
    tab5_err_t entrypoint_err = tab5_wasm_select_entrypoint(&entry->wasm_inst, &entrypoint);
    if (entrypoint_err != TAB5_OK || entrypoint == nullptr) {
        LOG_W("Nenhum entrypoint suportado em %s", app_id);
        tab5_wasm_unload(&entry->wasm_inst);
        memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
        tab5_host_set_active_app(previous_ctx);
        return entrypoint_err == TAB5_OK ? TAB5_ERR_NOT_FOUND : entrypoint_err;
    }

    // A inicialização candidata troca o active context. Em qualquer falha,
    // destrói a candidata e restaura o contexto anterior sem fechar sua app.
    tab5_err_t err = tab5_lifecycle_host_init_app(&entry->host_ctx);
    if (err != TAB5_OK) {
        tab5_lifecycle_host_abort_app(&entry->host_ctx);
        tab5_wasm_unload(&entry->wasm_inst);
        memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
        tab5_host_set_active_app(previous_ctx);
        return err;
    }

    LOG_I("Bytecode Wasm carregado com sucesso para %s, iniciando execucao...", app_id);

    // O preflight já selecionou o único símbolo que pode ser chamado.
    tab5_err_t entry_err = tab5_wasm_call_function(&entry->wasm_inst, entrypoint, 0, nullptr);
    if (entry_err != TAB5_OK) {
        LOG_E("Falha no ponto de entrada Wasm de %s (err=%d)", app_id, (int)entry_err);
        tab5_lifecycle_host_abort_app(&entry->host_ctx);
        tab5_wasm_unload(&entry->wasm_inst);
        memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
        tab5_host_set_active_app(previous_ctx);
        return entry_err;
    }

    err = tab5_lifecycle_host_resume_app(&entry->host_ctx);
    if (err != TAB5_OK) {
        tab5_lifecycle_host_abort_app(&entry->host_ctx);
        tab5_wasm_unload(&entry->wasm_inst);
        memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
        tab5_host_set_active_app(previous_ctx);
        return err;
    }

    if (open_file_path != nullptr) {
        err = tab5_lifecycle_host_open_file(&entry->host_ctx, open_file_path);
        if (err != TAB5_OK) {
            tab5_lifecycle_host_abort_app(&entry->host_ctx);
            tab5_wasm_unload(&entry->wasm_inst);
            memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
            tab5_host_set_active_app(previous_ctx);
            return err;
        }
    }

    // O candidato agora está validado; somente neste ponto fecha o anterior.
    if (previous_app != nullptr && previous_app != entry) {
        tab5_package_mgr_close_active();
    }

    s_running_dynamic_app = entry;
    LOG_I("App %s em execucao ativa", app_id);
    return TAB5_OK;
}

extern "C" tab5_err_t tab5_package_mgr_launch(const char *app_id, const char *open_file_path)
{
    if (app_id == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    if (s_dynamic_apps.find(app_id) == s_dynamic_apps.end()) {
        return TAB5_ERR_NOT_FOUND;
    }
    // The worker performs tab5_lifecycle_host_resume_app(&entry->host_ctx)
    // before tab5_package_mgr_close_active(); UI callers return immediately.
    // dispatch post: app_id and open_file_path are copied by value.
    return tab5_wasm_dispatch_post_launch(app_id, open_file_path);
}

tab5_err_t tab5_package_mgr_close_active(void)
{
    if (s_running_dynamic_app == nullptr) {
        return TAB5_OK;
    }

    DynamicAppEntry *entry = s_running_dynamic_app;

    // Uma abertura disparada por callback não pode destruir a tela/contexto
    // que ainda está na pilha. O runtime chamará o chokepoint após o join.
    if (entry->wasm_inst.call_depth > 0) {
        entry->wasm_inst.unload_pending = true;
        s_pending_close_app = entry;
        return TAB5_OK;
    }

    s_running_dynamic_app = nullptr;

    tab5_lifecycle_host_destroy_app(&entry->host_ctx);
    tab5_wasm_unload(&entry->wasm_inst);

    return TAB5_OK;
}

extern "C" void tab5_package_mgr_process_pending_close(void)
{
    DynamicAppEntry *entry = s_pending_close_app;
    if (entry == nullptr || entry->wasm_inst.call_depth != 0) {
        return;
    }

    s_pending_close_app = nullptr;
    if (s_running_dynamic_app == entry) {
        s_running_dynamic_app = nullptr;
    }
    tab5_lifecycle_host_destroy_app(&entry->host_ctx);
    tab5_wasm_unload(&entry->wasm_inst);
}
