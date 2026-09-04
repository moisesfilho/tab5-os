/**
 * @file tab5_storage_sandbox.cpp
 * @brief Implementação da Sandbox de Armazenamento e Sanitização de Caminhos
 */

#include "tab5_storage_sandbox.h"
#include "tab5_host_abi.h"
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "tab5_sandbox";
#define LOG_D(fmt, ...) ESP_LOGD(TAG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#else
#include <cstdio>
#define LOG_D(fmt, ...)
#define LOG_W(fmt, ...)
#endif

static bool is_valid_char(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    return (c == '.' || c == '_' || c == '-');
}

static bool is_valid_app_id(const char *app_id)
{
    if (app_id == nullptr || app_id[0] == '\0') {
        return false;
    }
    size_t len = strlen(app_id);
    if (len > 64) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!is_valid_char(app_id[i])) {
            return false;
        }
    }
    return true;
}

static bool normalize_path(const std::string &raw_path, std::string &out_canonical)
{
    if (raw_path.empty()) {
        return false;
    }

    std::vector<std::string> parts;
    std::string current;

    for (size_t i = 0; i <= raw_path.size(); ++i) {
        char c = (i < raw_path.size()) ? raw_path[i] : '\0';
        if (c == '/' || c == '\\' || c == '\0') {
            if (!current.empty()) {
                if (current == ".") {
                    // Ignora
                } else if (current == "..") {
                    if (parts.empty()) {
                        // Tentativa de escapar da raiz
                        return false;
                    }
                    parts.pop_back();
                } else {
                    parts.push_back(current);
                }
                current.clear();
            }
        } else {
            current += c;
        }
    }

    std::string res;
    for (const auto &p : parts) {
        res += "/" + p;
    }

    if (res.empty()) {
        res = "/";
    }

    out_canonical = res;
    return true;
}

static bool starts_with(const std::string &str, const std::string &prefix)
{
    return str.rfind(prefix, 0) == 0;
}

tab5_err_t tab5_storage_sandbox_get_app_dir(const char *app_id, char *out_buf, size_t buf_size)
{
    if (!is_valid_app_id(app_id) || out_buf == nullptr || buf_size == 0) {
        return TAB5_ERR_INVALID_ARG;
    }

    int needed = snprintf(out_buf, buf_size, "/sdcard/data/%s", app_id);
    if (needed < 0 || static_cast<size_t>(needed) >= buf_size) {
        return TAB5_ERR_BUFFER_OVERFLOW;
    }

    return TAB5_OK;
}

tab5_err_t tab5_storage_sandbox_resolve_path(const char *in_path, char *out_path, size_t out_size, const char *app_id,
                                             uint32_t permissions, bool write_access)
{
    if (in_path == nullptr || out_path == nullptr || out_size == 0 || !is_valid_app_id(app_id)) {
        return TAB5_ERR_INVALID_ARG;
    }

    std::string app_data_dir = std::string("/sdcard/data/") + app_id;
    std::string app_assets_dir = std::string("/sdcard/apps/installed/") + app_id;

    std::string raw(in_path);
    std::string combined;

    // Se for relativo, ancora em /sdcard/data/<app_id>/
    bool is_relative = (raw.empty() || (raw[0] != '/' && raw[0] != '\\'));
    if (is_relative) {
        combined = app_data_dir + "/" + raw;
    } else {
        combined = raw;
    }

    std::string canonical;
    if (!normalize_path(combined, canonical)) {
        LOG_W("Path traversal detectado: %s", in_path);
        return TAB5_ERR_ACCESS_DENIED;
    }

    // Se a chamada foi com caminho relativo, é estritamente proibido escapar de /sdcard/data/<app_id>
    if (is_relative) {
        if (canonical != app_data_dir && !starts_with(canonical, app_data_dir + "/")) {
            LOG_W("Caminho relativo tentou escapar do diretorio da app: %s", in_path);
            return TAB5_ERR_ACCESS_DENIED;
        }
    }

    // 1. Acesso ao diretório privado de dados da app (/sdcard/data/<app_id>)
    if (canonical == app_data_dir || starts_with(canonical, app_data_dir + "/")) {
        if (canonical.size() >= out_size) {
            return TAB5_ERR_BUFFER_OVERFLOW;
        }
        strncpy(out_path, canonical.c_str(), out_size - 1);
        out_path[out_size - 1] = '\0';
        return TAB5_OK;
    }

    // 2. Acesso aos assets instalados da própria app (somente leitura)
    if (canonical == app_assets_dir || starts_with(canonical, app_assets_dir + "/")) {
        if (write_access) {
            LOG_W("Tentativa de escrita no diretorio de assets instalado: %s", canonical.c_str());
            return TAB5_ERR_ACCESS_DENIED;
        }
        if (canonical.size() >= out_size) {
            return TAB5_ERR_BUFFER_OVERFLOW;
        }
        strncpy(out_path, canonical.c_str(), out_size - 1);
        out_path[out_size - 1] = '\0';
        return TAB5_OK;
    }

    // 3. Proibição de acesso aos dados de OUTRAS aplicações (/sdcard/data/...)
    if (starts_with(canonical, "/sdcard/data/")) {
        LOG_W("Acesso negado aos dados de outra app: %s", canonical.c_str());
        return TAB5_ERR_ACCESS_DENIED;
    }

    // 4. Proibição de acesso aos binários instalados de outras apps (/sdcard/apps/installed/...)
    if (starts_with(canonical, "/sdcard/apps/installed/")) {
        LOG_W("Acesso negado a pacote instalado de outra app: %s", canonical.c_str());
        return TAB5_ERR_ACCESS_DENIED;
    }

    // 5. Acesso ao restante do SD compartilhado (/sdcard/...)
    if (starts_with(canonical, "/sdcard/") || canonical == "/sdcard") {
        if (write_access) {
            if (!(permissions & TAB5_PERM_STORAGE_WRITE)) {
                LOG_W("Sem permissao STORAGE_WRITE para %s", canonical.c_str());
                return TAB5_ERR_ACCESS_DENIED;
            }
        } else {
            if (!(permissions & TAB5_PERM_STORAGE_READ)) {
                LOG_W("Sem permissao STORAGE_READ para %s", canonical.c_str());
                return TAB5_ERR_ACCESS_DENIED;
            }
        }

        if (canonical.size() >= out_size) {
            return TAB5_ERR_BUFFER_OVERFLOW;
        }
        strncpy(out_path, canonical.c_str(), out_size - 1);
        out_path[out_size - 1] = '\0';
        return TAB5_OK;
    }

    // Qualquer outro caminho do sistema operacional é estritamente bloqueado
    LOG_W("Acesso fora do sandbox /sdcard bloqueado: %s", canonical.c_str());
    return TAB5_ERR_ACCESS_DENIED;
}

tab5_err_t tab5_storage_sandbox_mkdir(const char *rel_or_abs_path, const char *app_id, uint32_t permissions)
{
    char safe_path[256];
    tab5_err_t err =
        tab5_storage_sandbox_resolve_path(rel_or_abs_path, safe_path, sizeof(safe_path), app_id, permissions, true);
    if (err != TAB5_OK) {
        return err;
    }

    // Cria recursivamente os diretórios pais
    char temp[256];
    strncpy(temp, safe_path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    for (char *p = temp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(temp, 0755);
            *p = '/';
        }
    }

    if (mkdir(safe_path, 0755) != 0) {
        struct stat st;
        if (stat(safe_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            return TAB5_OK; // Já existia
        }
        return TAB5_ERR_FAIL;
    }

    return TAB5_OK;
}

tab5_err_t tab5_storage_sandbox_remove(const char *rel_or_abs_path, const char *app_id, uint32_t permissions)
{
    char safe_path[256];
    tab5_err_t err =
        tab5_storage_sandbox_resolve_path(rel_or_abs_path, safe_path, sizeof(safe_path), app_id, permissions, true);
    if (err != TAB5_OK) {
        return err;
    }

    if (unlink(safe_path) != 0) {
        return TAB5_ERR_NOT_FOUND;
    }

    return TAB5_OK;
}

tab5_err_t tab5_storage_sandbox_scandir(const char *rel_or_abs_path, tab5_dir_entry_t *entries, uint32_t max_entries,
                                        uint32_t *out_count, const char *app_id, uint32_t permissions)
{
    if (rel_or_abs_path == nullptr || entries == nullptr || max_entries == 0 || out_count == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }
    *out_count = 0;

    char safe_path[256];
    tab5_err_t err =
        tab5_storage_sandbox_resolve_path(rel_or_abs_path, safe_path, sizeof(safe_path), app_id, permissions, false);
    if (err != TAB5_OK) {
        return err;
    }

    DIR *d = opendir(safe_path);
    if (d == nullptr) {
        return TAB5_ERR_NOT_FOUND;
    }

    uint32_t count = 0;
    struct dirent *de;
    while ((de = readdir(d)) != nullptr && count < max_entries) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }
        strncpy(entries[count].name, de->d_name, sizeof(entries[count].name) - 1);
        entries[count].name[sizeof(entries[count].name) - 1] = '\0';
        entries[count].is_dir = 0;
        entries[count].size = 0;
        entries[count].mtime = 0;

        std::string full_item = std::string(safe_path) + "/" + de->d_name;
        struct stat st;
        if (stat(full_item.c_str(), &st) == 0) {
            entries[count].size = (uint32_t)st.st_size;
            entries[count].mtime = (uint32_t)st.st_mtime;
            if (S_ISDIR(st.st_mode)) {
                entries[count].is_dir = 1;
            }
        }
        count++;
    }
    closedir(d);
    *out_count = count;
    return TAB5_OK;
}
