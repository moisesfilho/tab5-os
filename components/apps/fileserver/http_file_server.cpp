#include "http_file_server.h"
#include "ai_storage.h"
#include "wifi_mgr.h"
#include "wifi_storage.h"
#include "bt_mgr.h"
#include "bt_storage.h"
#include "display_storage.h"
#include "bsp/display.h"
#include "bsp/m5stack_tab5.h"
#include "imu_reader.h"
#include "orientation.h"
#include "timezone_mgr.h"
#include "ui_screensaver.h"
#include "ui_theme.h"
#include "ui_status.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>

static const char *TAG = "tab5_http_server";
static httpd_handle_t s_server = nullptr;

/* Helpers para decodificação e parsing de formulários / JSON */
static std::string url_decode(const std::string &src)
{
    std::string ret;
    for (size_t i = 0; i < src.length(); i++) {
        if (src[i] == '%' && i + 2 < src.length()) {
            unsigned int ii = 0;
            if (sscanf(src.substr(i + 1, 2).c_str(), "%x", &ii) == 1) {
                ret += static_cast<char>(ii);
                i += 2;
            } else {
                ret += src[i];
            }
        } else if (src[i] == '+') {
            ret += ' ';
        } else {
            ret += src[i];
        }
    }
    return ret;
}

static std::string get_query_param(const char *query, const char *key)
{
    if (!query || !key) {
        return "";
    }
    char val[256] = {0};
    if (httpd_query_key_value(query, key, val, sizeof(val)) == ESP_OK) {
        return url_decode(val);
    }
    return "";
}

static std::string read_req_body(httpd_req_t *req)
{
    int total_len = req->content_len;
    if (total_len <= 0 || total_len > 8192) {
        return "";
    }
    std::vector<char> buf(total_len + 1, 0);
    int cur_len = 0;
    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf.data() + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return "";
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    return std::string(buf.data(), total_len);
}

/* Sanitização segura de caminhos do cartão SD */
static bool is_safe_sd_path(const std::string &path)
{
    if (path.empty() || path.rfind("/sdcard", 0) != 0) {
        return false;
    }
    if (path.find("..") != std::string::npos) {
        return false;
    }
    return true;
}

/* ========================================================================= */
/* Handlers do Explorador de Arquivos e Downloads                            */
/* ========================================================================= */

struct FileItem {
    std::string name;
    bool is_dir;
    size_t size;
    time_t mtime;
};

static std::vector<FileItem> list_sd_directory(const std::string &dir_path)
{
    std::vector<FileItem> items;
    wifi_storage_mount();

    DIR *d = opendir(dir_path.c_str());
    if (!d) {
        ESP_LOGE(TAG, "opendir(%s) falhou: errno=%d (%s)", dir_path.c_str(), errno, strerror(errno));
        return items;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        std::string full_path = dir_path;
        if (full_path.back() != '/') {
            full_path += "/";
        }
        full_path += entry->d_name;

        struct stat st;
        FileItem item;
        item.name = entry->d_name;
        item.is_dir = false;
        item.size = 0;
        item.mtime = 0;

        if (stat(full_path.c_str(), &st) == 0) {
            item.is_dir = S_ISDIR(st.st_mode);
            item.size = st.st_size;
            item.mtime = st.st_mtime;
        }
        items.push_back(item);
    }
    closedir(d);

    std::sort(items.begin(), items.end(), [](const FileItem &a, const FileItem &b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir > b.is_dir;
        }
        return a.name < b.name;
    });

    return items;
}

static esp_err_t api_files_handler(httpd_req_t *req)
{
    char query[256] = {0};
    std::string path = "/sdcard";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        std::string q_path = get_query_param(query, "path");
        if (!q_path.empty() && is_safe_sd_path(q_path)) {
            path = q_path;
        }
    }

    std::vector<FileItem> items = list_sd_directory(path);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "path", path.c_str());

    std::string parent = "/sdcard";
    size_t last_slash = path.find_last_of('/');
    if (last_slash != std::string::npos && last_slash > 0 && path != "/sdcard") {
        parent = path.substr(0, last_slash);
        if (parent.length() < 7) {
            parent = "/sdcard";
        }
    }
    cJSON_AddStringToObject(root, "parent", parent.c_str());

    cJSON *arr = cJSON_CreateArray();
    for (const auto &it : items) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", it.name.c_str());
        cJSON_AddBoolToObject(obj, "is_dir", it.is_dir);
        cJSON_AddNumberToObject(obj, "size", it.size);
        cJSON_AddNumberToObject(obj, "mtime", it.mtime);
        cJSON_AddItemToArray(arr, obj);
    }
    cJSON_AddItemToObject(root, "files", arr);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

static esp_err_t download_handler(httpd_req_t *req)
{
    char query[512] = {0};
    std::string filepath;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        filepath = get_query_param(query, "file");
    }

    if (filepath.empty() || !is_safe_sd_path(filepath)) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    wifi_storage_mount();
    FILE *fp = fopen(filepath.c_str(), "rb");
    if (!fp) {
        ESP_LOGE(TAG, "arquivo nao encontrado: %s", filepath.c_str());
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    const char *dot = strrchr(filepath.c_str(), '.');
    if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)) {
        httpd_resp_set_type(req, "image/jpeg");
    } else if (dot && strcasecmp(dot, ".png") == 0) {
        httpd_resp_set_type(req, "image/png");
    } else if (dot && strcasecmp(dot, ".wav") == 0) {
        httpd_resp_set_type(req, "audio/wav");
    } else if (dot && strcasecmp(dot, ".mp3") == 0) {
        httpd_resp_set_type(req, "audio/mpeg");
    } else if (dot && (strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".log") == 0 || strcasecmp(dot, ".cfg") == 0)) {
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
    } else if (dot && strcasecmp(dot, ".json") == 0) {
        httpd_resp_set_type(req, "application/json");
    } else {
        httpd_resp_set_type(req, "application/octet-stream");
    }

    char chunk[2048];
    while (!feof(fp) && !ferror(fp)) {
        size_t read_bytes = fread(chunk, 1, sizeof(chunk), fp);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                fclose(fp);
                return ESP_FAIL;
            }
        }
        if (read_bytes < sizeof(chunk)) {
            break;
        }
    }
    fclose(fp);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

static esp_err_t upload_handler(httpd_req_t *req)
{
    char query[512] = {0};
    std::string path = "/sdcard";
    std::string filename;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        std::string q_path = get_query_param(query, "path");
        if (!q_path.empty() && is_safe_sd_path(q_path)) {
            path = q_path;
        }
        filename = get_query_param(query, "filename");
    }

    if (filename.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Filename is required");
        return ESP_FAIL;
    }

    size_t slash_pos = filename.find_last_of("/\\");
    if (slash_pos != std::string::npos) {
        filename = filename.substr(slash_pos + 1);
    }
    if (filename.empty() || filename == "." || filename == "..") {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid filename");
        return ESP_FAIL;
    }

    std::string fullpath = path;
    if (fullpath.back() != '/') {
        fullpath += "/";
    }
    fullpath += filename;

    if (!is_safe_sd_path(fullpath)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid destination path");
        return ESP_FAIL;
    }

    /* Buffer de recepcao de rede na PSRAM (SPIRAM) para liberar heap interna */
    constexpr size_t NET_CHUNK = 4096;

    char *net_buf = static_cast<char *>(heap_caps_malloc(NET_CHUNK, MALLOC_CAP_SPIRAM));
    if (!net_buf) {
        net_buf = static_cast<char *>(malloc(NET_CHUNK));
    }
    if (!net_buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    /* Gravamos direto do net_buf (PSRAM). Nao alocamos buffer DMA proprio:
     * buffers nao-DMA/misaligned sao roteados pelo driver sdmmc pelo
     * dma_aligned_buffer de 4KB que o BSP aloca no mount (bsp_storage.c),
     * sem gastar a heap DMA interna do P4 (escassa, compartilhada com
     * esp_hosted/audio/camera) durante o upload. */

    wifi_storage_mount();

    FILE *fp = fopen(fullpath.c_str(), "wb");
    if (!fp) {
        free(net_buf);
        ESP_LOGE(TAG, "Falha ao criar arquivo no SD: %s (errno=%d)", fullpath.c_str(), errno);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file on SD");
        return ESP_FAIL;
    }
    int sd_fd = fileno(fp);

    /* Suporte a chunked transfer (sem Content-Length) e upload com Content-Length.
     * Quando content_len <= 0 o browser esta usando chunked encoding: lemos ate
     * httpd_req_recv retornar 0 (fim de stream). */
    int content_len = req->content_len;
    bool chunked = (content_len <= 0);
    int remaining = chunked ? static_cast<int>(NET_CHUNK) : content_len;
    size_t total_written = 0;
    esp_err_t ret = ESP_OK;

    while (remaining > 0) {
        int to_read = remaining > static_cast<int>(NET_CHUNK) ? static_cast<int>(NET_CHUNK) : remaining;
        int received = httpd_req_recv(req, net_buf, to_read);
        if (received == 0) {
            break; /* fim de stream (chunked) */
        }
        if (received < 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            ESP_LOGE(TAG, "Erro de recepcao durante upload de %s (recebido=%d)", fullpath.c_str(), received);
            ret = ESP_FAIL;
            break;
        }

        /* Grava direto do net_buf em fatias de ate 4096B */
        size_t net_offset = 0;
        while (net_offset < static_cast<size_t>(received)) {
            size_t slice = received - net_offset;
            ssize_t wr = write(sd_fd, net_buf + net_offset, slice);
            if (wr <= 0) {
                ESP_LOGE(TAG, "Erro de gravacao no SD: %s (errno=%d, offset=%zu)", fullpath.c_str(), errno, net_offset);
                ret = ESP_FAIL;
                break;
            }
            net_offset += wr;
            total_written += wr;
        }
        if (ret != ESP_OK) {
            break;
        }

        if (chunked) {
            remaining = static_cast<int>(NET_CHUNK); /* continua lendo */
        } else {
            remaining -= received;
        }
    }

    fclose(fp);
    free(net_buf);

    if (ret != ESP_OK) {
        unlink(fullpath.c_str());
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload error");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Upload concluido com sucesso: %s (%zu bytes)", fullpath.c_str(), total_written);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(root, "filename", filename.c_str());
    cJSON_AddStringToObject(root, "path", fullpath.c_str());
    cJSON_AddNumberToObject(root, "size", total_written);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_set_type(req, "application/json");
    if (json_str) {
        httpd_resp_send(req, json_str, strlen(json_str));
        free(json_str);
    } else {
        httpd_resp_send(req, "{\"success\":true}", 16);
    }

    return ESP_OK;
}

static esp_err_t api_mkdir_handler(httpd_req_t *req)
{
    char query[512] = {0};
    std::string path;
    std::string name;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        path = get_query_param(query, "path");
        name = get_query_param(query, "name");
    }

    if (path.empty() || name.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'path' or 'name' parameter");
        return ESP_FAIL;
    }

    if (!is_safe_sd_path(path)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }

    if (name == "." || name == ".." || name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid name");
        return ESP_FAIL;
    }

    std::string fullpath = path;
    if (fullpath.back() != '/') {
        fullpath += "/";
    }
    fullpath += name;

    wifi_storage_mount();

    struct stat st;
    if (stat(fullpath.c_str(), &st) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Already exists");
        return ESP_FAIL;
    }

    if (mkdir(fullpath.c_str(), 0775) != 0) {
        ESP_LOGE(TAG, "Falha ao criar pasta %s (errno=%d)", fullpath.c_str(), errno);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create directory");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Pasta criada: %s", fullpath.c_str());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", 16);
    return ESP_OK;
}

static esp_err_t api_delete_handler(httpd_req_t *req)
{
    char query[512] = {0};
    std::string filepath;

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        filepath = get_query_param(query, "file");
    }

    if (filepath.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing 'file' parameter");
        return ESP_FAIL;
    }

    if (!is_safe_sd_path(filepath)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_FAIL;
    }

    wifi_storage_mount();

    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    int rc;
    if (S_ISDIR(st.st_mode)) {
        rc = rmdir(filepath.c_str());
    } else {
        rc = unlink(filepath.c_str());
    }

    if (rc != 0) {
        ESP_LOGE(TAG, "Falha ao apagar %s (errno=%d)", filepath.c_str(), errno);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to delete");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Arquivo/pasta apagado: %s", filepath.c_str());

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", 16);
    return ESP_OK;
}

/* ========================================================================= */
/* Handlers de Configurações do Sistema (Status, Display, Wi-Fi, BT, TZ)     */
/* ========================================================================= */

static esp_err_t api_system_status_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();

    /* Brilho */
    int cur_brightness = DISPLAY_DEFAULT_BRIGHTNESS;
    display_storage_load_brightness(&cur_brightness);
    cJSON_AddNumberToObject(root, "brightness", cur_brightness);

    /* Tema */
    cJSON_AddBoolToObject(root, "theme_dark", ui_theme_is_dark());

    /* Rotação */
    cJSON_AddBoolToObject(root, "rot_enabled", imu_reader_is_rotation_enabled());
    lv_disp_rotation_t cur_rot = LV_DISPLAY_ROTATION_0;
    display_storage_load_rotation(&cur_rot);
    cJSON_AddNumberToObject(root, "rotation", static_cast<int>(cur_rot));

    /* Protetor de Tela */
    cJSON_AddNumberToObject(root, "ss_timeout", ui_screensaver_get_timeout());

    /* Fuso Horário */
    int tz_offset = timezone_mgr_get_offset();
    char tz_str[16] = {0};
    timezone_mgr_format_offset(tz_offset, tz_str, sizeof(tz_str));
    cJSON_AddNumberToObject(root, "tz_offset", tz_offset);
    cJSON_AddStringToObject(root, "tz_str", tz_str);

    struct tm cur_time;
    if (timezone_mgr_get_localtime(&cur_time) != nullptr) {
        char time_buf[64];
        snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d (%02d/%02d/%04d)", cur_time.tm_hour, cur_time.tm_min,
                 cur_time.tm_sec, cur_time.tm_mday, cur_time.tm_mon + 1, cur_time.tm_year + 1900);
        cJSON_AddStringToObject(root, "time_str", time_buf);
    } else {
        cJSON_AddStringToObject(root, "time_str", "--:--:--");
    }

    /* Wi-Fi */
    cJSON *wifi_obj = cJSON_CreateObject();
    wifi_status_t w_stat = {};
    wifi_mgr_get_status(&w_stat);
    cJSON_AddBoolToObject(wifi_obj, "enabled", wifi_mgr_is_enabled());
    cJSON_AddBoolToObject(wifi_obj, "connected", w_stat.connected);
    cJSON_AddStringToObject(wifi_obj, "ssid", w_stat.ssid);
    cJSON_AddStringToObject(wifi_obj, "ip", w_stat.ip);
    cJSON_AddItemToObject(root, "wifi", wifi_obj);

    /* Bluetooth */
    cJSON *bt_obj = cJSON_CreateObject();
    bt_status_t b_stat = {};
    bt_mgr_get_status(&b_stat);
    cJSON_AddBoolToObject(bt_obj, "enabled", bt_mgr_is_enabled());
    cJSON_AddBoolToObject(bt_obj, "any_connected", b_stat.any_connected);
    cJSON_AddNumberToObject(bt_obj, "connected_count", b_stat.connected_count);
    cJSON_AddStringToObject(bt_obj, "last_name", b_stat.last_connected_name);
    cJSON_AddStringToObject(bt_obj, "last_mac", b_stat.last_connected_mac);
    cJSON_AddItemToObject(root, "bluetooth", bt_obj);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

static esp_err_t api_settings_display_handler(httpd_req_t *req)
{
    std::string body = read_req_body(req);
    cJSON *root = cJSON_Parse(body.c_str());

    if (root) {
        /* Brilho */
        cJSON *j_br = cJSON_GetObjectItem(root, "brightness");
        if (j_br && j_br->type == cJSON_Number) {
            int br = j_br->valueint;
            if (br < DISPLAY_MIN_BRIGHTNESS)
                br = DISPLAY_MIN_BRIGHTNESS;
            if (br > DISPLAY_MAX_BRIGHTNESS)
                br = DISPLAY_MAX_BRIGHTNESS;
            bsp_display_brightness_set(br);
            display_storage_save_brightness(br);
            ESP_LOGI(TAG, "Brilho atualizado via web: %d%%", br);
        }

        /* Tema */
        cJSON *j_theme = cJSON_GetObjectItem(root, "theme");
        if (j_theme && j_theme->valuestring) {
            bool dark = (strcmp(j_theme->valuestring, "dark") == 0);
            if (bsp_display_lock(pdMS_TO_TICKS(500))) {
                ui_theme_set(dark);
                bsp_display_unlock();
            }
            ESP_LOGI(TAG, "Tema atualizado via web: %s", dark ? "Escuro" : "Claro");
        }

        /* Auto-Rotação IMU */
        cJSON *j_rot_en = cJSON_GetObjectItem(root, "rot_enabled");
        if (j_rot_en && cJSON_IsBool(j_rot_en)) {
            bool en = cJSON_IsTrue(j_rot_en);
            imu_reader_set_rotation_enabled(en);
            ESP_LOGI(TAG, "Auto-rotacao via web: %s", en ? "Ativada" : "Desativada");
        }

        /* Rotação Manual */
        cJSON *j_rot = cJSON_GetObjectItem(root, "rotation");
        if (j_rot && j_rot->type == cJSON_Number) {
            int rot_val = j_rot->valueint;
            if (rot_val >= 0 && rot_val <= 3) {
                lv_disp_rotation_t target_rot = static_cast<lv_disp_rotation_t>(rot_val);
                if (bsp_display_lock(pdMS_TO_TICKS(500))) {
                    lv_display_t *disp = lv_display_get_default();
                    if (disp) {
                        lv_display_set_rotation(disp, target_rot);
                    }
                    ui_status_set_rotation(target_rot);
                    display_storage_save_rotation(target_rot);
                    bsp_display_unlock();
                }
                ESP_LOGI(TAG, "Rotacao manual atualizada via web: %d", rot_val);
            }
        }

        /* Protetor de Tela Timeout */
        cJSON *j_ss = cJSON_GetObjectItem(root, "ss_timeout");
        if (j_ss && j_ss->type == cJSON_Number) {
            uint32_t to = static_cast<uint32_t>(j_ss->valueint);
            ui_screensaver_set_timeout(to);
            ESP_LOGI(TAG, "Timeout protetor de tela via web: %u s", (unsigned)to);
        }

        cJSON_Delete(root);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", 16);
    return ESP_OK;
}

static esp_err_t api_settings_timezone_handler(httpd_req_t *req)
{
    std::string body = read_req_body(req);
    cJSON *root = cJSON_Parse(body.c_str());
    if (root) {
        cJSON *j_offset = cJSON_GetObjectItem(root, "offset");
        if (j_offset && j_offset->type == cJSON_Number) {
            int off = j_offset->valueint;
            if (off >= TIMEZONE_MIN_OFFSET && off <= TIMEZONE_MAX_OFFSET) {
                timezone_mgr_set_offset(off);
                ESP_LOGI(TAG, "Fuso horario atualizado via web: UTC%+d", off);
            }
        }
        cJSON_Delete(root);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", 16);
    return ESP_OK;
}

/* Estrutura para scan assíncrono de Wi-Fi */
struct WifiScanResult {
    SemaphoreHandle_t sem;
    std::vector<wifi_ap_record_t> aps;
};

static void http_wifi_scan_cb(const wifi_ap_record_t *aps, int count, void *ctx)
{
    auto *res = static_cast<WifiScanResult *>(ctx);
    if (res && aps && count > 0) {
        res->aps.assign(aps, aps + count);
    }
    if (res && res->sem) {
        xSemaphoreGive(res->sem);
    }
}

static esp_err_t api_wifi_scan_handler(httpd_req_t *req)
{
    if (!wifi_mgr_is_enabled()) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Wi-Fi desativado\",\"aps\":[]}", 51);
        return ESP_OK;
    }

    WifiScanResult res;
    res.sem = xSemaphoreCreateBinary();
    esp_err_t err = wifi_mgr_scan(http_wifi_scan_cb, &res);
    if (err == ESP_OK) {
        xSemaphoreTake(res.sem, pdMS_TO_TICKS(6000));
    }
    vSemaphoreDelete(res.sem);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    cJSON *arr = cJSON_CreateArray();
    for (const auto &ap : res.aps) {
        if (ap.ssid[0] == '\0')
            continue;
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "ssid", (const char *)ap.ssid);
        cJSON_AddNumberToObject(obj, "rssi", ap.rssi);
        cJSON_AddNumberToObject(obj, "auth", ap.authmode);
        cJSON_AddNumberToObject(obj, "channel", ap.primary);
        cJSON_AddItemToArray(arr, obj);
    }
    cJSON_AddItemToObject(root, "aps", arr);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

static esp_err_t api_settings_wifi_handler(httpd_req_t *req)
{
    std::string body = read_req_body(req);
    cJSON *root = cJSON_Parse(body.c_str());
    bool ok = false;

    if (root) {
        cJSON *j_act = cJSON_GetObjectItem(root, "action");
        if (j_act && j_act->valuestring) {
            std::string action = j_act->valuestring;
            if (action == "enable") {
                wifi_mgr_set_enabled(true);
                ok = true;
            } else if (action == "disable") {
                wifi_mgr_set_enabled(false);
                ok = true;
            } else if (action == "connect") {
                cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
                cJSON *j_pass = cJSON_GetObjectItem(root, "password");
                if (j_ssid && j_ssid->valuestring) {
                    const char *pass = (j_pass && j_pass->valuestring) ? j_pass->valuestring : "";
                    wifi_mgr_connect(j_ssid->valuestring, pass);
                    ok = true;
                }
            } else if (action == "disconnect") {
                wifi_mgr_disconnect();
                ok = true;
            } else if (action == "forget") {
                cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
                if (j_ssid && j_ssid->valuestring) {
                    wifi_mgr_forget(j_ssid->valuestring);
                    ok = true;
                }
            }
        }
        cJSON_Delete(root);
    }

    httpd_resp_set_type(req, "application/json");
    std::string resp = std::string("{\"success\":") + (ok ? "true" : "false") + "}";
    httpd_resp_send(req, resp.c_str(), resp.length());
    return ESP_OK;
}

static esp_err_t api_bluetooth_scan_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);

    /* Dispositivos salvos / conhecidos */
    bt_saved_list_t saved_list = {};
    bt_storage_load_all(&saved_list);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < saved_list.count; i++) {
        cJSON *obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "mac", saved_list.items[i].mac);
        cJSON_AddStringToObject(obj, "name", saved_list.items[i].name);
        cJSON_AddNumberToObject(obj, "type", saved_list.items[i].type);
        cJSON_AddBoolToObject(obj, "paired", saved_list.items[i].paired);
        cJSON_AddItemToArray(arr, obj);
    }
    cJSON_AddItemToObject(root, "devices", arr);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

static esp_err_t api_settings_bluetooth_handler(httpd_req_t *req)
{
    std::string body = read_req_body(req);
    cJSON *root = cJSON_Parse(body.c_str());
    bool ok = false;

    if (root) {
        cJSON *j_act = cJSON_GetObjectItem(root, "action");
        if (j_act && j_act->valuestring) {
            std::string action = j_act->valuestring;
            if (action == "enable") {
                bt_mgr_set_enabled(true);
                ok = true;
            } else if (action == "disable") {
                bt_mgr_set_enabled(false);
                ok = true;
            } else if (action == "connect") {
                cJSON *j_mac = cJSON_GetObjectItem(root, "mac");
                cJSON *j_name = cJSON_GetObjectItem(root, "name");
                cJSON *j_type = cJSON_GetObjectItem(root, "type");
                if (j_mac && j_mac->valuestring) {
                    const char *name = (j_name && j_name->valuestring) ? j_name->valuestring : "Dispositivo BT";
                    int type = (j_type && j_type->type == cJSON_Number) ? j_type->valueint : 0;
                    bt_mgr_connect(j_mac->valuestring, name, static_cast<bt_dev_type_t>(type));
                    ok = true;
                }
            } else if (action == "disconnect") {
                cJSON *j_mac = cJSON_GetObjectItem(root, "mac");
                if (j_mac && j_mac->valuestring) {
                    bt_mgr_disconnect(j_mac->valuestring);
                    ok = true;
                }
            } else if (action == "forget") {
                cJSON *j_mac = cJSON_GetObjectItem(root, "mac");
                if (j_mac && j_mac->valuestring) {
                    bt_mgr_forget(j_mac->valuestring);
                    ok = true;
                }
            }
        }
        cJSON_Delete(root);
    }

    httpd_resp_set_type(req, "application/json");
    std::string resp = std::string("{\"success\":") + (ok ? "true" : "false") + "}";
    httpd_resp_send(req, resp.c_str(), resp.length());
    return ESP_OK;
}

/* ========================================================================= */
/* Handlers de Chat IA (OpenAI / OpenCode Go)                                */
/* ========================================================================= */

static void parse_form_urlencoded(const std::string &body, ai_cfg_t &cfg)
{
    std::istringstream stream(body);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = url_decode(pair.substr(0, eq));
            std::string val = url_decode(pair.substr(eq + 1));
            if (key == "base_url") {
                snprintf(cfg.base_url, sizeof(cfg.base_url), "%s", val.c_str());
            } else if (key == "token") {
                snprintf(cfg.token, sizeof(cfg.token), "%s", val.c_str());
            } else if (key == "model") {
                snprintf(cfg.model, sizeof(cfg.model), "%s", val.c_str());
            } else if (key == "max_tokens") {
                int mt = atoi(val.c_str());
                if (mt > 0 && mt <= 8192) {
                    cfg.max_tokens = mt;
                }
            } else if (key == "timeout_sec") {
                int to = atoi(val.c_str());
                if (to >= 5 && to <= 300) {
                    cfg.timeout_sec = to;
                }
            }
        }
    }
}

static esp_err_t ai_save_handler(httpd_req_t *req)
{
    std::string body = read_req_body(req);
    if (body.empty()) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    ai_cfg_t cfg;
    ai_storage_load(&cfg);

    if (body[0] == '{') {
        cJSON *root = cJSON_Parse(body.c_str());
        if (root) {
            cJSON *json_url = cJSON_GetObjectItem(root, "base_url");
            cJSON *json_token = cJSON_GetObjectItem(root, "token");
            cJSON *json_model = cJSON_GetObjectItem(root, "model");
            cJSON *json_max_tokens = cJSON_GetObjectItem(root, "max_tokens");
            cJSON *json_timeout = cJSON_GetObjectItem(root, "timeout_sec");
            if (json_url && json_url->valuestring)
                snprintf(cfg.base_url, sizeof(cfg.base_url), "%s", json_url->valuestring);
            if (json_token && json_token->valuestring)
                snprintf(cfg.token, sizeof(cfg.token), "%s", json_token->valuestring);
            if (json_model && json_model->valuestring)
                snprintf(cfg.model, sizeof(cfg.model), "%s", json_model->valuestring);
            if (json_max_tokens && json_max_tokens->valueint > 0)
                cfg.max_tokens = json_max_tokens->valueint;
            if (json_timeout && json_timeout->valueint > 0)
                cfg.timeout_sec = json_timeout->valueint;
            cJSON_Delete(root);
        }
    } else {
        parse_form_urlencoded(body, cfg);
    }

    ai_storage_save(&cfg);
    ESP_LOGI(TAG, "Configuracao de IA atualizada via web server (base_url=%s, model=%s)", cfg.base_url, cfg.model);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/?tab=ai&saved=1");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

static esp_err_t api_ai_get_handler(httpd_req_t *req)
{
    ai_cfg_t cfg;
    ai_storage_load(&cfg);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "base_url", cfg.base_url);
    cJSON_AddStringToObject(root, "token", cfg.token);
    cJSON_AddStringToObject(root, "model", cfg.model);
    cJSON_AddNumberToObject(root, "max_tokens", cfg.max_tokens);
    cJSON_AddNumberToObject(root, "timeout_sec", cfg.timeout_sec);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

/* ========================================================================= */
/* Página Principal (Single Page Application com CSS/JS Embutidos)           */
/* ========================================================================= */

static esp_err_t index_handler(httpd_req_t *req)
{
    char query[128] = {0};
    bool saved_alert = false;
    std::string active_tab = "files";
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16] = {0};
        if (httpd_query_key_value(query, "saved", val, sizeof(val)) == ESP_OK && strcmp(val, "1") == 0) {
            saved_alert = true;
        }
        char tab_val[32] = {0};
        if (httpd_query_key_value(query, "tab", tab_val, sizeof(tab_val)) == ESP_OK) {
            active_tab = tab_val;
        }
    }

    ai_cfg_t ai_cfg;
    ai_storage_load(&ai_cfg);

    std::ostringstream html;
    html << "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='utf-8'>"
         << "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
         << "<title>M5Stack Tab5 OS - Painel de Controle</title>"
         << "<style>"
         << "*{box-sizing:border-box;margin:0;padding:0}"
         << "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;"
         << "background:#0f111a;color:#cdd6f4;padding:16px;line-height:1.5}"
         << ".container{max-width:960px;margin:0 auto}"
         << "header{margin-bottom:20px;text-align:center}"
         << "h1{font-size:1.8rem;color:#89b4fa;margin-bottom:6px;display:flex;align-items:center;justify-content:"
            "center;gap:10px}"
         << "p.subtitle{color:#a6adc8;font-size:0.95rem}"
         << ".alert{background:#1e382b;border:1px solid #a6e3a1;color:#a6e3a1;padding:12px 16px;"
         << "border-radius:10px;margin-bottom:16px;font-weight:600;display:flex;align-items:center;gap:8px}"
         << ".status-bar{display:flex;gap:10px;justify-content:center;margin-bottom:18px;flex-wrap:wrap}"
         << ".pill{background:#181825;border:1px solid #313244;padding:6px "
            "12px;border-radius:20px;font-size:0.82rem;font-weight:600;display:flex;align-items:center;gap:6px}"
         << ".pill.ok{border-color:#a6e3a1;color:#a6e3a1}"
         << ".pill.warn{border-color:#f9e2af;color:#f9e2af}"
         << ".tabs{display:flex;gap:6px;margin-bottom:18px;border-bottom:1px solid "
            "#313244;padding-bottom:8px;overflow-x:auto}"
         << ".tab-btn{background:#181825;color:#cdd6f4;border:1px solid #313244;padding:10px 16px;"
         << "border-radius:8px;font-size:0.92rem;font-weight:600;cursor:pointer;transition:all .2s;white-space:nowrap}"
         << ".tab-btn.active{background:#89b4fa;color:#11111b;border-color:#89b4fa}"
         << ".tab-btn:hover:not(.active){background:#313244}"
         << ".card{background:#181825;border:1px solid #313244;border-radius:12px;padding:20px;box-shadow:0 6px 20px "
            "rgba(0,0,0,0.3);margin-bottom:20px}"
         << ".card "
            "h2{font-size:1.2rem;color:#cdd6f4;margin-bottom:14px;display:flex;align-items:center;gap:8px;border-"
            "bottom:1px solid #313244;padding-bottom:8px}"
         << ".grid-2{display:grid;grid-template-columns:1fr 1fr;gap:16px}"
         << "@media(max-width:700px){.grid-2{grid-template-columns:1fr}}"
         << ".form-group{margin-bottom:16px}"
         << "label{display:block;font-size:0.88rem;font-weight:600;color:#bac2de;margin-bottom:6px}"
         << "input[type='text'],input[type='password'],input[type='number'],select{"
         << "width:100%;padding:10px 12px;background:#1e1e2e;border:1px solid #45475a;border-radius:8px;"
         << "color:#cdd6f4;font-size:0.92rem;transition:border-color .2s;outline:none}"
         << "input:focus,select:focus{border-color:#89b4fa}"
         << "input[type='range']{width:100%;cursor:pointer;accent-color:#89b4fa}"
         << ".help{font-size:0.78rem;color:#9399b2;margin-top:4px}"
         << ".btn-row{display:flex;gap:10px;margin-top:18px;flex-wrap:wrap}"
         << ".btn{padding:10px "
            "20px;border-radius:8px;font-size:0.9rem;font-weight:600;cursor:pointer;border:none;transition:opacity "
            ".2s;display:inline-flex;align-items:center;gap:6px}"
         << ".btn:hover{opacity:0.85}"
         << ".btn-sm{padding:6px 12px;font-size:0.8rem;border-radius:6px}"
         << ".btn-primary{background:#89b4fa;color:#11111b}"
         << ".btn-secondary{background:#313244;color:#cdd6f4}"
         << ".btn-success{background:#a6e3a1;color:#11111b}"
         << ".btn-danger{background:#f38ba8;color:#11111b}"
         << ".presets{display:flex;gap:6px;margin-bottom:14px;flex-wrap:wrap}"
         << ".preset-btn{background:#313244;color:#cdd6f4;border:none;padding:5px "
            "10px;border-radius:6px;font-size:0.78rem;cursor:pointer}"
         << ".preset-btn:hover{background:#45475a}"
         << "table{width:100%;border-collapse:collapse;margin-top:10px}"
         << "th,td{padding:10px;text-align:left;border-bottom:1px solid #313244;font-size:0.88rem}"
         << "th{color:#9399b2;font-weight:600}"
         << "tr:hover{background:#1e1e2e}"
         << ".dl-link{color:#89b4fa;text-decoration:none;font-weight:600;padding:5px "
            "10px;background:#313244;border-radius:6px;display:inline-block}"
         << ".dl-link:hover{background:#45475a;color:#cdd6f4}"
         << ".token-wrap{position:relative;display:flex;align-items:center}"
         << ".token-wrap input{padding-right:80px}"
         << ".token-toggle{position:absolute;right:8px;background:none;border:none;color:#89b4fa;font-size:0.8rem;font-"
            "weight:600;cursor:pointer;padding:6px}"
         << ".breadcrumbs{display:flex;gap:6px;align-items:center;background:#1e1e2e;padding:10px "
            "14px;border-radius:8px;margin-bottom:14px;overflow-x:auto;font-size:0.9rem}"
         << ".bc-link{color:#89b4fa;cursor:pointer;text-decoration:none;font-weight:600}"
         << ".bc-link:hover{text-decoration:underline}"
         << ".folder-link{color:#f9e2af;cursor:pointer;text-decoration:none;font-weight:600;display:inline-flex;align-"
            "items:center;gap:6px}"
         << ".folder-link:hover{color:#fab387;text-decoration:underline}"
         << ".toast{position:fixed;bottom:20px;right:20px;background:#89b4fa;color:#11111b;padding:12px "
            "20px;border-radius:8px;font-weight:600;box-shadow:0 4px 16px rgba(0,0,0,0.5);display:none;z-index:999}"
         << "</style></head><body><div class='container'>"
         << "<header><h1>⚡ M5Stack Tab5 OS</h1><p class='subtitle'>Painel de Controle e Gerenciamento do "
            "Sistema</p></header>";

    if (saved_alert) {
        html << "<div class='alert'>✓ Configurações salvas com sucesso!</div>";
    }

    html << "<div id='status-pill-bar' class='status-bar'>"
         << "<div id='pill-wifi' class='pill'>🌐 Wi-Fi: Carregando...</div>"
         << "<div id='pill-bt' class='pill'>📶 Bluetooth: Carregando...</div>"
         << "<div id='pill-tz' class='pill'>🕒 Horário: Carregando...</div>"
         << "</div>";

    /* Abas de Navegação */
    html << "<div class='tabs'>"
         << "<button class='tab-btn" << (active_tab == "files" ? " active" : "")
         << "' onclick='showTab(\"tab-files\", this)'>📁 Arquivos & SD Card</button>"
         << "<button class='tab-btn" << (active_tab == "settings" ? " active" : "")
         << "' onclick='showTab(\"tab-settings\", this)'>⚙️ Configurações do Sistema</button>"
         << "<button class='tab-btn" << (active_tab == "ai" ? " active" : "")
         << "' onclick='showTab(\"tab-ai\", this)'>🤖 Chat IA</button>"
         << "<button class='tab-btn" << (active_tab == "gallery" ? " active" : "")
         << "' onclick='showTab(\"tab-gallery\", this)'>📸 Galeria Rápida</button>"
         << "</div>";

    /* --------------------------------------------------------------------- */
    /* TAB 1: Explorador de Arquivos MicroSD                                 */
    /* --------------------------------------------------------------------- */
    html
        << "<div id='tab-files' class='tab-content'" << (active_tab == "files" ? "" : " style='display:none'") << ">"
        << "<div class='card' id='files-dropzone' ondragover='handleDragOver(event)' "
           "ondragleave='handleDragLeave(event)' ondrop='handleDrop(event)'>"
        << "<h2>📁 Explorador de Arquivos do Cartão MicroSD</h2>"
        << "<div class='breadcrumbs' id='file-breadcrumbs'>Carregando...</div>"
        << "<div class='btn-row' style='margin-top:0;margin-bottom:14px;justify-content:space-between;align-items:"
           "center;'>"
        << "<div style='display:flex;gap:8px;'>"
        << "<button class='btn btn-secondary btn-sm' onclick='navigateParent()'>⬆ Subir Nível</button>"
        << "<button class='btn btn-secondary btn-sm' onclick='loadFileList(currentPath)'>🔄 Atualizar</button>"
        << "</div>"
        << "<div>"
        << "<input type='file' id='file-upload-input' multiple style='display:none;' "
           "onchange='handleFileSelect(event)'>"
        << "<button class='btn btn-secondary btn-sm' onclick='createFolder()'>📁 Nova Pasta</button>"
        << "<button class='btn btn-primary btn-sm' onclick='document.getElementById(\"file-upload-input\").click()'>📤 "
           "Enviar Arquivo(s)</button>"
        << "</div>"
        << "</div>"
        << "<div id='upload-progress-container' style='display:none;margin-bottom:14px;background:#1e1e2e;padding:"
           "12px;border-radius:8px;border:1px solid #45475a;'>"
        << "<div style='display:flex;justify-content:space-between;font-size:0.85rem;margin-bottom:6px;'>"
        << "<span id='upload-status-text'>Enviando arquivo...</span>"
        << "<span id='upload-percent-text'>0%</span>"
        << "</div>"
        << "<div style='background:#313244;height:8px;border-radius:4px;overflow:hidden;'>"
        << "<div id='upload-progress-bar' style='background:#89b4fa;width:0%;height:100%;transition:width "
           ".1s;'></div>"
        << "</div>"
        << "</div>"
        << "<div style='overflow-x:auto;'>"
        << "<table "
           "id='file-table'><thead><tr><th>Nome</th><th>Tipo</th><th>Tamanho</th><th>Baixar</th><th>Apagar</th></tr></"
           "thead><tbody "
           "id='file-tbody'>"
        << "<tr><td colspan='5' style='text-align:center;'>Carregando arquivos do SD...</td></tr>"
        << "</tbody></table>"
        << "</div>"
        << "</div></div>";

    /* --------------------------------------------------------------------- */
    /* TAB 2: Configurações do Sistema                                       */
    /* --------------------------------------------------------------------- */
    html << "<div id='tab-settings' class='tab-content'" << (active_tab == "settings" ? "" : " style='display:none'")
         << ">"
         << "<div class='grid-2'>"

         /* Card Display, Brilho & Tema */
         << "<div class='card'>"
         << "<h2>💡 Tela, Brilho & Tema</h2>"
         << "<div class='form-group'>"
         << "<label for='slider-brightness'>Brilho da Tela: <span id='val-brightness'>80</span>%</label>"
         << "<input type='range' id='slider-brightness' min='10' max='100' value='80' "
            "oninput='updateBrightnessLabel(this.value)' onchange='saveDisplaySettings()'>"
         << "</div>"
         << "<div class='form-group'>"
         << "<label>Tema da Interface:</label>"
         << "<div style='display:flex;gap:10px;'>"
         << "<button class='btn btn-secondary' id='btn-theme-dark' onclick='setTheme(\"dark\")'>🌙 Tema Escuro</button>"
         << "<button class='btn btn-secondary' id='btn-theme-light' onclick='setTheme(\"light\")'>☀️ Tema Claro</button>"
         << "</div>"
         << "</div>"
         << "<div class='form-group'>"
         << "<label>Rotação da Tela:</label>"
         << "<div style='display:flex;align-items:center;gap:10px;margin-bottom:10px;'>"
         << "<input type='checkbox' id='chk-autorot' onchange='saveDisplaySettings()'>"
         << "<label for='chk-autorot' style='margin:0;cursor:pointer;'>Auto-rotação via Sensor IMU</label>"
         << "</div>"
         << "<div style='display:flex;gap:6px;flex-wrap:wrap;'>"
         << "<button class='btn btn-secondary btn-sm' onclick='setRotation(0)'>0° (Padrão)</button>"
         << "<button class='btn btn-secondary btn-sm' onclick='setRotation(1)'>90°</button>"
         << "<button class='btn btn-secondary btn-sm' onclick='setRotation(2)'>180°</button>"
         << "<button class='btn btn-secondary btn-sm' onclick='setRotation(3)'>270°</button>"
         << "</div>"
         << "</div>"
         << "<div class='form-group'>"
         << "<label for='sel-screensaver'>Tempo do Protetor de Tela:</label>"
         << "<select id='sel-screensaver' onchange='saveDisplaySettings()'>"
         << "<option value='0'>Desativado</option>"
         << "<option value='30'>30 Segundos</option>"
         << "<option value='60'>1 Minuto</option>"
         << "<option value='120'>2 Minutos</option>"
         << "<option value='300'>5 Minutos</option>"
         << "<option value='600'>10 Minutos</option>"
         << "</select>"
         << "</div>"
         << "</div>"

         /* Card Fuso Horário & Data/Hora */
         << "<div class='card'>"
         << "<h2>🕒 Fuso Horário & Relógio</h2>"
         << "<div class='form-group'>"
         << "<label for='sel-timezone'>Fuso Horário (Offset UTC):</label>"
         << "<select id='sel-timezone' onchange='saveTimezone()'>"
         << "<option value='-12'>UTC-12:00</option><option value='-11'>UTC-11:00</option><option "
            "value='-10'>UTC-10:00</option>"
         << "<option value='-9'>UTC-09:00</option><option value='-8'>UTC-08:00</option><option "
            "value='-7'>UTC-07:00</option>"
         << "<option value='-6'>UTC-06:00</option><option value='-5'>UTC-05:00</option><option value='-4'>UTC-04:00 "
            "(Manaus)</option>"
         << "<option value='-3'>UTC-03:00 (Brasília / SP / Rio)</option><option value='-2'>UTC-02:00</option><option "
            "value='-1'>UTC-01:00</option>"
         << "<option value='0'>UTC+00:00 (GMT/Londres)</option><option value='1'>UTC+01:00 "
            "(Paris/Berlim)</option><option value='2'>UTC+02:00</option>"
         << "<option value='3'>UTC+03:00</option><option value='4'>UTC+04:00</option><option "
            "value='5'>UTC+05:00</option>"
         << "<option value='6'>UTC+06:00</option><option value='7'>UTC+07:00</option><option value='8'>UTC+08:00 "
            "(Pequim)</option>"
         << "<option value='9'>UTC+09:00 (Tóquio)</option><option value='10'>UTC+10:00</option><option "
            "value='11'>UTC+11:00</option>"
         << "<option value='12'>UTC+12:00</option><option value='13'>UTC+13:00</option><option "
            "value='14'>UTC+14:00</option>"
         << "</select>"
         << "</div>"
         << "<div class='form-group'>"
         << "<label>Horário do Sistema:</label>"
         << "<div id='disp-system-time' style='font-size:1.1rem;font-weight:700;color:#89b4fa;'>--:--:--</div>"
         << "</div>"
         << "</div>"

         /* Card Wi-Fi */
         << "<div class='card' style='grid-column:1 / -1;'>"
         << "<h2>🌐 Conectividade Wi-Fi</h2>"
         << "<div "
            "style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;margin-"
            "bottom:14px;'>"
         << "<div id='wifi-status-info' style='font-size:0.95rem;font-weight:600;'>Status: Carregando...</div>"
         << "<div style='display:flex;gap:8px;'>"
         << "<button class='btn btn-secondary btn-sm' id='btn-wifi-toggle' onclick='toggleWifi()'>Ligar / "
            "Desligar</button>"
         << "<button class='btn btn-primary btn-sm' onclick='scanWifi()'>🔍 Buscar Redes Wi-Fi</button>"
         << "</div>"
         << "</div>"
         << "<div id='wifi-scan-results' style='display:none;margin-bottom:16px;'>"
         << "<table id='wifi-table'><thead><tr><th>Rede "
            "(SSID)</th><th>Sinal</th><th>Segurança</th><th>Ação</th></tr></thead><tbody "
            "id='wifi-tbody'></tbody></table>"
         << "</div>"
         << "<div class='grid-2'>"
         << "<div class='form-group'><label for='wifi-ssid'>SSID (Nome da Rede):</label><input type='text' "
            "id='wifi-ssid' placeholder='Nome da rede'></div>"
         << "<div class='form-group'><label for='wifi-pass'>Senha:</label><input type='password' id='wifi-pass' "
            "placeholder='Senha do Wi-Fi'></div>"
         << "</div>"
         << "<div class='btn-row' style='margin-top:0;'>"
         << "<button class='btn btn-success' onclick='connectWifi()'>🔗 Conectar à Rede</button>"
         << "<button class='btn btn-secondary' onclick='disconnectWifi()'>Desconectar</button>"
         << "<button class='btn btn-danger' onclick='forgetWifi()'>Esquecer Rede</button>"
         << "</div>"
         << "</div>"

         /* Card Bluetooth */
         << "<div class='card' style='grid-column:1 / -1;'>"
         << "<h2>📶 Conectividade Bluetooth</h2>"
         << "<div "
            "style='display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:10px;margin-"
            "bottom:14px;'>"
         << "<div id='bt-status-info' style='font-size:0.95rem;font-weight:600;'>Status: Carregando...</div>"
         << "<div style='display:flex;gap:8px;'>"
         << "<button class='btn btn-secondary btn-sm' id='btn-bt-toggle' onclick='toggleBt()'>Ligar / Desligar</button>"
         << "<button class='btn btn-primary btn-sm' onclick='scanBt()'>🔍 Listar Dispositivos</button>"
         << "</div>"
         << "</div>"
         << "<div id='bt-results' style='margin-bottom:14px;'>"
         << "<table id='bt-table'><thead><tr><th>Nome</th><th>Endereço "
            "MAC</th><th>Tipo</th><th>Ação</th></tr></thead><tbody id='bt-tbody'>"
         << "<tr><td colspan='4' style='text-align:center;'>Nenhum dispositivo salvo ou conectado.</td></tr>"
         << "</tbody></table>"
         << "</div>"
         << "</div>"

         << "</div></div>";

    /* --------------------------------------------------------------------- */
    /* TAB 3: Configuração Chat IA                                           */
    /* --------------------------------------------------------------------- */
    html
        << "<div id='tab-ai' class='tab-content'" << (active_tab == "ai" ? "" : " style='display:none'") << ">"
        << "<div class='card'>"
        << "<h2>🤖 Parâmetros da API OpenAI-compatível</h2>"
        << "<p style='color:#a6adc8;margin-bottom:16px;font-size:0.9rem'>Configure os dados de acesso ao modelo de IA "
           "(OpenCode Go, OpenAI, OpenRouter, etc.).</p>"
        << "<div class='presets'><span style='font-size:0.8rem;color:#9399b2;line-height:26px'>Atalhos rápidos: </span>"
        << "<button type='button' class='preset-btn' onclick='setPreset(\"https://opencode.ai/zen/go/v1\", "
           "\"deepseek-v4-pro\")'>OpenCode Go (DeepSeek-v4)</button>"
        << "<button type='button' class='preset-btn' onclick='setPreset(\"https://api.openai.com/v1\", "
           "\"gpt-4o-mini\")'>OpenAI (GPT-4o mini)</button>"
        << "<button type='button' class='preset-btn' onclick='setPreset(\"https://openrouter.ai/api/v1\", "
           "\"deepseek/deepseek-chat\")'>OpenRouter</button>"
        << "</div>"
        << "<form method='POST' action='/ai/save'>"
        << "<div class='form-group'>"
        << "<label for='base_url'>URL Base da API (Endpoint):</label>"
        << "<input type='text' id='base_url' name='base_url' value='" << ai_cfg.base_url << "' required>"
        << "<div class='help'>Exemplo: https://opencode.ai/zen/go/v1 ou https://api.openai.com/v1</div>"
        << "</div>"
        << "<div class='form-group'>"
        << "<label for='token'>Token de Autenticação (Bearer Token):</label>"
        << "<div class='token-wrap'>"
        << "<input type='password' id='token' name='token' value='" << ai_cfg.token << "' placeholder='sk-...'>"
        << "<button type='button' class='token-toggle' onclick='toggleToken()'>Mostrar</button>"
        << "</div>"
        << "<div class='help'>Chave de API enviada no cabeçalho Authorization: Bearer &lt;token&gt;</div>"
        << "</div>"
        << "<div class='form-group'>"
        << "<label for='model'>Modelo de Linguagem (LLM):</label>"
        << "<input type='text' id='model' name='model' value='" << ai_cfg.model << "' required>"
        << "<div class='help'>Exemplo: deepseek-v4-pro, deepseek-v4-flash, gpt-4o-mini, kimi-k2.6</div>"
        << "</div>"
        << "<div class='grid-2'>"
        << "<div class='form-group'>"
        << "<label for='max_tokens'>Max Tokens:</label>"
        << "<input type='number' id='max_tokens' name='max_tokens' value='" << ai_cfg.max_tokens
        << "' min='64' max='8192'>"
        << "<div class='help'>Limite de tokens na resposta (padrão: 2048)</div>"
        << "</div>"
        << "<div class='form-group'>"
        << "<label for='timeout_sec'>Timeout (segundos):</label>"
        << "<input type='number' id='timeout_sec' name='timeout_sec' value='" << ai_cfg.timeout_sec
        << "' min='5' max='300'>"
        << "<div class='help'>Tempo limite de espera (padrão: 120s)</div>"
        << "</div>"
        << "</div>"
        << "<div class='btn-row'>"
        << "<button type='submit' class='btn btn-primary'>💾 Salvar Configurações</button>"
        << "<button type='button' class='btn btn-secondary' onclick='resetDefaults()'>↺ Restaurar Padrões</button>"
        << "</div>"
        << "</form>"
        << "</div></div>";

    /* --------------------------------------------------------------------- */
    /* TAB 4: Galeria Rápida                                                 */
    /* --------------------------------------------------------------------- */
    html
        << "<div id='tab-gallery' class='tab-content'" << (active_tab == "gallery" ? "" : " style='display:none'")
        << ">"
        << "<div class='card'>"
        << "<h2>📸 Galeria de Fotos & Mídias</h2>"
        << "<p style='color:#a6adc8;margin-bottom:14px;font-size:0.9rem'>Acesso rápido às fotos capturadas pela câmera "
           "em /sdcard/imagens:</p>"
        << "<table><thead><tr><th>Arquivo</th><th>Tamanho</th><th>Ação</th></tr></thead><tbody>";

    wifi_storage_mount();
    DIR *d = opendir("/sdcard/imagens");
    if (d) {
        struct dirent *entry;
        bool found = false;
        while ((entry = readdir(d)) != nullptr) {
            if (entry->d_name[0] == '.')
                continue;
            std::string path = std::string("/sdcard/imagens/") + entry->d_name;
            struct stat st;
            size_t sz = 0;
            if (stat(path.c_str(), &st) == 0)
                sz = st.st_size;
            found = true;
            html << "<tr><td>🖼️ " << entry->d_name << "</td>"
                 << "<td>" << (sz / 1024) << " KB</td>"
                 << "<td><a class='dl-link' href='/download?file=/sdcard/imagens/" << entry->d_name
                 << "' target='_blank'>⬇ Baixar</a></td></tr>";
        }
        closedir(d);
        if (!found) {
            html << "<tr><td colspan='3' style='text-align:center;'>Nenhuma foto encontrada em "
                    "/sdcard/imagens</td></tr>";
        }
    } else {
        html << "<tr><td colspan='3' style='text-align:center;'>Diretório /sdcard/imagens não encontrado.</td></tr>";
    }

    html << "</tbody></table></div></div>";

    /* Toast Notification Element */
    html << "<div id='toast' class='toast'>✓ Sucesso</div>";

    /* --------------------------------------------------------------------- */
    /* JavaScript Frontend                                                   */
    /* --------------------------------------------------------------------- */
    html
        << "<script>"
        << "let currentPath = '/sdcard';"
        << "let systemStatus = {};"
        << "function showToast(msg){const "
           "t=document.getElementById('toast');t.innerText=msg;t.style.display='block';setTimeout(()=>t.style.display='"
           "none',3000);}"
        << "function showTab(tabId, btn){"
        << "document.querySelectorAll('.tab-content').forEach(el=>el.style.display='none');"
        << "document.querySelectorAll('.tab-btn').forEach(el=>el.classList.remove('active'));"
        << "document.getElementById(tabId).style.display='block';"
        << "if(btn) btn.classList.add('active');"
        << "if(tabId==='tab-files') loadFileList(currentPath);"
        << "if(tabId==='tab-settings') loadSystemStatus();"
        << "}"
        << "function toggleToken(){"
        << "var t=document.getElementById('token');"
        << "if(t.type==='password'){t.type='text';event.target.innerText='Ocultar';}"
        << "else{t.type='password';event.target.innerText='Mostrar';}"
        << "}"
        << "function setPreset(url, model){"
        << "document.getElementById('base_url').value=url;"
        << "document.getElementById('model').value=model;"
        << "}"
        << "function resetDefaults(){"
        << "if(confirm('Restaurar parâmetros padrão do OpenCode Go?')){"
        << "setPreset('https://opencode.ai/zen/go/v1', 'deepseek-v4-pro');"
        << "document.getElementById('max_tokens').value=2048;"
        << "document.getElementById('timeout_sec').value=120;"
        << "}}"
        << "function formatSize(bytes){"
        << "if(bytes===0) return '0 B';"
        << "if(bytes<1024) return bytes+' B';"
        << "if(bytes<1048576) return (bytes/1024).toFixed(1)+' KB';"
        << "return (bytes/1048576).toFixed(1)+' MB';"
        << "}"
        << "function loadFileList(path){"
        << "currentPath = path;"
        << "renderBreadcrumbs(path);"
        << "fetch('/api/files?path='+encodeURIComponent(path))"
        << ".then(r=>r.json())"
        << ".then(data=>{"
        << "const tbody = document.getElementById('file-tbody');"
        << "tbody.innerHTML = '';"
        << "if(!data.files || data.files.length === 0){"
        << "tbody.innerHTML = '<tr><td colspan=\"5\" style=\"text-align:center;\">Pasta vazia</td></tr>';"
        << "return;"
        << "}"
        << "data.files.forEach(f=>{"
        << "const tr = document.createElement('tr');"
        << "const full = (path.endsWith('/')?path:path+'/')+f.name;"
        << "const tdName = document.createElement('td');"
        << "const tdType = document.createElement('td');"
        << "const tdSize = document.createElement('td');"
        << "const tdAction = document.createElement('td');"
        << "const tdDel = document.createElement('td');"
        << "const btnDel = document.createElement('button');"
        << "btnDel.innerText = '🗑';"
        << "btnDel.title = 'Apagar';"
        << "btnDel.style.cssText = 'background:#f38ba8;color:#1e1e2e;border:none;border-radius:6px;padding:3px "
           "9px;cursor:pointer;font-size:0.85rem;';"
        << "btnDel.onclick = ()=>deleteFile(full, f.name, f.is_dir);"
        << "tdDel.appendChild(btnDel);"
        << "if(f.is_dir){"
        << "const aFold = document.createElement('a');"
        << "aFold.className = 'folder-link';"
        << "aFold.innerText = '📁 ' + f.name;"
        << "aFold.onclick = ()=>loadFileList(full);"
        << "tdName.appendChild(aFold);"
        << "tdType.innerText = 'Diretório';"
        << "tdSize.innerText = '--';"
        << "const aOpen = document.createElement('a');"
        << "aOpen.className = 'dl-link';"
        << "aOpen.innerText = 'Abrir';"
        << "aOpen.onclick = ()=>loadFileList(full);"
        << "tdAction.appendChild(aOpen);"
        << "}else{"
        << "let icon = '📄 ';"
        << "if(/\\.(jpg|jpeg|png)$/i.test(f.name)) icon = '🖼️ ';"
        << "else if(/\\.(wav|mp3)$/i.test(f.name)) icon = '🎵 ';"
        << "else if(/\\.(txt|log|cfg|json)$/i.test(f.name)) icon = '📝 ';"
        << "tdName.innerText = icon + f.name;"
        << "tdType.innerText = 'Arquivo';"
        << "tdSize.innerText = formatSize(f.size);"
        << "const aDl = document.createElement('a');"
        << "aDl.className = 'dl-link';"
        << "aDl.href = '/download?file=' + encodeURIComponent(full);"
        << "aDl.target = '_blank';"
        << "aDl.innerText = '⬇ Baixar';"
        << "tdAction.appendChild(aDl);"
        << "}"
        << "tr.appendChild(tdName);"
        << "tr.appendChild(tdType);"
        << "tr.appendChild(tdSize);"
        << "tr.appendChild(tdAction);"
        << "tr.appendChild(tdDel);"
        << "tbody.appendChild(tr);"
        << "});"
        << "}).catch(e=>{"
        << "document.getElementById('file-tbody').innerHTML = '<tr><td colspan=\"5\" "
           "style=\"text-align:center;color:#f38ba8;\">Erro ao ler pasta</td></tr>';"
        << "});"
        << "}"
        << "function renderBreadcrumbs(path){"
        << "const bc = document.getElementById('file-breadcrumbs');"
        << "bc.innerHTML = '';"
        << "const parts = path.split('/').filter(p=>p.length>0);"
        << "let acc = '';"
        << "const home = document.createElement('a');"
        << "home.className = 'bc-link';"
        << "home.innerText = '💾 Raiz SD';"
        << "home.onclick = ()=>loadFileList('/sdcard');"
        << "bc.appendChild(home);"
        << "parts.forEach((p, idx)=>{"
        << "if(idx===0) return;"
        << "acc += '/' + p;"
        << "const cur = '/sdcard' + acc;"
        << "const sep = document.createElement('span');"
        << "sep.innerText = ' / ';"
        << "sep.style.color = '#9399b2';"
        << "bc.appendChild(sep);"
        << "const link = document.createElement('a');"
        << "link.className = 'bc-link';"
        << "link.innerText = p;"
        << "link.onclick = ()=>loadFileList(cur);"
        << "bc.appendChild(link);"
        << "});"
        << "}"
        << "function navigateParent(){"
        << "if(currentPath==='/sdcard') return;"
        << "const last = currentPath.lastIndexOf('/');"
        << "if(last>0){loadFileList(currentPath.substring(0, last));}"
        << "else{loadFileList('/sdcard');}"
        << "}"
        << "async function deleteFile(fullpath, name, isDir){"
        << "const tipo = isDir ? 'pasta' : 'arquivo';"
        << "if(!confirm('Apagar '+tipo+' \"'+name+'\"? Esta acao nao pode ser desfeita.')) return;"
        << "try{"
        << "const resp = await fetch('/api/delete?file='+encodeURIComponent(fullpath),{method:'POST'});"
        << "if(resp.ok){showToast('🗑 '+name+' apagado com sucesso');loadFileList(currentPath);}"
        << "else{const t=await resp.text();showToast('Erro: '+t);}"
        << "}catch(e){showToast('Erro de rede: '+e.message);}"
        << "}"
        << "async function createFolder(){"
        << "const name = prompt('Nome da nova pasta:');"
        << "if(!name || !name.trim()) return;"
        << "const n = name.trim();"
        << "if(n==='.'||n==='..'||n.includes('/')||n.includes('\\\\')){showToast('Nome invalido');return;}"
        << "try{"
        << "const resp = await "
           "fetch('/api/mkdir?path='+encodeURIComponent(currentPath)+'&name='+encodeURIComponent(n),{method:'POST'});"
        << "if(resp.ok){showToast('📁 Pasta \"'+n+'\" criada');loadFileList(currentPath);}"
        << "else{const t=await resp.text();showToast('Erro: '+t);}"
        << "}catch(e){showToast('Erro de rede: '+e.message);}"
        << "}"
        << "function handleFileSelect(event){"
        << "const files = event.target.files;"
        << "if(files && files.length>0){uploadFiles(Array.from(files));}"
        << "event.target.value = '';"
        << "}"
        << "function handleDragOver(e){"
        << "e.preventDefault();e.stopPropagation();"
        << "const dz = document.getElementById('files-dropzone');"
        << "if(dz){dz.style.borderColor='#89b4fa';dz.style.backgroundColor='#1e2030';}"
        << "}"
        << "function handleDragLeave(e){"
        << "e.preventDefault();e.stopPropagation();"
        << "const dz = document.getElementById('files-dropzone');"
        << "if(dz){dz.style.borderColor='#313244';dz.style.backgroundColor='#181825';}"
        << "}"
        << "function handleDrop(e){"
        << "e.preventDefault();e.stopPropagation();"
        << "const dz = document.getElementById('files-dropzone');"
        << "if(dz){dz.style.borderColor='#313244';dz.style.backgroundColor='#181825';}"
        << "if(e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files.length>0){"
        << "uploadFiles(Array.from(e.dataTransfer.files));"
        << "}"
        << "}"
        << "async function uploadFiles(files){"
        << "const progCont = document.getElementById('upload-progress-container');"
        << "const statusText = document.getElementById('upload-status-text');"
        << "const percentText = document.getElementById('upload-percent-text');"
        << "const progBar = document.getElementById('upload-progress-bar');"
        << "progCont.style.display = 'block';"
        << "for(let i=0; i<files.length; i++){"
        << "const file = files[i];"
        << "statusText.innerText = 'Enviando '+(i+1)+'/'+files.length+': '+file.name+' ('+formatSize(file.size)+')...';"
        << "progBar.style.width = '0%';"
        << "percentText.innerText = '0%';"
        << "try{"
        << "await new Promise((resolve, reject)=>{"
        << "const xhr = new XMLHttpRequest();"
        << "const url = '/api/upload?path='+encodeURIComponent(currentPath)+'&filename='+encodeURIComponent(file.name);"
        << "xhr.open('POST', url, true);"
        << "xhr.upload.onprogress = function(e){"
        << "if(e.lengthComputable){"
        << "const pct = Math.round((e.loaded / e.total) * 100);"
        << "progBar.style.width = pct + '%';"
        << "percentText.innerText = pct + '%';"
        << "}"
        << "};"
        << "xhr.onload = function(){"
        << "if(xhr.status>=200 && xhr.status<300){resolve();}"
        << "else{reject(new Error('Erro HTTP '+xhr.status+': '+xhr.responseText));}"
        << "};"
        << "xhr.onerror = function(){reject(new Error('Erro de rede durante o upload'));};"
        << "xhr.send(file);"
        << "});"
        << "}catch(err){"
        << "alert('Falha ao enviar '+file.name+': '+err.message);"
        << "break;"
        << "}"
        << "}"
        << "statusText.innerText = '✓ Upload concluído!';"
        << "percentText.innerText = '100%';"
        << "progBar.style.width = '100%';"
        << "showToast('✓ Arquivo(s) enviado(s) com sucesso!');"
        << "setTimeout(()=>{progCont.style.display='none';}, 2000);"
        << "loadFileList(currentPath);"
        << "}"
        << "function loadSystemStatus(){"
        << "fetch('/api/system/status')"
        << ".then(r=>r.json())"
        << ".then(st=>{"
        << "systemStatus = st;"
        << "document.getElementById('slider-brightness').value = st.brightness;"
        << "document.getElementById('val-brightness').innerText = st.brightness;"
        << "document.getElementById('chk-autorot').checked = st.rot_enabled;"
        << "document.getElementById('sel-screensaver').value = st.ss_timeout;"
        << "document.getElementById('sel-timezone').value = st.tz_offset;"
        << "document.getElementById('disp-system-time').innerText = st.time_str;"
        << "const pw = document.getElementById('pill-wifi');"
        << "if(st.wifi.connected){pw.className='pill ok';pw.innerText='🌐 Wi-Fi: '+st.wifi.ssid+' ('+st.wifi.ip+')';}"
        << "else if(st.wifi.enabled){pw.className='pill warn';pw.innerText='🌐 Wi-Fi: Desconectado';}"
        << "else{pw.className='pill';pw.innerText='🌐 Wi-Fi: Desativado';}"
        << "const pb = document.getElementById('pill-bt');"
        << "if(st.bluetooth.any_connected){pb.className='pill ok';pb.innerText='📶 BT: Conectado "
           "('+st.bluetooth.last_name+')';}"
        << "else if(st.bluetooth.enabled){pb.className='pill';pb.innerText='📶 BT: Ativo (Livre)';}"
        << "else{pb.className='pill';pb.innerText='📶 BT: Desativado';}"
        << "const pt = document.getElementById('pill-tz');"
        << "pt.innerText = '🕒 '+st.tz_str+' | '+st.time_str.split(' ')[0];"
        << "const winfo = document.getElementById('wifi-status-info');"
        << "winfo.innerText = 'Status: ' + (st.wifi.connected ? 'Conectado a '+st.wifi.ssid+' ('+st.wifi.ip+')' : "
           "(st.wifi.enabled ? 'Ativo (Desconectado)' : 'Desativado'));"
        << "document.getElementById('btn-wifi-toggle').innerText = st.wifi.enabled ? 'Desligar Wi-Fi' : 'Ligar Wi-Fi';"
        << "const binfo = document.getElementById('bt-status-info');"
        << "binfo.innerText = 'Status: ' + (st.bluetooth.any_connected ? 'Conectado a '+st.bluetooth.last_name : "
           "(st.bluetooth.enabled ? 'Ativo (Nenhum conectado)' : 'Desativado'));"
        << "document.getElementById('btn-bt-toggle').innerText = st.bluetooth.enabled ? 'Desligar Bluetooth' : 'Ligar "
           "Bluetooth';"
        << "}).catch(console.error);"
        << "}"
        << "function updateBrightnessLabel(val){document.getElementById('val-brightness').innerText = val;}"
        << "function saveDisplaySettings(){"
        << "const body = {"
        << "brightness: parseInt(document.getElementById('slider-brightness').value),"
        << "rot_enabled: document.getElementById('chk-autorot').checked,"
        << "ss_timeout: parseInt(document.getElementById('sel-screensaver').value)"
        << "};"
        << "fetch('/api/settings/display', {method:'POST', body:JSON.stringify(body)})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Configurações de tela atualizadas');loadSystemStatus();});"
        << "}"
        << "function setTheme(t){"
        << "fetch('/api/settings/display', {method:'POST', body:JSON.stringify({theme: t})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Tema '+t+' aplicado');loadSystemStatus();});"
        << "}"
        << "function setRotation(r){"
        << "fetch('/api/settings/display', {method:'POST', body:JSON.stringify({rotation: r})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Rotação ajustada para '+(r*90)+'°');loadSystemStatus();});"
        << "}"
        << "function saveTimezone(){"
        << "const off = parseInt(document.getElementById('sel-timezone').value);"
        << "fetch('/api/settings/timezone', {method:'POST', body:JSON.stringify({offset: off})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Fuso horário atualizado');loadSystemStatus();});"
        << "}"
        << "function toggleWifi(){"
        << "const en = !systemStatus.wifi.enabled;"
        << "fetch('/api/settings/wifi', {method:'POST', body:JSON.stringify({action: en ? 'enable' : 'disable'})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast(en ? '✓ Wi-Fi ativado' : '✓ Wi-Fi desativado');setTimeout(loadSystemStatus, 1000);});"
        << "}"
        << "function scanWifi(){"
        << "const tbody = document.getElementById('wifi-tbody');"
        << "document.getElementById('wifi-scan-results').style.display='block';"
        << "tbody.innerHTML = '<tr><td colspan=\"4\" style=\"text-align:center;\">🔍 Escaneando redes... (aguarde "
           "3-5s)</td></tr>';"
        << "fetch('/api/wifi/scan')"
        << ".then(r=>r.json())"
        << ".then(data=>{"
        << "tbody.innerHTML = '';"
        << "if(!data.aps || data.aps.length===0){"
        << "tbody.innerHTML = '<tr><td colspan=\"4\" style=\"text-align:center;\">Nenhuma rede encontrada</td></tr>';"
        << "return;"
        << "}"
        << "data.aps.forEach(ap=>{"
        << "const tr = document.createElement('tr');"
        << "tr.innerHTML = '<td><strong>'+ap.ssid+'</strong></td><td>'+ap.rssi+' "
           "dBm</td><td>'+(ap.auth===0?'Aberta':'WPA/WPA2')+'</td><td><button class=\"btn btn-secondary btn-sm\" "
           "onclick=\"document.getElementById(\\'wifi-ssid\\').value=\\''+ap.ssid+'\\';document.getElementById(\\'wifi-"
           "pass\\').focus();\">Selecionar</button></td>';"
        << "tbody.appendChild(tr);"
        << "});"
        << "}).catch(()=>{tbody.innerHTML='<tr><td colspan=\"4\" style=\"text-align:center;color:#f38ba8;\">Erro ao "
           "buscar redes</td></tr>';});"
        << "}"
        << "function connectWifi(){"
        << "const ssid = document.getElementById('wifi-ssid').value;"
        << "const pass = document.getElementById('wifi-pass').value;"
        << "if(!ssid){alert('Digite o SSID da rede');return;}"
        << "showToast('Conectando a '+ssid+'...');"
        << "fetch('/api/settings/wifi', {method:'POST', body:JSON.stringify({action:'connect', ssid:ssid, "
           "password:pass})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Conexão iniciada');setTimeout(loadSystemStatus, 3000);});"
        << "}"
        << "function disconnectWifi(){"
        << "fetch('/api/settings/wifi', {method:'POST', body:JSON.stringify({action:'disconnect'})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Wi-Fi desconectado');setTimeout(loadSystemStatus, 1000);});"
        << "}"
        << "function forgetWifi(){"
        << "const ssid = document.getElementById('wifi-ssid').value;"
        << "if(!ssid){alert('Digite o SSID da rede para esquecer');return;}"
        << "fetch('/api/settings/wifi', {method:'POST', body:JSON.stringify({action:'forget', ssid:ssid})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Rede esquecida');setTimeout(loadSystemStatus, 1000);});"
        << "}"
        << "function toggleBt(){"
        << "const en = !systemStatus.bluetooth.enabled;"
        << "fetch('/api/settings/bluetooth', {method:'POST', body:JSON.stringify({action: en ? 'enable' : 'disable'})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast(en ? '✓ Bluetooth ativado' : '✓ Bluetooth desativado');setTimeout(loadSystemStatus, "
           "1000);});"
        << "}"
        << "function scanBt(){"
        << "fetch('/api/bluetooth/scan')"
        << ".then(r=>r.json())"
        << ".then(data=>{"
        << "const tbody = document.getElementById('bt-tbody');"
        << "tbody.innerHTML = '';"
        << "if(!data.devices || data.devices.length===0){"
        << "tbody.innerHTML = '<tr><td colspan=\"4\" style=\"text-align:center;\">Nenhum dispositivo salvo ou "
           "conectado.</td></tr>';"
        << "return;"
        << "}"
        << "data.devices.forEach(dev=>{"
        << "const tr = document.createElement('tr');"
        << "const typeStr = dev.type===1?'Teclado':(dev.type===2?'Mouse':(dev.type===3?'Fone/Áudio':'Genérico'));"
        << "tr.innerHTML = '<td>'+dev.name+'</td><td>'+dev.mac+'</td><td>'+typeStr+'</td><td><button class=\"btn "
           "btn-secondary btn-sm\" "
           "onclick=\"connectBt(\\''+dev.mac+'\\',\\''+dev.name+'\\','+dev.type+')\">Conectar</button> <button "
           "class=\"btn btn-danger btn-sm\" onclick=\"forgetBt(\\''+dev.mac+'\\')\">Esquecer</button></td>';"
        << "tbody.appendChild(tr);"
        << "});"
        << "}).catch(console.error);"
        << "}"
        << "function connectBt(mac, name, type){"
        << "showToast('Conectando a '+name+'...');"
        << "fetch('/api/settings/bluetooth', {method:'POST', body:JSON.stringify({action:'connect', mac:mac, "
           "name:name, type:type})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Conexão Bluetooth solicitada');setTimeout(loadSystemStatus, 2000);});"
        << "}"
        << "function forgetBt(mac){"
        << "fetch('/api/settings/bluetooth', {method:'POST', body:JSON.stringify({action:'forget', mac:mac})})"
        << ".then(r=>r.json())"
        << ".then(()=>{showToast('✓ Dispositivo esquecido');setTimeout(scanBt, 500);});"
        << "}"
        << "window.onload = function(){"
        << "loadSystemStatus();"
        << "if('" << active_tab << "'==='files'){loadFileList('/sdcard');}"
        << "setInterval(loadSystemStatus, 5000);"
        << "};"
        << "</script>";

    html << "</div></body></html>";
    std::string response = html.str();

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response.c_str(), response.size());
    return ESP_OK;
}

/* ========================================================================= */
/* Inicialização e Finalização do Servidor HTTP                             */
/* ========================================================================= */

esp_err_t http_file_server_start(void)
{
    if (s_server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.max_uri_handlers = 20;
    config.stack_size = 12288;
    config.recv_wait_timeout = 10;
    config.send_wait_timeout = 10;

    ESP_LOGI(TAG, "iniciando servidor HTTP (painel web, explorador SD e config) na porta %d...", config.server_port);
    ESP_LOGI(TAG, "HEAP_DIAG server_pre: internal=%zu dma=%zu dma_largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    esp_err_t ret = httpd_start(&s_server, &config);
    ESP_LOGI(TAG, "HEAP_DIAG server_post: internal=%zu dma=%zu dma_largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "falha ao iniciar http server (%s)", esp_err_to_name(ret));
        return ret;
    }

    /* Rotas Web & Arquivos */
    httpd_uri_t uri_index = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_index);

    httpd_uri_t uri_download = {
        .uri = "/download", .method = HTTP_GET, .handler = download_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_download);

    httpd_uri_t uri_api_files = {
        .uri = "/api/files", .method = HTTP_GET, .handler = api_files_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_api_files);

    httpd_uri_t uri_upload = {
        .uri = "/api/upload", .method = HTTP_POST, .handler = upload_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_upload);

    httpd_uri_t uri_delete = {
        .uri = "/api/delete", .method = HTTP_POST, .handler = api_delete_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_delete);

    httpd_uri_t uri_mkdir = {
        .uri = "/api/mkdir", .method = HTTP_POST, .handler = api_mkdir_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_mkdir);

    /* Rotas de Status & Configurações do Sistema */
    httpd_uri_t uri_sys_status = {
        .uri = "/api/system/status", .method = HTTP_GET, .handler = api_system_status_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_sys_status);

    httpd_uri_t uri_set_display = {.uri = "/api/settings/display",
                                   .method = HTTP_POST,
                                   .handler = api_settings_display_handler,
                                   .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_set_display);

    httpd_uri_t uri_set_tz = {.uri = "/api/settings/timezone",
                              .method = HTTP_POST,
                              .handler = api_settings_timezone_handler,
                              .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_set_tz);

    /* Rotas Wi-Fi */
    httpd_uri_t uri_wifi_scan = {
        .uri = "/api/wifi/scan", .method = HTTP_GET, .handler = api_wifi_scan_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_wifi_scan);

    httpd_uri_t uri_set_wifi = {
        .uri = "/api/settings/wifi", .method = HTTP_POST, .handler = api_settings_wifi_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_set_wifi);

    /* Rotas Bluetooth */
    httpd_uri_t uri_bt_scan = {
        .uri = "/api/bluetooth/scan", .method = HTTP_GET, .handler = api_bluetooth_scan_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_bt_scan);

    httpd_uri_t uri_set_bt = {.uri = "/api/settings/bluetooth",
                              .method = HTTP_POST,
                              .handler = api_settings_bluetooth_handler,
                              .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_set_bt);

    /* Rotas Chat IA */
    httpd_uri_t uri_ai_save = {.uri = "/ai/save", .method = HTTP_POST, .handler = ai_save_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_ai_save);

    httpd_uri_t uri_api_ai = {.uri = "/api/ai", .method = HTTP_GET, .handler = api_ai_get_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_api_ai);

    ESP_LOGI(TAG, "servidor HTTP ativo em http://<IP>:8080/");
    return ESP_OK;
}

esp_err_t http_file_server_stop(void)
{
    if (s_server == nullptr) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "parando servidor HTTP...");
    esp_err_t ret = httpd_stop(s_server);
    s_server = nullptr;
    ESP_LOGI(TAG, "HEAP_DIAG server_stopped: internal=%zu dma=%zu dma_largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    return ret;
}

bool http_file_server_is_running(void)
{
    return s_server != nullptr;
}

uint16_t http_file_server_get_port(void)
{
    return 8080;
}
