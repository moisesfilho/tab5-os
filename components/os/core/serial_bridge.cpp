#include "serial_bridge.h"

#include "cJSON.h"
#include "app_registry.h"
#include "battery_reader.h"
#include "http_file_server.h"
#include "screenshot.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <memory>
#include <new>
#include <string>
#include <sys/stat.h>
#include <limits.h>
#include <vector>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "bsp/esp-bsp.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_vfs.h"
#include "esp_log.h"
#include "lvgl.h"
#include "misc/lv_area_private.h"
#include "tab5_package_mgr.h"
#include "tab5_host_abi.h"
#include "ui_mouse.h"
#include "ui_shell.h"
#include "ui_screensaver.h"
#include "ui_screen_off.h"
#include "wifi_mgr.h"
#include <unistd.h>
#include <cstdarg>
#endif

namespace {

bool has_string(const cJSON *root, const char *name, const char **value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!item || !cJSON_IsString(item) || !item->valuestring)
        return false;
    if (value)
        *value = item->valuestring;
    return true;
}

bool has_number(const cJSON *root, const char *name, int *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!item || !cJSON_IsNumber(item))
        return false;
    if (value)
        *value = item->valueint;
    return true;
}

bool has_bool(const cJSON *root, const char *name, bool *value)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!item || !cJSON_IsBool(item))
        return false;
    if (value)
        *value = cJSON_IsTrue(item);
    return true;
}

std::string print_json(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    if (!text)
        return {};
    std::string result(text);
    cJSON_free(text);
    return result;
}

std::string envelope(const char *status, const char *action, const char *error, cJSON *data, const char *rid = nullptr)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return {};
    cJSON_AddStringToObject(root, "status", status);
    if (action)
        cJSON_AddStringToObject(root, "action", action);
    if (rid)
        cJSON_AddStringToObject(root, "rid", rid);
    if (error)
        cJSON_AddStringToObject(root, "error", error);
    if (data)
        cJSON_AddItemToObject(root, "data", data);
    std::string result = print_json(root);
    cJSON_Delete(root);
    result.push_back('\n');
    return result;
}

std::string error_frame(const char *action, const char *message, const char *rid = nullptr)
{
    return envelope("error", action ? action : "", message, nullptr, rid);
}

std::string raw_frame(cJSON *root)
{
    std::string result = print_json(root);
    cJSON_Delete(root);
    result.push_back('\n');
    return result;
}

void append_b64(std::string &out, const unsigned char *data, size_t size)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (size_t i = 0; i < size; i += 3) {
        unsigned value = data[i] << 16;
        if (i + 1 < size)
            value |= data[i + 1] << 8;
        if (i + 2 < size)
            value |= data[i + 2];
        out += table[(value >> 18) & 63];
        out += table[(value >> 12) & 63];
        out += (i + 1 < size) ? table[(value >> 6) & 63] : '=';
        out += (i + 2 < size) ? table[value & 63] : '=';
    }
}

#ifdef ESP_PLATFORM
void visit_ui(lv_obj_t *obj, const char *target, const char *symbol, bool *found)
{
    if (!obj || *found)
        return;
    const char *text = lv_obj_check_type(obj, &lv_label_class) ? lv_label_get_text(obj) : nullptr;
    if (text && ((target && strcmp(text, target) == 0) || (symbol && strstr(text, symbol)))) {
        lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
        *found = true;
        return;
    }
    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i)
        visit_ui(lv_obj_get_child(obj, i), target, symbol, found);
}

void dump_ui(lv_obj_t *obj, cJSON *items)
{
    if (!obj)
        return;
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
        return;
    if (lv_obj_check_type(obj, &lv_label_class) || lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_area_t area;
        lv_obj_get_coords(obj, &area);
        const int32_t width = lv_area_get_width(&area);
        const int32_t height = lv_area_get_height(&area);
        if (width <= 0 || height <= 0)
            return;
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "x", area.x1);
        cJSON_AddNumberToObject(item, "y", area.y1);
        cJSON_AddNumberToObject(item, "w", width);
        cJSON_AddNumberToObject(item, "h", height);
        const char *text = lv_obj_check_type(obj, &lv_label_class) ? lv_label_get_text(obj) : lv_textarea_get_text(obj);
        cJSON_AddStringToObject(item, "text", text ? text : "");
        cJSON_AddItemToArray(items, item);
    }
    uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i)
        dump_ui(lv_obj_get_child(obj, i), items);
}

bool click_ui_at(lv_obj_t *obj, int x, int y)
{
    if (!obj)
        return false;
    for (int32_t i = (int32_t)lv_obj_get_child_count(obj) - 1; i >= 0; --i) {
        if (click_ui_at(lv_obj_get_child(obj, (uint32_t)i), x, y))
            return true;
    }
    lv_area_t area;
    lv_point_t point{x, y};
    lv_obj_get_coords(obj, &area);
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE) && _lv_area_is_point_on(&area, &point, 0)) {
        lv_obj_send_event(obj, LV_EVENT_CLICKED, nullptr);
        return true;
    }
    return false;
}
#endif

static uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t size)
{
    while (size--) {
        crc ^= *data++;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return crc;
}

static bool file_crc32(FILE *file, uint32_t *out_crc)
{
    if (!file || !out_crc || fseek(file, 0, SEEK_SET) != 0)
        return false;
    unsigned char buffer[1023];
    uint32_t crc = 0xFFFFFFFFu;
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
        crc = crc32_update(crc, buffer, read);
    if (ferror(file) || fseek(file, 0, SEEK_SET) != 0)
        return false;
    *out_crc = ~crc;
    return true;
}

using frame_writer_t = bool (*)(const char *, size_t, void *);

/* Potentially large bridge data is heap-backed: the FreeRTOS task remains
 * intentionally fixed at 8 KiB. */
constexpr size_t kMaxResponseBytes = 16u * 1024u;
constexpr size_t kMaxInputPathBytes = PATH_MAX;

bool safe_screenshot_path(const char *path, char *canonical, size_t canonical_size)
{
    if (!path || !canonical || canonical_size == 0)
        return false;
    constexpr const char root[] = "/sdcard/screenshots/";
    if (strncmp(path, root, sizeof(root) - 1) != 0 || strstr(path + sizeof(root) - 1, ".."))
        return false;

#ifndef ESP_PLATFORM
    /* The host harness redirects /sdcard at libc boundaries, so realpath()
     * cannot resolve the virtual path itself.  The prefix and traversal
     * checks above are the equivalent sandbox boundary in that harness. */
    if (strlen(path) + 1 > canonical_size)
        return false;
    strcpy(canonical, path);
    return true;
#else
    std::unique_ptr<char[]> root_real(new (std::nothrow) char[kMaxInputPathBytes]);
    std::unique_ptr<char[]> path_real(new (std::nothrow) char[kMaxInputPathBytes]);
    if (!root_real || !path_real)
        return false;
    if (!realpath("/sdcard/screenshots", root_real.get()))
        return false;
    if (realpath(path, path_real.get())) {
        const size_t root_len = strlen(root_real.get());
        if (strncmp(path_real.get(), root_real.get(), root_len) != 0 || path_real[root_len] != '/')
            return false;
        if (strlen(path_real.get()) + 1 > canonical_size)
            return false;
        strcpy(canonical, path_real.get());
        return true;
    }
    return false;
#endif
}

bool write_frame(frame_writer_t writer, void *ctx, const std::string &frame)
{
    return writer && writer(frame.data(), frame.size(), ctx);
}

bool dump_end(frame_writer_t writer, void *ctx, const char *rid, const char *error = nullptr)
{
    cJSON *end = cJSON_CreateObject();
    if (!end)
        return false;
    cJSON_AddStringToObject(end, "action", "screen.dump");
    cJSON_AddStringToObject(end, "event", "end");
    if (rid)
        cJSON_AddStringToObject(end, "rid", rid);
    if (error) {
        cJSON_AddStringToObject(end, "status", "error");
        cJSON_AddStringToObject(end, "error", error);
    }
    return write_frame(writer, ctx, raw_frame(end));
}

bool dump_error(frame_writer_t writer, void *ctx, const char *message, const char *rid)
{
    const std::string error = error_frame("screen.dump", message, rid);
    const bool error_sent = write_frame(writer, ctx, error);
    const bool end_sent = dump_end(writer, ctx, rid, message);
    return error_sent && end_sent;
}

bool stream_dump(const char *path, frame_writer_t writer, void *ctx, const char *rid = nullptr, size_t from_chunk = 0)
{
    /* Wire invariants: cJSON_AddNumberToObject(start, "crc32", crc32);
     * for (size_t index = 0; ...); terminate with dump_end(writer, ctx, rid),
     * including retransmissions. */
    /* O contrato do stream sempre termina com event=end, inclusive no
     * caminho de erro (dump_error -> dump_end). */
    std::unique_ptr<char[]> safe_path(new (std::nothrow) char[kMaxInputPathBytes]);
    if (!safe_path) {
        return dump_error(writer, ctx, "memoria insuficiente para validar o caminho", rid);
    }
    if (!safe_screenshot_path(path, safe_path.get(), kMaxInputPathBytes)) {
        return dump_error(writer, ctx, "path fora do diretorio de screenshots", rid);
    }
    FILE *file = fopen(safe_path.get(), "rb");
    if (!file) {
        return dump_error(writer, ctx, "screenshot nao encontrado", rid);
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return dump_error(writer, ctx, "falha ao medir screenshot", rid);
    }
    const long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return dump_error(writer, ctx, "falha ao ler screenshot", rid);
    }
    /* 1023 é múltiplo de 3: cada bloco base64 é independente e também pode
     * ser concatenado pelo cliente sem padding no meio do fluxo. */
    constexpr size_t chunk_size = 1023;
    const size_t total = static_cast<size_t>(length);
    const size_t chunks = (total + chunk_size - 1) / chunk_size;
    uint32_t crc32 = 0;
    if (!file_crc32(file, &crc32)) {
        fclose(file);
        return dump_error(writer, ctx, "falha ao calcular CRC32", rid);
    }
    cJSON *start = cJSON_CreateObject();
    cJSON_AddStringToObject(start, "status", "ok");
    cJSON_AddStringToObject(start, "action", "screen.dump");
    cJSON_AddStringToObject(start, "event", "start");
    cJSON_AddNumberToObject(start, "size", total);
    cJSON_AddNumberToObject(start, "chunks", chunks);
    cJSON_AddNumberToObject(start, "crc32", crc32);
    if (rid)
        cJSON_AddStringToObject(start, "rid", rid);
    if (!write_frame(writer, ctx, raw_frame(start))) {
        fclose(file);
        return false;
    }

    unsigned char buffer[chunk_size];
    if (from_chunk > chunks)
        from_chunk = chunks;
    if (from_chunk != 0 && fseek(file, static_cast<long>(from_chunk * chunk_size), SEEK_SET) != 0) {
        fclose(file);
        return dump_error(writer, ctx, "indice inicial invalido", rid);
    }
    for (size_t index = 0; index < chunks - from_chunk; ++index) {
        const size_t chunk_index = index + from_chunk;
        const size_t read = fread(buffer, 1, chunk_size, file);
        if (read == 0) {
            fclose(file);
            return dump_error(writer, ctx, "falha durante leitura do screenshot", rid);
        }
        std::string b64;
        append_b64(b64, buffer, read);
        cJSON *frame = cJSON_CreateObject();
        cJSON_AddStringToObject(frame, "action", "screen.dump");
        cJSON_AddNumberToObject(frame, "chunk", chunk_index);
        cJSON_AddStringToObject(frame, "b64", b64.c_str());
        if (rid)
            cJSON_AddStringToObject(frame, "rid", rid);
        if (!write_frame(writer, ctx, raw_frame(frame))) {
            fclose(file);
            return false;
        }
    }
    /* Successful streams terminate through dump_end(writer, ctx, rid). */
    fclose(file);
    return dump_end(writer, ctx, rid);
}

struct string_writer_context {
    std::string value;
};
bool collect_frame(const char *data, size_t size, void *ctx)
{
    static_cast<string_writer_context *>(ctx)->value.append(data, size);
    return true;
}

std::string add_rid_to_frames(const std::string &frames, const char *rid)
{
    if (!rid || !rid[0])
        return frames;
    std::string output;
    size_t begin = 0;
    while (begin < frames.size()) {
        size_t end = frames.find('\n', begin);
        if (end == std::string::npos)
            end = frames.size();
        cJSON *frame = cJSON_ParseWithLength(frames.data() + begin, end - begin);
        if (frame && cJSON_IsObject(frame) && !cJSON_GetObjectItemCaseSensitive(frame, "rid")) {
            cJSON_AddStringToObject(frame, "rid", rid);
            output += print_json(frame);
            output.push_back('\n');
        } else {
            output.append(frames, begin, end - begin);
            if (end < frames.size())
                output.push_back('\n');
        }
        if (frame)
            cJSON_Delete(frame);
        begin = end < frames.size() ? end + 1 : end;
    }
    return output;
}

#ifdef ESP_PLATFORM
static bool s_idle_enabled = false;
static uint32_t s_idle_screensaver_timeout = 0;
static uint32_t s_idle_screen_off_timeout = 0;

static bool set_idle_state(bool enabled)
{
    if (!bsp_display_lock(pdMS_TO_TICKS(500)))
        return false;
    if (enabled && !s_idle_enabled) {
        s_idle_screensaver_timeout = ui_screensaver_get_timeout();
        s_idle_screen_off_timeout = ui_screen_off_get_timeout();
        ui_screensaver_set_timeout_volatile(0);
        ui_screen_off_set_timeout_volatile(0);
    } else if (!enabled && s_idle_enabled) {
        ui_screensaver_set_timeout_volatile(s_idle_screensaver_timeout);
        ui_screen_off_set_timeout_volatile(s_idle_screen_off_timeout);
    }
    s_idle_enabled = enabled;
    bsp_display_unlock();
    return true;
}
#else
static bool s_idle_enabled = false;
static bool set_idle_state(bool enabled)
{
    s_idle_enabled = enabled;
    return true;
}
#endif

} // namespace

extern "C" int serial_bridge_dispatch(const char *json_line, char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;
    out[0] = '\0';
    cJSON *root = json_line ? cJSON_Parse(json_line) : nullptr;
    if (!root || !cJSON_IsObject(root)) {
        if (root)
            cJSON_Delete(root);
        std::string result = error_frame("", "linha JSON invalida");
        if (result.size() > out_sz)
            return -1;
        memcpy(out, result.data(), result.size());
        return (int)result.size();
    }
    const char *cmd = nullptr;
    const char *rid = nullptr;
    (void)has_string(root, "rid", &rid);
    if (!has_string(root, "cmd", &cmd) || !cmd[0]) {
        cJSON_Delete(root);
        std::string result = error_frame("", "campo cmd ausente");
        if (result.size() > out_sz)
            return -1;
        memcpy(out, result.data(), result.size());
        return (int)result.size();
    }
    std::string result;
    if (!strcmp(cmd, "app.list")) {
        cJSON *data = cJSON_CreateObject();
        cJSON *apps = cJSON_AddArrayToObject(data, "apps");
        for (const auto &app : app_registry_get_all()) {
            cJSON *a = cJSON_CreateObject();
            cJSON_AddStringToObject(a, "id", app.id);
            cJSON_AddStringToObject(a, "name", app.name);
            cJSON_AddItemToArray(apps, a);
        }
        result = envelope("ok", cmd, nullptr, data);
    } else if (!strcmp(cmd, "app.open")) {
        const char *id = nullptr;
        if (!has_string(root, "id", &id))
            result = error_frame(cmd, "campo id ausente");
        else {
#ifdef ESP_PLATFORM
            const char *file = nullptr;
            has_string(root, "file", &file);
            /* O bridge não pode executar o entrypoint WASM no task do
             * console: o runtime pode aguardar o worker do módulo. Usa o
             * dispatcher oficial, como os callbacks LVGL, e libera o
             * transporte para entregar o envelope e os logs. */
            const tab5_err_t launch_err = tab5_package_mgr_launch(id, file);
            if (launch_err != TAB5_OK) {
                char message[96];
                snprintf(message, sizeof(message), "falha ao abrir aplicacao (err=%d)", (int)launch_err);
                result = error_frame(cmd, message);
            } else
#endif
            {
                cJSON *d = cJSON_CreateObject();
                cJSON_AddStringToObject(d, "id", id);
                result = envelope("ok", cmd, nullptr, d);
            }
        }
    } else if (!strcmp(cmd, "app.close")) {
#ifdef ESP_PLATFORM
        if (tab5_package_mgr_close_active() != TAB5_OK)
            result = error_frame(cmd, "nao foi possivel fechar a aplicacao");
        else
#endif
            result = result.empty() ? envelope("ok", cmd, nullptr, nullptr) : result;
    } else if (!strcmp(
                   cmd,
                   "app.active")) { /* AddBoolToObject(active); app_id, app_name and is_wasm come from the snapshot */
        cJSON *d = cJSON_CreateObject();
#ifdef ESP_PLATFORM
        /* Do not dereference tab5_host_get_active_app(): the snapshot is the
         * lifetime-safe replacement for the old active != nullptr check. */
        tab5_active_app_snapshot_t snapshot = {};
        const bool active = tab5_host_get_active_app_snapshot(&snapshot);
        cJSON_AddBoolToObject(d, "active", active);
        if (active) {
            cJSON_AddStringToObject(d, "app_id", snapshot.app_id);
            cJSON_AddStringToObject(d, "app_name", snapshot.app_name);
            cJSON_AddStringToObject(d, "id", snapshot.app_id);
            cJSON_AddStringToObject(d, "name", snapshot.app_name);
            cJSON_AddBoolToObject(d, "is_wasm", snapshot.is_wasm);
        }
#else
        cJSON_AddBoolToObject(d, "active", false);
#endif
        result = envelope("ok", cmd, nullptr, d);
    } else if (!strcmp(cmd, "ui.click")) {
        int x, y;
        if (!has_number(root, "x", &x) || !has_number(root, "y", &y))
            result = error_frame(cmd, "x e y numericos sao obrigatorios");
        else {
#ifdef ESP_PLATFORM
            if (!bsp_display_lock(pdMS_TO_TICKS(500)))
                result = error_frame(cmd, "lock do display indisponivel");
            else {
                bool found = click_ui_at(lv_screen_active(), x, y) || click_ui_at(lv_layer_top(), x, y);
                bsp_display_unlock();
                if (!found)
                    ui_mouse_inject_click();
                cJSON *d = cJSON_CreateObject();
                cJSON_AddNumberToObject(d, "x", x);
                cJSON_AddNumberToObject(d, "y", y);
                result = envelope("ok", cmd, nullptr, d);
            }
#else
            cJSON *d = cJSON_CreateObject();
            cJSON_AddNumberToObject(d, "x", x);
            cJSON_AddNumberToObject(d, "y", y);
            result = envelope("ok", cmd, nullptr, d);
#endif
        }
    } else if (!strcmp(cmd, "ui.tap")) {
        const char *target = nullptr;
        const char *symbol = nullptr;
        if (!has_string(root, "target", &target) && !has_string(root, "symbol", &symbol))
            result = error_frame(cmd, "target ou symbol obrigatorio");
        else {
            bool found = false;
#ifdef ESP_PLATFORM
            if (!bsp_display_lock(pdMS_TO_TICKS(500)))
                result = error_frame(cmd, "lock do display indisponivel");
            else {
                visit_ui(lv_screen_active(), target, symbol, &found);
                visit_ui(lv_layer_top(), target, symbol, &found);
                bsp_display_unlock();
            }
#else
            found = true;
#endif
            if (result.empty()) {
                cJSON *d = cJSON_CreateObject();
                cJSON_AddBoolToObject(d, "found", found);
                result = envelope("ok", cmd, nullptr, d);
            }
        }
    } else if (!strcmp(cmd, "ui.type")) {
        const char *text = nullptr;
        if (!has_string(root, "text", &text))
            result = error_frame(cmd, "campo text ausente");
        else {
#ifdef ESP_PLATFORM
            /* Keyboard subsystem owns focus and insertion; dispatch one character sequence through LVGL. */
            if (!bsp_display_lock(pdMS_TO_TICKS(500)))
                result = error_frame(cmd, "lock do display indisponivel");
            else {
                lv_obj_t *focused = lv_group_get_focused(lv_group_get_default());
                if (focused && lv_obj_check_type(focused, &lv_textarea_class))
                    lv_textarea_add_text(focused, text);
                bsp_display_unlock();
            }
#endif
            if (result.empty()) {
                cJSON *d = cJSON_CreateObject();
                cJSON_AddNumberToObject(d, "chars", strlen(text));
                result = envelope("ok", cmd, nullptr, d);
            }
        }
    } else if (!strcmp(cmd, "ui.dump")) {
        cJSON *d = cJSON_CreateObject();
        cJSON *items = cJSON_AddArrayToObject(d, "items");
#ifdef ESP_PLATFORM
        if (!bsp_display_lock(pdMS_TO_TICKS(500)))
            result = error_frame(cmd, "lock do display indisponivel");
        else {
            dump_ui(lv_screen_active(), items);
            dump_ui(lv_layer_top(), items);
            bsp_display_unlock();
        }
#else
        (void)items;
#endif
        if (result.empty())
            result = envelope("ok", cmd, nullptr, d);
        else
            cJSON_Delete(d);
    } else if (!strcmp(cmd, "server.start") || !strcmp(cmd, "server.stop") || !strcmp(cmd, "server.status")) {
        esp_err_t err = ESP_OK;
        if (!strcmp(cmd, "server.start"))
            err = http_file_server_start();
        else if (!strcmp(cmd, "server.stop"))
            err = http_file_server_stop();
        if (err != ESP_OK)
            result = error_frame(cmd, "operacao do servidor falhou");
        else {
            cJSON *d = cJSON_CreateObject();
            bool running = http_file_server_is_running();
            cJSON_AddBoolToObject(d, "running", running);
            cJSON_AddNumberToObject(d, "port", http_file_server_get_port());
#ifdef ESP_PLATFORM
            wifi_status_t status = {};
            wifi_mgr_get_status(&status);
            cJSON_AddStringToObject(d, "ip", status.ip);
            char url[96];
            snprintf(url, sizeof(url), "http://%s:%u/", status.ip, http_file_server_get_port());
#else
            cJSON_AddStringToObject(d, "ip", "0.0.0.0");
            char url[96];
            snprintf(url, sizeof(url), "http://0.0.0.0:%u/", http_file_server_get_port());
#endif
            cJSON_AddStringToObject(d, "url", url);
            result = envelope("ok", cmd, nullptr, d);
        }
    } else if (!strcmp(cmd, "screen.shot")) {
        const screenshot_result_t shot = screenshot_take();
        if (shot == SCREENSHOT_RESULT_BUSY)
            result = error_frame(cmd, "screenshot ja esta em andamento");
        else if (shot != SCREENSHOT_RESULT_OK)
            result = error_frame(cmd, "falha ao iniciar screenshot");
        else if (screenshot_wait_for_completion(5000) != SCREENSHOT_RESULT_OK)
            result = error_frame(cmd, "screenshot falhou ou nao terminou no prazo");
        else {
            cJSON *d = cJSON_CreateObject();
            cJSON_AddStringToObject(d, "path", screenshot_get_last_path());
            result = envelope("ok", cmd, nullptr, d);
        }
    } else if (!strcmp(cmd, "screen.dump") || !strcmp(cmd, "screen.dump.retry") || !strcmp(cmd, "screen.dump.resume")) {
        const char *path = nullptr;
        if (!has_string(root, "path", &path))
            path = screenshot_get_last_path();
        /* screen.dump.retry/resume use stream_dump(path, writer, ctx, rid, from_index). */
        int from_index = 0;
        if (!strcmp(cmd, "screen.dump.retry")) {
            if (!has_number(root, "from", &from_index))
                (void)has_number(root, "start_index", &from_index);
            if (from_index < 0)
                from_index = 0;
        } else if (!strcmp(cmd, "screen.dump.resume")) {
            int last_index = -1;
            if (!has_number(root, "last_chunk", &last_index))
                (void)has_number(root, "chunk", &last_index);
            from_index = last_index < 0 ? 0 : last_index + 1;
        }
        string_writer_context collector;
        stream_dump(path, collect_frame, &collector, rid, static_cast<size_t>(from_index));
        result = std::move(collector.value);
    } else if (!strcmp(cmd, "sys.idle")) {
        bool requested = false;
        if (has_bool(root, "enable", &requested) && !set_idle_state(requested)) {
            result = error_frame(cmd, "lock do display indisponivel", rid);
        } else {
            cJSON *d = cJSON_CreateObject();
            cJSON_AddBoolToObject(d, "enabled", s_idle_enabled);
            result = envelope("ok", cmd, nullptr, d, rid);
        }
    } else if (!strcmp(cmd, "sys.info")) {
        cJSON *d = cJSON_CreateObject();
#ifdef ESP_PLATFORM
        cJSON_AddNumberToObject(d, "heap_free_internal", esp_get_free_heap_size());
        cJSON_AddNumberToObject(d, "heap_free_psram", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        battery_status_t b = {};
        const bool battery_ok = battery_reader_get_status(&b);
        cJSON_AddBoolToObject(d, "battery_available", battery_ok && b.available);
        if (battery_ok && b.available) {
            cJSON_AddNumberToObject(d, "battery_mv", b.voltage_mv);
            cJSON_AddNumberToObject(d, "battery_pct", b.percent);
        } else {
            cJSON_AddNullToObject(d, "battery_mv");
            cJSON_AddNullToObject(d, "battery_pct");
        }
        cJSON_AddStringToObject(d, "battery_state", battery_ok ? (b.available ? "available" : "unavailable") : "error");
        wifi_status_t w = {};
        const bool wifi_ok = wifi_mgr_get_status(&w) == ESP_OK;
        cJSON_AddBoolToObject(d, "wifi_connected", wifi_ok && w.connected);
        if (wifi_ok) {
            cJSON_AddStringToObject(d, "wifi_ssid", w.ssid);
            cJSON_AddStringToObject(d, "wifi_ip", w.ip);
        } else {
            cJSON_AddNullToObject(d, "wifi_ssid");
            cJSON_AddNullToObject(d, "wifi_ip");
        }
        cJSON_AddStringToObject(d, "wifi_state", wifi_ok ? (w.connected ? "connected" : "disconnected") : "error");
#else
        cJSON_AddNumberToObject(d, "heap_free_internal", 0);
        cJSON_AddNumberToObject(d, "heap_free_psram", 0);
        cJSON_AddBoolToObject(d, "battery_available", false);
        cJSON_AddNullToObject(d, "battery_mv");
        cJSON_AddNullToObject(d, "battery_pct");
        cJSON_AddStringToObject(d, "battery_state", "unavailable");
        cJSON_AddBoolToObject(d, "wifi_connected", false);
        cJSON_AddNullToObject(d, "wifi_ssid");
        cJSON_AddNullToObject(d, "wifi_ip");
        cJSON_AddStringToObject(d, "wifi_state", "unavailable");
#endif
        result = envelope("ok", cmd, nullptr, d);
    } else
        result = error_frame(cmd, "comando desconhecido");
    result = add_rid_to_frames(result, rid);
    cJSON_Delete(root);
    if (result.size() > out_sz)
        return -1;
    memcpy(out, result.data(), result.size());
    return (int)result.size();
}

#ifdef ESP_PLATFORM
static SemaphoreHandle_t s_frame_lock = nullptr;
static vprintf_like_t s_previous_log_writer = nullptr;

static int locked_log_writer(const char *format, va_list args)
{
    if (!s_frame_lock || xSemaphoreTake(s_frame_lock, portMAX_DELAY) != pdTRUE) {
        return s_previous_log_writer ? s_previous_log_writer(format, args) : 0;
    }
    const int result = s_previous_log_writer ? s_previous_log_writer(format, args) : 0;
    xSemaphoreGive(s_frame_lock);
    return result;
}

static bool lock_frame_writer(void)
{
    return s_frame_lock && xSemaphoreTake(s_frame_lock, portMAX_DELAY) == pdTRUE;
}

static void unlock_frame_writer(void)
{
    if (s_frame_lock)
        xSemaphoreGive(s_frame_lock);
}

static bool uart_frame_writer(const char *data, size_t size, void *ctx)
{
    const uart_port_t port = *static_cast<const uart_port_t *>(ctx);
    if (!lock_frame_writer())
        return false;
    std::string wire;
    wire.reserve(size + 8);
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\n')
            wire.push_back('\r');
        wire.push_back(data[i]);
    }
    const int written = uart_write_bytes(port, wire.data(), wire.size());
    unlock_frame_writer();
    return written == static_cast<int>(wire.size());
}

static bool usb_frame_writer(const char *data, size_t size, void *)
{
    if (!lock_frame_writer())
        return false;
    std::string wire;
    wire.reserve(size + 8);
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == '\n')
            wire.push_back('\r');
        wire.push_back(data[i]);
    }
    const int written = usb_serial_jtag_write_bytes(wire.data(), wire.size(), pdMS_TO_TICKS(100));
    unlock_frame_writer();
    return written == static_cast<int>(wire.size());
}

static bool dispatch_stream_line(const char *line, frame_writer_t writer, void *ctx)
{
    cJSON *root = cJSON_Parse(line);
    const char *cmd = nullptr;
    if (root && cJSON_IsObject(root) && has_string(root, "cmd", &cmd) &&
        (!strcmp(cmd, "screen.dump") || !strcmp(cmd, "screen.dump.retry") || !strcmp(cmd, "screen.dump.resume"))) {
        const char *path = nullptr;
        const char *rid = nullptr;
        if (!has_string(root, "path", &path))
            path = screenshot_get_last_path();
        (void)has_string(root, "rid", &rid);
        int from_index = 0;
        if (!strcmp(cmd, "screen.dump.retry"))
            (void)has_number(root, "from", &from_index);
        else if (!strcmp(cmd, "screen.dump.resume")) {
            int last_chunk = -1;
            (void)has_number(root, "last_chunk", &last_chunk);
            from_index = last_chunk + 1;
        }
        if (from_index < 0)
            from_index = 0;
        const bool ok = stream_dump(path, writer, ctx, rid, static_cast<size_t>(from_index));
        cJSON_Delete(root);
        return ok;
    }
    if (root)
        cJSON_Delete(root);
    std::unique_ptr<char[]> response(new (std::nothrow) char[kMaxResponseBytes]);
    if (!response) {
        ESP_LOGE("serial_bridge", "Memoria insuficiente para resposta do comando");
        static constexpr char frame[] = "{\"status\":\"error\",\"action\":\"\","
                                        "\"error\":\"memoria insuficiente para resposta\"}\n";
        return writer && writer(frame, sizeof(frame) - 1, ctx);
    }
    const int size = serial_bridge_dispatch(line, response.get(), kMaxResponseBytes);
    if (size <= 0)
        return false;
    return writer(response.get(), static_cast<size_t>(size), ctx);
}
#endif

#ifdef ESP_PLATFORM
static void serial_bridge_task(void *)
{
#if CONFIG_TAB5_SERIAL_BRIDGE_TRANSPORT_UART
    uart_port_t port = (uart_port_t)CONFIG_TAB5_SERIAL_BRIDGE_UART_NUM;
    if (CONFIG_TAB5_SERIAL_BRIDGE_UART_RX_PIN < 0 || CONFIG_TAB5_SERIAL_BRIDGE_UART_TX_PIN < 0 ||
        CONFIG_TAB5_SERIAL_BRIDGE_UART_RX_PIN == CONFIG_TAB5_SERIAL_BRIDGE_UART_TX_PIN) {
        ESP_LOGE("serial_bridge", "Pinos UART invalidos: RX=%d TX=%d", CONFIG_TAB5_SERIAL_BRIDGE_UART_RX_PIN,
                 CONFIG_TAB5_SERIAL_BRIDGE_UART_TX_PIN);
        vTaskDelete(nullptr);
        return;
    }
    uart_config_t config = {};
    config.baud_rate = CONFIG_TAB5_SERIAL_BRIDGE_UART_BAUD;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (uart_param_config(port, &config) != ESP_OK ||
        uart_set_pin(port, CONFIG_TAB5_SERIAL_BRIDGE_UART_TX_PIN, CONFIG_TAB5_SERIAL_BRIDGE_UART_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(port, 4096, 0, 0, nullptr, 0) != ESP_OK) {
        ESP_LOGE("serial_bridge", "Falha ao inicializar UART");
        vTaskDelete(nullptr);
        return;
    }
    std::string line;
    char input;
    bool overflow = false;
    for (;;) {
        if (uart_read_bytes(port, (uint8_t *)&input, 1, pdMS_TO_TICKS(100)) != 1)
            continue;
        if (input == '\n') {
            if (overflow) {
                const std::string error = error_frame("", "linha de entrada excede o limite");
                if (!uart_frame_writer(error.data(), error.size(), &port)) {
                    ESP_LOGE("serial_bridge", "Falha ao transmitir erro de linha excedente");
                }
            } else if (!dispatch_stream_line(line.c_str(), uart_frame_writer, &port)) {
                ESP_LOGE("serial_bridge", "Falha ao processar/transmitir comando");
            }
            line.clear();
            overflow = false;
        } else if (input != '\r') {
            if (line.size() < 8191)
                line += input;
            else
                overflow = true;
        }
    }
#else
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
        config.tx_buffer_size = 8192;
        config.rx_buffer_size = 8192;
        if (usb_serial_jtag_driver_install(&config) != ESP_OK) {
            ESP_LOGE("serial_bridge", "Falha ao inicializar USB Serial-JTAG");
            vTaskDelete(nullptr);
            return;
        }
    }
    /* O VFS e o console compartilham locks e o mesmo driver; a conversao aqui
     * garante CRLF para logs e frames sem inventar uma API de transporte. */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI("serial_bridge", "USB Serial-JTAG bridge pronto");

    std::string line;
    bool overflow = false;
    char input;
    for (;;) {
        const int received = usb_serial_jtag_read_bytes(&input, 1, pdMS_TO_TICKS(100));
        if (received != 1) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (input == '\n') {
            if (overflow) {
                const std::string error = error_frame("", "linha de entrada excede o limite");
                if (!usb_frame_writer(error.data(), error.size(), nullptr)) {
                    ESP_LOGE("serial_bridge", "Falha ao transmitir erro de linha excedente");
                }
            } else if (!dispatch_stream_line(line.c_str(), usb_frame_writer, nullptr)) {
                ESP_LOGE("serial_bridge", "Falha ao processar/transmitir comando");
            }
            line.clear();
            overflow = false;
        } else if (input != '\r') {
            if (line.size() < 8191)
                line += input;
            else
                overflow = true;
        }
    }
#endif
}
#endif

extern "C" void serial_bridge_start(void)
{
#ifdef ESP_PLATFORM
#if CONFIG_TAB5_SERIAL_BRIDGE_ENABLED
    if (!s_frame_lock) {
        s_frame_lock = xSemaphoreCreateMutex();
        if (!s_frame_lock) {
            ESP_LOGE("serial_bridge", "Falha ao criar mutex do console");
            return;
        }
        /* O writer de logs e o writer NDJSON usam o mesmo mutex no console
         * USB. Isso evita que uma linha ESP_LOG seja inserida no meio de um
         * frame, sem substituir a API oficial do IDF. */
        s_previous_log_writer = esp_log_set_vprintf(locked_log_writer);
    }
    if (xTaskCreatePinnedToCore(serial_bridge_task, "serial_bridge", 8192, nullptr, 3, nullptr, tskNO_AFFINITY) !=
        pdPASS) {
        ESP_LOGE("serial_bridge", "Falha ao criar task da ponte serial");
    }
#endif
#endif
}
