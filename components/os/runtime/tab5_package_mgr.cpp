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

static std::vector<std::string> s_registered_app_ids;

template <size_t Index> struct AppSlot {
    static void launch()
    {
        if (Index < s_registered_app_ids.size()) {
            on_dynamic_app_launch(s_registered_app_ids[Index].c_str());
        }
    }
    static void open_file(const char *filepath)
    {
        if (Index < s_registered_app_ids.size()) {
            on_dynamic_app_open_file(s_registered_app_ids[Index].c_str(), filepath);
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
    s_registered_app_ids.clear();
    s_running_dynamic_app = nullptr;
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
            if (cmp >= 0) {
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

    size_t slot = s_registered_app_ids.size();
    if (slot >= sizeof(s_trampolines) / sizeof(s_trampolines[0])) {
        LOG_E("Limite maximo de apps dinamicas atingido");
        return TAB5_ERR_NO_MEM;
    }

    s_registered_app_ids.push_back(id);

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

        if (inspected_dirs.contains(pkg_name)) {
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

tab5_err_t tab5_package_mgr_launch(const char *app_id, const char *open_file_path)
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

    // Fecha app anterior se houver
    if (s_running_dynamic_app != nullptr) {
        tab5_package_mgr_close_active();
    }

    // Prepara contexto
    memset(&entry->host_ctx, 0, sizeof(entry->host_ctx));
    strncpy(entry->host_ctx.app_id, entry->manifest.id, sizeof(entry->host_ctx.app_id) - 1);
    strncpy(entry->host_ctx.app_name, entry->manifest.name, sizeof(entry->host_ctx.app_name) - 1);
    entry->host_ctx.permissions = entry->manifest.permissions;
    entry->host_ctx.state = TAB5_APP_STATE_UNINITIALIZED;

    tab5_err_t err = tab5_lifecycle_host_init_app(&entry->host_ctx);
    if (err != TAB5_OK) {
        return err;
    }

    // Carrega WASM
    std::string wasm_file = entry->install_dir + "/" + entry->manifest.entry;
    err = tab5_wasm_load_from_file(wasm_file.c_str(), entry->manifest.stack_size, entry->manifest.heap_size,
                                   &entry->host_ctx, &entry->wasm_inst);
    if (err != TAB5_OK) {
        LOG_W("Bytecode Wasm nao encontrado (%s), rodando em modo container nativo", wasm_file.c_str());
    } else {
        // Tenta app_main, depois main, depois _start
        if (tab5_wasm_call_function(&entry->wasm_inst, "app_main", 0, nullptr) != TAB5_OK) {
            if (tab5_wasm_call_function(&entry->wasm_inst, "main", 0, nullptr) != TAB5_OK) {
                tab5_wasm_call_function(&entry->wasm_inst, "_start", 0, nullptr);
            }
        }
    }

    tab5_lifecycle_host_resume_app(&entry->host_ctx);

    if (open_file_path != nullptr) {
        tab5_lifecycle_host_open_file(&entry->host_ctx, open_file_path);
    }

    s_running_dynamic_app = entry;
    return TAB5_OK;
}

tab5_err_t tab5_package_mgr_close_active(void)
{
    if (s_running_dynamic_app == nullptr) {
        return TAB5_OK;
    }

    DynamicAppEntry *entry = s_running_dynamic_app;
    s_running_dynamic_app = nullptr;

    tab5_lifecycle_host_destroy_app(&entry->host_ctx);
    tab5_wasm_unload(&entry->wasm_inst);

    return TAB5_OK;
}
