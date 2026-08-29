/**
 * @file tab5_manifest.cpp
 * @brief Implementação do Parser e Validator de Manifestos de Aplicações
 */

#include "tab5_manifest.h"
#include "cJSON.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "tab5_manifest";
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) ESP_LOGE(TAG, fmt, ##__VA_ARGS__)
#else
#define LOG_W(fmt, ...)
#define LOG_E(fmt, ...)
#endif

static bool is_valid_char(char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        return true;
    }
    return (c == '.' || c == '_' || c == '-');
}

bool tab5_manifest_is_valid(const tab5_manifest_t *manifest)
{
    if (manifest == nullptr) {
        return false;
    }
    if (manifest->id[0] == '\0' || manifest->name[0] == '\0') {
        return false;
    }
    // Validação básica do formato do app ID (ex: "com.tab5.app")
    size_t id_len = strlen(manifest->id);
    if (id_len < 3 || id_len > 63) {
        return false;
    }
    for (size_t i = 0; i < id_len; i++) {
        if (!is_valid_char(manifest->id[i])) {
            return false;
        }
    }
    return true;
}

tab5_err_t tab5_manifest_parse_json(const char *json_str, tab5_manifest_t *out_manifest)
{
    if (json_str == nullptr || out_manifest == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    memset(out_manifest, 0, sizeof(*out_manifest));

    // Valores padrão
    strncpy(out_manifest->version, "1.0.0", sizeof(out_manifest->version) - 1);
    strncpy(out_manifest->entry, "app.wasm", sizeof(out_manifest->entry) - 1);
    out_manifest->stack_size = TAB5_WASM_DEFAULT_STACK_SIZE;
    out_manifest->heap_size = TAB5_WASM_DEFAULT_HEAP_SIZE;

    cJSON *root = cJSON_Parse(json_str);
    if (root == nullptr) {
        LOG_E("Falha ao analisar JSON do manifesto");
        return TAB5_ERR_FAIL;
    }

    cJSON *item_id = cJSON_GetObjectItem(root, "id");
    if (item_id != nullptr && cJSON_IsString(item_id)) {
        strncpy(out_manifest->id, item_id->valuestring, sizeof(out_manifest->id) - 1);
    }

    cJSON *item_name = cJSON_GetObjectItem(root, "name");
    if (item_name != nullptr && cJSON_IsString(item_name)) {
        strncpy(out_manifest->name, item_name->valuestring, sizeof(out_manifest->name) - 1);
    }

    cJSON *item_version = cJSON_GetObjectItem(root, "version");
    if (item_version != nullptr && cJSON_IsString(item_version)) {
        strncpy(out_manifest->version, item_version->valuestring, sizeof(out_manifest->version) - 1);
    }

    cJSON *item_author = cJSON_GetObjectItem(root, "author");
    if (item_author != nullptr && cJSON_IsString(item_author)) {
        strncpy(out_manifest->author, item_author->valuestring, sizeof(out_manifest->author) - 1);
    }

    cJSON *item_desc = cJSON_GetObjectItem(root, "description");
    if (item_desc != nullptr && cJSON_IsString(item_desc)) {
        strncpy(out_manifest->description, item_desc->valuestring, sizeof(out_manifest->description) - 1);
    }

    cJSON *item_entry = cJSON_GetObjectItem(root, "entry");
    if (item_entry != nullptr && cJSON_IsString(item_entry)) {
        strncpy(out_manifest->entry, item_entry->valuestring, sizeof(out_manifest->entry) - 1);
    }

    // Icon parsing
    cJSON *item_icon = cJSON_GetObjectItem(root, "icon");
    if (item_icon != nullptr) {
        if (cJSON_IsObject(item_icon)) {
            cJSON *sym = cJSON_GetObjectItem(item_icon, "symbol");
            if (sym != nullptr && cJSON_IsString(sym)) {
                strncpy(out_manifest->icon_symbol, sym->valuestring, sizeof(out_manifest->icon_symbol) - 1);
            }
            cJSON *bg = cJSON_GetObjectItem(item_icon, "bg_color");
            if (bg != nullptr && cJSON_IsString(bg)) {
                strncpy(out_manifest->icon_bg_color, bg->valuestring, sizeof(out_manifest->icon_bg_color) - 1);
            }
        } else if (cJSON_IsString(item_icon)) {
            strncpy(out_manifest->icon_symbol, item_icon->valuestring, sizeof(out_manifest->icon_symbol) - 1);
        }
    }

    // File associations
    cJSON *item_file_assoc = cJSON_GetObjectItem(root, "file_associations");
    if (item_file_assoc != nullptr && cJSON_IsArray(item_file_assoc)) {
        int count = cJSON_GetArraySize(item_file_assoc);
        if (count > TAB5_MANIFEST_MAX_FILE_ASSOC) {
            count = TAB5_MANIFEST_MAX_FILE_ASSOC;
        }
        for (int i = 0; i < count; i++) {
            cJSON *ext_elem = cJSON_GetArrayItem(item_file_assoc, i);
            if (ext_elem != nullptr && cJSON_IsString(ext_elem)) {
                strncpy(out_manifest->file_associations[out_manifest->file_assoc_count], ext_elem->valuestring,
                        TAB5_MANIFEST_EXT_LEN - 1);
                out_manifest->file_assoc_count++;
            }
        }
    }

    // Permissions
    cJSON *item_perms = cJSON_GetObjectItem(root, "permissions");
    if (item_perms != nullptr && cJSON_IsArray(item_perms)) {
        int count = cJSON_GetArraySize(item_perms);
        for (int i = 0; i < count; i++) {
            cJSON *perm_elem = cJSON_GetArrayItem(item_perms, i);
            if (perm_elem != nullptr && cJSON_IsString(perm_elem)) {
                const char *p = perm_elem->valuestring;
                if (strcmp(p, "storage.read") == 0 || strcmp(p, "storage_read") == 0) {
                    out_manifest->permissions |= TAB5_PERM_STORAGE_READ;
                } else if (strcmp(p, "storage.write") == 0 || strcmp(p, "storage_write") == 0) {
                    out_manifest->permissions |= TAB5_PERM_STORAGE_WRITE;
                } else if (strcmp(p, "storage.readwrite") == 0 || strcmp(p, "storage_readwrite") == 0) {
                    out_manifest->permissions |= (TAB5_PERM_STORAGE_READ | TAB5_PERM_STORAGE_WRITE);
                } else if (strcmp(p, "ui.keyboard") == 0 || strcmp(p, "ui_keyboard") == 0) {
                    out_manifest->permissions |= TAB5_PERM_UI_KEYBOARD;
                } else if (strcmp(p, "network") == 0) {
                    out_manifest->permissions |= TAB5_PERM_NETWORK;
                } else if (strcmp(p, "bluetooth") == 0) {
                    out_manifest->permissions |= TAB5_PERM_BLUETOOTH;
                } else if (strcmp(p, "audio") == 0) {
                    out_manifest->permissions |= TAB5_PERM_AUDIO;
                }
            }
        }
    }

    // Stack / Heap sizes
    cJSON *item_stack = cJSON_GetObjectItem(root, "stack_size");
    if (item_stack != nullptr && cJSON_IsNumber(item_stack)) {
        out_manifest->stack_size = (uint32_t)item_stack->valueint;
    }

    cJSON *item_heap = cJSON_GetObjectItem(root, "heap_size");
    if (item_heap != nullptr && cJSON_IsNumber(item_heap)) {
        out_manifest->heap_size = (uint32_t)item_heap->valueint;
    }

    cJSON_Delete(root);

    if (!tab5_manifest_is_valid(out_manifest)) {
        LOG_W("Manifesto invalido: campos obrigatorios ausentes ou invalidos");
        return TAB5_ERR_INVALID_ARG;
    }

    return TAB5_OK;
}

tab5_err_t tab5_manifest_load_from_file(const char *filepath, tab5_manifest_t *out_manifest)
{
    if (filepath == nullptr || out_manifest == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    FILE *f = fopen(filepath, "r");
    if (f == nullptr) {
        return TAB5_ERR_NOT_FOUND;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0 || size > 64 * 1024) {
        fclose(f);
        return TAB5_ERR_FAIL;
    }

    char *buf = (char *)calloc(1, size + 1);
    if (buf == nullptr) {
        fclose(f);
        return TAB5_ERR_NO_MEM;
    }

    size_t read_bytes = fread(buf, 1, size, f);
    fclose(f);
    if (read_bytes == 0) {
        free(buf);
        return TAB5_ERR_FAIL;
    }

    tab5_err_t err = tab5_manifest_parse_json(buf, out_manifest);
    free(buf);
    return err;
}

int tab5_manifest_version_compare(const char *v1, const char *v2)
{
    if (v1 == nullptr && v2 == nullptr) {
        return 0;
    }
    if (v1 == nullptr) {
        return -1;
    }
    if (v2 == nullptr) {
        return 1;
    }

    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;

    sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1);
    sscanf(v2, "%d.%d.%d", &maj2, &min2, &pat2);

    if (maj1 != maj2) {
        return (maj1 - maj2);
    }
    if (min1 != min2) {
        return (min1 - min2);
    }
    return (pat1 - pat2);
}
