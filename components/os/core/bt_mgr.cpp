#include "bt_mgr.h"
#include "bt_storage.h"
#include "hid_report_map.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
extern "C" {
#include "esp_hosted_misc.h"
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "store/config/ble_store_config.h"
#include "ui_keyboard.h"
#include "ui_mouse.h"
#include "nvs.h"
#include "nvs_flash.h"

extern "C" void ble_store_config_init(void);

static const char *TAG = "tab5_bt";

namespace {

struct active_conn_t {
    char mac[18];
    char name[64];
    bt_dev_type_t type;
    uint8_t addr_type;
    uint16_t conn_handle;
    bool connected;
    char id_mac[18]; /* endereco identidade distribuido no bonding (estavel) */
    uint8_t id_addr_type;
};

#define MAX_ACTIVE_CONNS 4

/* Tentativa de conexao em andamento (procedimento GAP ocupado) */
struct pending_conn_t {
    bool active;
    bool user_initiated;
    bool cancel_expected; /* cancelamento disparado por nos (scan/forget): nao conta backoff */
    char mac[18];
};

#define AUTOCONN_BACKOFF_MS 15000
#define AUTOCONN_FAIL_SLOTS 4
struct autoconn_fail_t {
    char mac[18];
    TickType_t tick;
};

SemaphoreHandle_t s_bt_mutex = nullptr;
active_conn_t s_active_conns[MAX_ACTIVE_CONNS] = {};
int s_active_count = 0;
pending_conn_t s_pending = {};
autoconn_fail_t s_autoconn_fails[AUTOCONN_FAIL_SLOTS] = {};
bt_conn_cb_t s_conn_cb = nullptr;
void *s_conn_ctx = nullptr;

bool s_bt_enabled = true;
bool s_nimble_inited = false;
bool s_nimble_synced = false;
bool s_scanning = false;
TimerHandle_t s_scan_watchdog = nullptr;
bt_scan_cb_t s_scan_cb = nullptr;
void *s_scan_ctx = nullptr;

static void load_nvs_bt_enabled(void)
{
    nvs_handle_t h;
    if (nvs_open("radios", NVS_READONLY, &h) == ESP_OK) {
        uint8_t val = 1;
        if (nvs_get_u8(h, "bt_en", &val) == ESP_OK) {
            s_bt_enabled = (val != 0);
        }
        nvs_close(h);
    }
}

static void save_nvs_bt_enabled(bool en)
{
    nvs_handle_t h;
    if (nvs_open("radios", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u8(h, "bt_en", en ? 1 : 0);
        nvs_commit(h);
        nvs_close(h);
    }
}

bt_device_info_t s_discovered[BT_SCAN_MAX_DEVICES] = {};
int s_discovered_count = 0;

bt_device_info_t s_callback_buffer[BT_SCAN_MAX_DEVICES] = {};

#define MAX_CCCD_QUEUE 16
uint16_t s_cccd_queue[MAX_CCCD_QUEUE] = {};
int s_cccd_queue_count = 0;
int s_cccd_queue_idx = 0;

uint16_t s_hid_start_handle = 0;
uint16_t s_hid_end_handle = 0;

void format_mac_addr(const uint8_t *val, char *buf, size_t buf_len)
{
    snprintf(buf, buf_len, "%02X:%02X:%02X:%02X:%02X:%02X", val[5], val[4], val[3], val[2], val[1], val[0]);
}

bool parse_mac_addr(const char *str, ble_addr_t *out_addr)
{
    if (str == nullptr || out_addr == nullptr) {
        return false;
    }
    unsigned int b[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", &b[5], &b[4], &b[3], &b[2], &b[1], &b[0]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        out_addr->val[i] = (uint8_t)b[i];
    }
    return true;
}

/* Notifica resultado de conexao SEM segurar o mutex (callback pode marshallar
 * para a task de UI). Copia o MAC pois o chamador costuma passar ponteiro
 * interno protegido pelo mutex. */
void notify_conn_event(const char *mac, bt_conn_event_t event, int reason)
{
    if (s_conn_cb == nullptr || mac == nullptr || mac[0] == '\0') {
        return;
    }
    char mac_copy[18];
    snprintf(mac_copy, sizeof(mac_copy), "%s", mac);
    s_conn_cb(mac_copy, event, reason, s_conn_ctx);
}

bool any_connected_locked(void)
{
    for (int i = 0; i < s_active_count; i++) {
        if (s_active_conns[i].connected) {
            return true;
        }
    }
    return false;
}

void remove_slot_locked(int idx)
{
    if (idx < 0 || idx >= s_active_count) {
        return;
    }
    for (int j = idx; j < s_active_count - 1; j++) {
        s_active_conns[j] = s_active_conns[j + 1];
    }
    memset(&s_active_conns[s_active_count - 1], 0, sizeof(active_conn_t));
    s_active_count--;
}

active_conn_t *find_slot_by_handle_locked(uint16_t conn_handle)
{
    for (int i = 0; i < s_active_count; i++) {
        if (s_active_conns[i].conn_handle == conn_handle) {
            return &s_active_conns[i];
        }
    }
    return nullptr;
}

/* Backoff por MAC: evita que o auto-conect martele um dispositivo que acabou
 * de recusar a conexao (ou cujo endereco rotacionou), monopolizando o GAP. */
bool autoconn_backoff_active_locked(const char *mac)
{
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < AUTOCONN_FAIL_SLOTS; i++) {
        if (s_autoconn_fails[i].mac[0] != '\0' && strcasecmp(s_autoconn_fails[i].mac, mac) == 0) {
            return (now - s_autoconn_fails[i].tick) < pdMS_TO_TICKS(AUTOCONN_BACKOFF_MS);
        }
    }
    return false;
}

void autoconn_mark_failed_locked(const char *mac)
{
    if (mac == nullptr || mac[0] == '\0') {
        return;
    }
    int oldest = 0;
    for (int i = 0; i < AUTOCONN_FAIL_SLOTS; i++) {
        if (s_autoconn_fails[i].mac[0] == '\0') {
            oldest = i;
            break;
        }
        /* Aritmetica modular de ticks: negativo significa tick[i] < tick[oldest] */
        if ((int32_t)(s_autoconn_fails[i].tick - s_autoconn_fails[oldest].tick) < 0) {
            oldest = i;
        }
    }
    snprintf(s_autoconn_fails[oldest].mac, sizeof(s_autoconn_fails[oldest].mac), "%s", mac);
    s_autoconn_fails[oldest].tick = xTaskGetTickCount();
}

void autoconn_clear_locked(const char *mac)
{
    for (int i = 0; i < AUTOCONN_FAIL_SLOTS; i++) {
        if (s_autoconn_fails[i].mac[0] != '\0' && strcasecmp(s_autoconn_fails[i].mac, mac) == 0) {
            memset(&s_autoconn_fails[i], 0, sizeof(autoconn_fail_t));
        }
    }
}

/* Persiste o dispositivo como pareado APENAS quando a inicializacao HID foi
 * concluida (GAP conectado + descoberta GATT + CCCDs gravados). Usa o endereco
 * identidade distribuido no bonding quando disponivel (estavel entre reboots,
 * diferente do RPA rotativo anunciado por mouses como o Logitech Lift). */
void persist_paired_device_locked(uint16_t conn_handle, char *out_mac, size_t out_mac_len)
{
    active_conn_t *slot = find_slot_by_handle_locked(conn_handle);
    if (slot == nullptr || slot->mac[0] == '\0') {
        return;
    }

    const char *key_mac = (slot->id_mac[0] != '\0') ? slot->id_mac : slot->mac;
    uint8_t key_type = (slot->id_mac[0] != '\0') ? slot->id_addr_type : slot->addr_type;

    bt_saved_device_t dev = {};
    snprintf(dev.mac, sizeof(dev.mac), "%s", key_mac);
    snprintf(dev.name, sizeof(dev.name), "%s", slot->name);
    dev.type = slot->type;
    dev.addr_type = key_type;
    dev.paired = true;
    dev.auto_connect = true;
    esp_err_t err = bt_storage_add_or_update(&dev);

    /* O endereço rotativo antigo não deve continuar ocupando espaço como
     * entrada "fantasma" no bt.cfg. */
    if (err == ESP_OK && strcasecmp(key_mac, slot->mac) != 0) {
        bt_storage_remove(slot->mac);
    }

    ESP_LOGI(TAG, "Dispositivo %s [%s] persistido como pareado (HID pronto)", slot->name, key_mac);
    if (out_mac != nullptr && out_mac_len > 0) {
        snprintf(out_mac, out_mac_len, "%s", key_mac);
    }
}

bt_dev_type_t classify_device(const struct ble_hs_adv_fields *fields)
{
    if (fields == nullptr) {
        return BT_DEV_TYPE_GENERIC;
    }

    if (fields->appearance_is_present) {
        uint16_t app = fields->appearance;
        if (app == 0x03C1 || app == 0x03C0) {
            return BT_DEV_TYPE_KEYBOARD;
        }
        if (app == 0x03C2) {
            return BT_DEV_TYPE_MOUSE;
        }
        if (app == 0x0842 || app == 0x0844 || app == 0x0840) {
            return BT_DEV_TYPE_HEADPHONE;
        }
    }

    for (int i = 0; i < fields->num_uuids16; i++) {
        if (ble_uuid_u16(&fields->uuids16[i].u) == 0x1812) {
            return BT_DEV_TYPE_KEYBOARD;
        }
    }

    if (fields->name != nullptr && fields->name_len > 0) {
        char name_buf[64] = {};
        size_t len = fields->name_len < 63 ? fields->name_len : 63;
        memcpy(name_buf, fields->name, len);
        name_buf[len] = '\0';

        if (strcasestr(name_buf, "keyb") || strcasestr(name_buf, "teclado") || strcasestr(name_buf, "k380") ||
            strcasestr(name_buf, "k480") || strcasestr(name_buf, "k400") || strcasestr(name_buf, "k810") ||
            strcasestr(name_buf, "rii") || strcasestr(name_buf, "air") || strcasestr(name_buf, "touch") ||
            strcasestr(name_buf, "wireless")) {
            return BT_DEV_TYPE_KEYBOARD;
        }
        if (strcasestr(name_buf, "mouse") || strcasestr(name_buf, "trackpad")) {
            return BT_DEV_TYPE_MOUSE;
        }
        if (strcasestr(name_buf, "head") || strcasestr(name_buf, "fone") || strcasestr(name_buf, "audio") ||
            strcasestr(name_buf, "buds") || strcasestr(name_buf, "airpod") || strcasestr(name_buf, "ear")) {
            return BT_DEV_TYPE_HEADPHONE;
        }
    }

    return BT_DEV_TYPE_KEYBOARD;
}

void finish_scan_locked(void)
{
    if (s_scan_watchdog != nullptr) {
        xTimerStop(s_scan_watchdog, 0);
    }
    s_scanning = false;
    ESP_LOGI(TAG, "Scan NimBLE concluido (%d dispositivos)", s_discovered_count);

    if (s_scan_cb != nullptr) {
        bt_scan_cb_t cb = s_scan_cb;
        void *ctx = s_scan_ctx;
        s_scan_cb = nullptr;
        s_scan_ctx = nullptr;

        int copy_count = s_discovered_count;
        memcpy(s_callback_buffer, s_discovered, sizeof(bt_device_info_t) * copy_count);

        if (s_bt_mutex != nullptr) {
            xSemaphoreGive(s_bt_mutex);
        }

        cb(s_callback_buffer, copy_count, ctx);
        return;
    }

    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }
}

void scan_watchdog_cb(TimerHandle_t xTimer)
{
    (void)xTimer;
    ESP_LOGW(TAG, "Scan watchdog disparado (finalizando busca)");
    ble_gap_disc_cancel();

    if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        finish_scan_locked();
    }
}

uint16_t s_curr_conn_handle = BLE_HS_CONN_HANDLE_NONE;

uint16_t s_hid_cp_handle = 0;
uint16_t s_proto_mode_handle = 0;

/* Report Map (0x2A4B) do dispositivo conectado: permite rotear notificacoes
 * pelo report ID real em vez de IDs fixos no codigo. */
#define HID_MAP_RAW_MAX 512
uint8_t s_report_map_raw[HID_MAP_RAW_MAX] = {};
int s_report_map_len = 0;
hid_report_entry_t s_report_entries[HID_REPORT_MAP_MAX] = {};
int s_report_count = 0;

void reset_report_map(void)
{
    memset(s_report_map_raw, 0, sizeof(s_report_map_raw));
    s_report_map_len = 0;
    memset(s_report_entries, 0, sizeof(s_report_entries));
    s_report_count = 0;
}

int on_read_report_map(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error != nullptr && error->status != 0) {
        ESP_LOGW(TAG, "Falha ao ler Report Map (0x2A4B): status=%d (usando heuristica de relatorios)", error->status);
        return 0;
    }
    if (attr == nullptr) {
        return 0;
    }

    uint16_t len = os_mbuf_len(attr->om);
    if (len > HID_MAP_RAW_MAX) {
        len = HID_MAP_RAW_MAX;
    }
    os_mbuf_copydata(attr->om, 0, len, s_report_map_raw);
    s_report_map_len = len;

    s_report_count =
        hid_report_map_parse(s_report_map_raw, (size_t)s_report_map_len, s_report_entries, HID_REPORT_MAP_MAX);

    ESP_LOGI(TAG, "Report Map lido (%d bytes): %d report ID(s) classificado(s)", s_report_map_len, s_report_count);
    for (int i = 0; i < s_report_count; i++) {
        static const char *kind_names[] = {"UNKNOWN", "MOUSE", "KEYBOARD", "CONSUMER"};
        ESP_LOGI(TAG, "  Report ID=0x%02X -> %s", s_report_entries[i].report_id, kind_names[s_report_entries[i].kind]);
    }
    return 0;
}

/* Pipeline Sequencial de GATT, Descritores 0x2902 e CCCD */
int on_write_cccd_seq(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg);

void write_next_cccd(void)
{
    if (s_cccd_queue_idx >= s_cccd_queue_count || s_curr_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "Todas as notificacoes (%d CCCDs) foram ativadas com sucesso! Teclado e Mouse ativos.",
                 s_cccd_queue_count);

        char ready_mac[18] = {};
        if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            persist_paired_device_locked(s_curr_conn_handle, ready_mac, sizeof(ready_mac));
            xSemaphoreGive(s_bt_mutex);
        }
        notify_conn_event(ready_mac, BT_CONN_READY, 0);

        ui_keyboard_notify_hardware_change();
        return;
    }

    uint16_t handle = s_cccd_queue[s_cccd_queue_idx++];
    uint8_t val[2] = {0x01, 0x00};
    ESP_LOGI(TAG, "Gravando CCCD (0x2902) no handle=%d (%d/%d)...", handle, s_cccd_queue_idx, s_cccd_queue_count);
    int rc = ble_gattc_write_flat(s_curr_conn_handle, handle, val, sizeof(val), on_write_cccd_seq, nullptr);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gattc_write_flat handle=%d retornou %d, continuando proximo...", handle, rc);
        write_next_cccd();
    }
}

int on_write_cccd_seq(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    if (error != nullptr && error->status != 0) {
        ESP_LOGW(TAG, "Aviso ao gravar CCCD: status=%d", error->status);
    }
    write_next_cccd();
    return 0;
}

int on_write_proto_mode(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)error;
    (void)attr;
    (void)arg;
    ESP_LOGI(TAG, "Protocol Mode gravado (Report Mode 0x01). Iniciando gravacao dos CCCDs...");
    write_next_cccd();
    return 0;
}

int on_write_hid_cp(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)error;
    (void)attr;
    (void)arg;
    ESP_LOGI(TAG, "HID Control Point gravado (Exit Suspend 0x00).");
    if (s_proto_mode_handle != 0) {
        uint8_t proto = 0x01; /* Report Protocol Mode */
        ble_gattc_write_flat(conn_handle, s_proto_mode_handle, &proto, 1, on_write_proto_mode, nullptr);
    } else {
        write_next_cccd();
    }
    return 0;
}

void start_hid_initialization(uint16_t conn_handle)
{
    if (s_hid_cp_handle != 0) {
        uint8_t cp = 0x00; /* Exit Suspend */
        ESP_LOGI(TAG, "Gravando HID Control Point (Exit Suspend) no handle=%d...", s_hid_cp_handle);
        ble_gattc_write_flat(conn_handle, s_hid_cp_handle, &cp, 1, on_write_hid_cp, nullptr);
    } else if (s_proto_mode_handle != 0) {
        uint8_t proto = 0x01; /* Report Protocol Mode */
        ESP_LOGI(TAG, "Gravando Protocol Mode (Report Mode) no handle=%d...", s_proto_mode_handle);
        ble_gattc_write_flat(conn_handle, s_proto_mode_handle, &proto, 1, on_write_proto_mode, nullptr);
    } else {
        write_next_cccd();
    }
}

int on_disc_dsc_seq(uint16_t conn_handle, const struct ble_gatt_error *error, uint16_t chr_val_handle,
                    const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)chr_val_handle;
    (void)arg;
    if (error == nullptr || error->status == 0) {
        if (dsc != nullptr) {
            uint16_t uuid16 = ble_uuid_u16(&dsc->uuid.u);
            if (uuid16 == 0x2902) { /* Client Characteristic Configuration Descriptor */
                if (s_cccd_queue_count < MAX_CCCD_QUEUE) {
                    s_cccd_queue[s_cccd_queue_count++] = dsc->handle;
                    ESP_LOGI(TAG, "Descoberto CCCD real 0x2902 no handle=%d (total=%d)", dsc->handle,
                             s_cccd_queue_count);
                }
            }
            return 0;
        }
    }

    ESP_LOGI(TAG, "Descoberta de descritores concluida (%d CCCDs 0x2902 encontrados). Inicializando HOGP...",
             s_cccd_queue_count);
    start_hid_initialization(conn_handle);
    return 0;
}

int on_disc_chr_seq(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
    (void)arg;
    if (error == nullptr || error->status == 0) {
        if (chr != nullptr) {
            uint16_t uuid16 = ble_uuid_u16(&chr->uuid.u);
            ESP_LOGI(TAG, "GATT Caracteristica: UUID=0x%04X val_handle=%d props=0x%02X", uuid16, chr->val_handle,
                     chr->properties);

            if (uuid16 == 0x2A4C) { /* HID Control Point */
                s_hid_cp_handle = chr->val_handle;
            } else if (uuid16 == 0x2A4E) { /* Protocol Mode */
                s_proto_mode_handle = chr->val_handle;
            } else if (uuid16 == 0x2A4B) { /* Report Map: base para rotear os relatorios */
                int rc = ble_gattc_read(conn_handle, chr->val_handle, on_read_report_map, nullptr);
                if (rc != 0) {
                    ESP_LOGW(TAG, "ble_gattc_read Report Map falhou: rc=%d", rc);
                }
            }
            return 0;
        }
    }

    ESP_LOGI(TAG, "Descoberta de caracteristicas concluida. Descobrindo descritores 0x2902...");
    uint16_t start = (s_hid_start_handle != 0) ? s_hid_start_handle : 0x0001;
    uint16_t end = (s_hid_end_handle != 0) ? s_hid_end_handle : 0xFFFF;
    ble_gattc_disc_all_dscs(conn_handle, start, end, on_disc_dsc_seq, nullptr);
    return 0;
}

int on_disc_svc_seq(uint16_t conn_handle, const struct ble_gatt_error *error, const struct ble_gatt_svc *service,
                    void *arg)
{
    (void)arg;
    if (error == nullptr || error->status == 0) {
        if (service != nullptr) {
            uint16_t uuid16 = ble_uuid_u16(&service->uuid.u);
            ESP_LOGI(TAG, "GATT Servico: UUID=0x%04X handles=[%d..%d]", uuid16, service->start_handle,
                     service->end_handle);
            if (uuid16 == 0x1812 || uuid16 == 0x180F) {
                if (s_hid_start_handle == 0 || service->start_handle < s_hid_start_handle) {
                    s_hid_start_handle = service->start_handle;
                }
                if (service->end_handle > s_hid_end_handle) {
                    s_hid_end_handle = service->end_handle;
                }
            }
            return 0;
        }
    }

    ESP_LOGI(TAG, "Descoberta de servicos concluida. Descobrindo caracteristicas em [%d..%d]...", s_hid_start_handle,
             s_hid_end_handle);
    s_cccd_queue_count = 0;
    s_cccd_queue_idx = 0;
    s_hid_cp_handle = 0;
    s_proto_mode_handle = 0;

    uint16_t start = (s_hid_start_handle != 0) ? s_hid_start_handle : 0x0001;
    uint16_t end = (s_hid_end_handle != 0) ? s_hid_end_handle : 0xFFFF;
    ble_gattc_disc_all_chrs(conn_handle, start, end, on_disc_chr_seq, nullptr);
    return 0;
}

/**
 * @brief Callback principal de eventos GAP/GATT BLE (NimBLE).
 *
 * Processa o ciclo de vida completo de conexões BLE HID: descoberta por scan,
 * conexão, autenticação (bonding), descoberta de serviços/características,
 * subscription de notificações HID e tratamento de dados de teclado, mouse e
 * touchpad recebidos por notificação GATT.
 *
 * @param event  Estrutura do evento GAP recebido pela stack NimBLE.
 * @param arg    Argumento de usuário (não utilizado).
 * @return       0 em todos os casos (convenção NimBLE).
 */
static int handle_gap_disc(struct ble_gap_event *event)
{
    if (event->disc.data == nullptr || event->disc.length_data == 0) {
        return 0;
    }

    struct ble_hs_adv_fields fields = {};
    int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
    if (rc != 0) {
        return 0;
    }

    char mac_str[18];
    format_mac_addr(event->disc.addr.val, mac_str, sizeof(mac_str));

    char name_str[64] = "";
    if (fields.name != nullptr && fields.name_len > 0) {
        size_t len = fields.name_len < 63 ? fields.name_len : 63;
        memcpy(name_str, fields.name, len);
        name_str[len] = '\0';
    }

    bt_dev_type_t dev_type = classify_device(&fields);

    if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(30)) == pdTRUE) {
        /* 1. Verificação prioritária de Auto-Conexão para dispositivos pareados */
        bt_saved_device_t saved = {};
        bool is_paired = false;
        bool matched_by_name = false;
        if (bt_storage_find(mac_str, &saved) && saved.paired) {
            is_paired = true;
        } else if (name_str[0] != '\0') {
            bt_saved_list_t list = {};
            if (bt_storage_load_all(&list) == ESP_OK) {
                for (int k = 0; k < list.count; k++) {
                    if (strcasecmp(list.items[k].name, name_str) == 0 && list.items[k].paired) {
                        saved = list.items[k];
                        is_paired = true;
                        matched_by_name = true;
                        break;
                    }
                }
            }
        }

        /* Dispositivos com endereco privado rotativo (RPA), como mouses
         * Logitech, mudam de MAC a cada anuncio. O pareamento por nome permite
         * atualizar o registro para o endereco atual em vez de tentar conectar
         * a um endereco ja invalido. */
        if (is_paired && matched_by_name && strcasecmp(saved.mac, mac_str) != 0) {
            ESP_LOGI(TAG, "MAC do dispositivo \"%s\" rotacionou (%s -> %s): atualizando registro", name_str, saved.mac,
                     mac_str);
            bt_saved_list_t list = {};
            if (bt_storage_load_all(&list) == ESP_OK) {
                bool updated = false;
                for (int k = 0; k < list.count; k++) {
                    if (strcasecmp(list.items[k].mac, saved.mac) == 0) {
                        snprintf(list.items[k].mac, sizeof(list.items[k].mac), "%s", mac_str);
                        list.items[k].addr_type = event->disc.addr.type;
                        updated = true;
                        break;
                    }
                }
                if (updated && bt_storage_save_all(&list) == ESP_OK) {
                    snprintf(saved.mac, sizeof(saved.mac), "%s", mac_str);
                    saved.addr_type = event->disc.addr.type;
                }
            }
        }

        bool any_connected = any_connected_locked();
        bool pending_busy = s_pending.active;

        if (is_paired && saved.auto_connect && !any_connected && !pending_busy && !ble_gap_conn_active() &&
            !autoconn_backoff_active_locked(saved.mac)) {
            ESP_LOGI(TAG, "Dispositivo pareado detectado no ar! Auto-conectando imediatamente: %s [%s] (addr_type=%d)",
                     saved.name[0] ? saved.name : name_str, mac_str, (int)event->disc.addr.type);
            char target_mac[18];
            char target_name[64];
            bt_dev_type_t target_type = (saved.type != BT_DEV_TYPE_GENERIC) ? saved.type : dev_type;
            snprintf(target_mac, sizeof(target_mac), "%s", mac_str);
            snprintf(target_name, sizeof(target_name), "%s", saved.name[0] ? saved.name : name_str);
            xSemaphoreGive(s_bt_mutex);
            bt_mgr_connect(target_mac, target_name, target_type);
            return 0;
        }

        /* 2. Atualização da tabela de descobertos */
        int idx = -1;
        for (int i = 0; i < s_discovered_count; i++) {
            if (strcasecmp(s_discovered[i].mac, mac_str) == 0) {
                idx = i;
                break;
            }
        }

        if (idx >= 0) {
            if (name_str[0] != '\0' && s_discovered[idx].name[0] == '\0') {
                snprintf(s_discovered[idx].name, sizeof(s_discovered[idx].name), "%s", name_str);
            }
            if (dev_type != BT_DEV_TYPE_GENERIC) {
                s_discovered[idx].type = dev_type;
            }
            s_discovered[idx].addr_type = event->disc.addr.type;
            s_discovered[idx].rssi = event->disc.rssi;
            s_discovered[idx].paired = is_paired;
        } else if (s_discovered_count < BT_SCAN_MAX_DEVICES) {
            bt_device_info_t *dev = &s_discovered[s_discovered_count++];
            snprintf(dev->mac, sizeof(dev->mac), "%s", mac_str);
            snprintf(dev->name, sizeof(dev->name), "%s", (saved.name[0] != '\0') ? saved.name : name_str);
            dev->type = (saved.type != BT_DEV_TYPE_GENERIC) ? saved.type : dev_type;
            dev->addr_type = event->disc.addr.type;
            dev->rssi = event->disc.rssi;
            dev->connected = false;
            dev->paired = is_paired;

            for (int j = 0; j < s_active_count; j++) {
                if (s_active_conns[j].connected && strcasecmp(dev->mac, s_active_conns[j].mac) == 0) {
                    dev->connected = true;
                    break;
                }
            }

            ESP_LOGI(TAG, "BLE Adv: \"%s\" [%s] (addr_type=%d) RSSI=%d", dev->name, dev->mac, (int)dev->addr_type,
                     dev->rssi);
        }
        xSemaphoreGive(s_bt_mutex);
    }
    return 0;
}

static int handle_gap_connect(struct ble_gap_event *event)
{
    if (event->connect.status == 0) {
        ESP_LOGI(TAG, "GAP Conectado com sucesso! conn_handle=%d", event->connect.conn_handle);
        s_curr_conn_handle = event->connect.conn_handle;
        reset_report_map();

        struct ble_gap_conn_desc desc = {};
        char mac_str[18] = {0};
        if (ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
            format_mac_addr(desc.peer_ota_addr.val, mac_str, sizeof(mac_str));
            ESP_LOGI(TAG, "GAP Peer MAC: %s (type=%d)", mac_str, (int)desc.peer_ota_addr.type);
        }

        if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            bool slot_found = false;
            for (int i = 0; i < s_active_count; i++) {
                if (mac_str[0] != '\0' && strcasecmp(s_active_conns[i].mac, mac_str) == 0) {
                    s_active_conns[i].conn_handle = event->connect.conn_handle;
                    s_active_conns[i].connected = true;
                    slot_found = true;
                    break;
                } else if (mac_str[0] == '\0') {
                    s_active_conns[i].conn_handle = event->connect.conn_handle;
                    s_active_conns[i].connected = true;
                    slot_found = true;
                }
            }

            if (!slot_found) {
                if (s_active_count < MAX_ACTIVE_CONNS) {
                    int idx = s_active_count++;
                    s_active_conns[idx].conn_handle = event->connect.conn_handle;
                    s_active_conns[idx].connected = true;
                    if (mac_str[0] != '\0') {
                        snprintf(s_active_conns[idx].mac, sizeof(s_active_conns[idx].mac), "%s", mac_str);
                        bt_saved_device_t saved = {};
                        if (bt_storage_find(mac_str, &saved)) {
                            snprintf(s_active_conns[idx].name, sizeof(s_active_conns[idx].name), "%s", saved.name);
                            s_active_conns[idx].type = saved.type;
                        } else {
                            snprintf(s_active_conns[idx].name, sizeof(s_active_conns[idx].name),
                                     "Dispositivo Bluetooth");
                            s_active_conns[idx].type = BT_DEV_TYPE_GENERIC;
                        }
                        s_active_conns[idx].addr_type = desc.peer_ota_addr.type;
                    } else {
                        snprintf(s_active_conns[idx].name, sizeof(s_active_conns[idx].name), "Dispositivo Bluetooth");
                        s_active_conns[idx].type = BT_DEV_TYPE_GENERIC;
                    }
                } else if (s_active_count > 0) {
                    s_active_conns[0].conn_handle = event->connect.conn_handle;
                    s_active_conns[0].connected = true;
                }
            }

            /* Endereco identidade do peer (estavel; o MAC anunciado pode ser
             * RPA rotativo). Atualizado de novo no ENC_CHANGE apos bonding. */
            active_conn_t *conn_slot = find_slot_by_handle_locked(event->connect.conn_handle);
            if (conn_slot != nullptr && ble_gap_conn_find(event->connect.conn_handle, &desc) == 0) {
                format_mac_addr(desc.peer_id_addr.val, conn_slot->id_mac, sizeof(conn_slot->id_mac));
                conn_slot->id_addr_type = desc.peer_id_addr.type;
            }

            if (s_pending.active) {
                autoconn_clear_locked(s_pending.mac);
            }

            for (int i = 0; i < s_discovered_count; i++) {
                for (int j = 0; j < s_active_count; j++) {
                    if (s_active_conns[j].connected && strcasecmp(s_discovered[i].mac, s_active_conns[j].mac) == 0) {
                        s_discovered[i].connected = true;
                        s_discovered[i].paired = true;
                    }
                }
            }
            xSemaphoreGive(s_bt_mutex);
        }

        s_pending.active = false;
        notify_conn_event(mac_str, BT_CONN_CONNECTED, 0);

        ble_gap_security_initiate(event->connect.conn_handle);

        s_hid_start_handle = 0;
        s_hid_end_handle = 0;
        ble_gattc_disc_all_svcs(event->connect.conn_handle, on_disc_svc_seq, nullptr);
    } else {
        ESP_LOGW(TAG, "GAP Falha na conexao fisica status=%d", event->connect.status);

        char failed_mac[18] = {};
        bool cancel_expected = false;
        if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            /* Remove o slot pendente da tentativa que falhou (antes ele ficava
             * "zumbi" para sempre, bloqueando rescan e reconexao automatica). */
            for (int i = 0; i < s_active_count; i++) {
                if (!s_active_conns[i].connected) {
                    snprintf(failed_mac, sizeof(failed_mac), "%s",
                             s_pending.mac[0] != '\0' ? s_pending.mac : s_active_conns[i].mac);
                    remove_slot_locked(i);
                    break;
                }
            }
            if (s_pending.active) {
                cancel_expected = s_pending.cancel_expected;
                if (!cancel_expected && failed_mac[0] != '\0') {
                    autoconn_mark_failed_locked(s_pending.mac);
                }
                s_pending.active = false;
            }
            xSemaphoreGive(s_bt_mutex);
        }

        if (!cancel_expected) {
            notify_conn_event(failed_mac, BT_CONN_FAILED, event->connect.status);
        }

        /* Rescan passivo so quando ninguem cancelou de proposito (ex.: o
         * usuario ja iniciou um scan novo) e nao ha conexao viva. */
        if (!cancel_expected && !any_connected_locked()) {
            bt_mgr_scan(nullptr, nullptr);
        }
    }
    ui_keyboard_notify_hardware_change();
    return 0;
}

static int handle_gap_disconnect(struct ble_gap_event *event)
{
    ESP_LOGI(TAG, "GAP Desconectado reason=%d", event->disconnect.reason);
    if (s_curr_conn_handle == event->disconnect.conn.conn_handle) {
        s_curr_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }

    char gone_mac[18] = {};
    if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < s_active_count; i++) {
            if (s_active_conns[i].conn_handle == event->disconnect.conn.conn_handle) {
                snprintf(gone_mac, sizeof(gone_mac), "%s", s_active_conns[i].mac);
                remove_slot_locked(i);
                break;
            }
        }
        for (int i = 0; i < s_discovered_count; i++) {
            s_discovered[i].connected = false;
        }
        xSemaphoreGive(s_bt_mutex);
    }

    if (gone_mac[0] != '\0') {
        notify_conn_event(gone_mac, BT_CONN_DISCONNECTED, event->disconnect.reason);
    }

    ui_keyboard_notify_hardware_change();
    ui_mouse_set_connected(false);

    {
        bt_saved_list_t list = {};
        if (bt_storage_load_all(&list) == ESP_OK && list.count > 0) {
            for (int i = 0; i < list.count; i++) {
                if (list.items[i].paired && list.items[i].auto_connect) {
                    ESP_LOGI(TAG, "Reconectando diretamente a %s [%s] apos desconexao...", list.items[i].name,
                             list.items[i].mac);
                    bt_mgr_connect(list.items[i].mac, list.items[i].name, list.items[i].type);
                    return 0;
                }
            }
            ESP_LOGI(TAG, "Reiniciando escuta passiva apos desconexao...");
            bt_mgr_scan(nullptr, nullptr);
        }
    }
    return 0;
}

void handle_mouse_report(const uint8_t *rpt, uint16_t len)
{
    if (rpt == nullptr || len < 3) {
        return;
    }
    uint8_t buttons = rpt[0];
    int8_t dx = (int8_t)rpt[1];
    int8_t dy = (int8_t)rpt[2];
    int8_t wheel = (len > 3) ? (int8_t)rpt[3] : 0;
    ESP_LOGI(TAG, "HID Mouse Padrão: btn=0x%02X dx=%d dy=%d wheel=%d", buttons, dx, dy, wheel);
    ui_mouse_inject_motion(dx, dy, buttons, wheel);
}

/* Mouse composto descrito pelo Report Map real (ex.: Lift da Logitech:
 * 16 botoes, X/Y de 12 bits, wheel e AC Pan de 8 bits). */
void handle_mouse_report_composite(const hid_report_entry_t *entry, const uint8_t *rpt, uint16_t len)
{
    if (rpt == nullptr || entry == nullptr) {
        return;
    }

    /* Geometria ausente ou equivalente ao formato boot de 1 byte por eixo:
     * usa o decoder simples. */
    if ((entry->x_bits == 0 && entry->y_bits == 0) ||
        (entry->x_bits % 8 == 0 && entry->y_bits % 8 == 0 && entry->button_count <= 8 && entry->wheel_bits % 8 == 0)) {
        handle_mouse_report(rpt, len);
        return;
    }

    hid_mouse_sample_t sample = {};
    if (!hid_report_map_decode_mouse(entry, rpt, (size_t)len, &sample)) {
        ESP_LOGW(TAG, "Falha ao decodificar relatorio de mouse composto (len=%d)", (int)len);
        return;
    }
    ESP_LOGI(TAG, "HID Mouse Composto: btn=0x%02X dx=%ld dy=%ld wheel=%ld pan=%ld", sample.buttons, (long)sample.dx,
             (long)sample.dy, (long)sample.wheel, (long)sample.pan);
    ui_mouse_inject_motion((int8_t)sample.dx, (int8_t)sample.dy, sample.buttons, (int8_t)sample.wheel);
}

/* Decide se uma notificacao pode ser roteada pelo Report Map real do
 * dispositivo (lido na descoberta GATT) em vez das heuristicas fixas.
 * Retorna o tipo e o offset do payload (1 quando ha Report ID no primeiro
 * byte, 0 quando o dispositivo nao usa IDs). */
bool route_by_report_map(const uint8_t *data, uint16_t len, hid_report_kind_t *kind_out, int *offset_out)
{
    if (s_report_count <= 0 || data == nullptr || len < 1 || kind_out == nullptr || offset_out == nullptr) {
        return false;
    }

    for (int i = 0; i < s_report_count; i++) {
        if (s_report_entries[i].report_id != 0 && s_report_entries[i].report_id == data[0]) {
            if (s_report_entries[i].kind == HID_REPORT_UNKNOWN) {
                return false;
            }
            *kind_out = s_report_entries[i].kind;
            *offset_out = 1;
            return true;
        }
    }

    for (int i = 0; i < s_report_count; i++) {
        if (s_report_entries[i].report_id == 0 && s_report_entries[i].kind != HID_REPORT_UNKNOWN) {
            *kind_out = s_report_entries[i].kind;
            *offset_out = 0;
            return true;
        }
    }
    return false;
}

static int handle_gap_notify_rx(struct ble_gap_event *event)
{
    uint16_t len = os_mbuf_len(event->notify_rx.om);
    if (len >= 3) {
        uint8_t data[32] = {};
        os_mbuf_copydata(event->notify_rx.om, 0, len < 32 ? len : 32, data);

        char hex_buf[80] = "";
        int pos = 0;
        for (int i = 0; i < len && i < 20; i++) {
            if (pos >= (int)(sizeof(hex_buf) - 4)) {
                break; /* evita overflow: cada byte ocupa ate 3 chars + NUL */
            }
            pos += snprintf(hex_buf + pos, sizeof(hex_buf) - (size_t)pos, "%02X ", data[i]);
        }
        ESP_LOGI(TAG, "NOTIFY len=%d [%s]", (int)len, hex_buf);

        /* 0. Roteamento pelo Report Map (0x2A4B) lido na descoberta GATT */
        hid_report_kind_t rkind = HID_REPORT_UNKNOWN;
        int roffset = -1;
        bool mapped = route_by_report_map(data, len, &rkind, &roffset);
        bool kbd_routed = (mapped && rkind == HID_REPORT_KEYBOARD && (int)(len - roffset) >= 8);

        if (mapped && rkind == HID_REPORT_MOUSE) {
            const hid_report_entry_t *entry =
                hid_report_map_find(s_report_entries, s_report_count, (roffset == 1) ? data[0] : 0);
            handle_mouse_report_composite(entry, data + roffset, (uint16_t)(len - roffset));
            return 0;
        }
        if (mapped && rkind == HID_REPORT_CONSUMER) {
            ESP_LOGI(TAG, "HID Consumer (midia/volume): ID=0x%02X len=%d", data[roffset], (int)len);
            return 0;
        }

        /* 1. Relatório de Mouse Padrão (Report ID 0x02 ou len 3..5 sem ID) */
        if (!mapped && ((len <= 7 && data[0] == 0x02) || (len <= 5 && data[0] != 0x01))) {
            handle_mouse_report(data + (data[0] == 0x02 ? 1 : 0), (uint16_t)(len - (data[0] == 0x02 ? 1 : 0)));
            return 0;
        }

        /* 2. Relatório de Touchpad / Multi-Touch (Report ID 0x07 e 0x05) */
        static uint16_t s_prev_touch_x = 0;
        static uint16_t s_prev_touch_y = 0;
        static bool s_touch_active = false;
        static uint32_t s_touch_start_time = 0;
        static int32_t s_total_move = 0;

        if (!kbd_routed && data[0] == 0x07 && len >= 4) {
            uint16_t cur_x = data[1] | ((uint16_t)(data[2] & 0x0F) << 8);
            uint16_t cur_y = (data[2] >> 4) | ((uint16_t)data[3] << 4);

            if (!s_touch_active) {
                s_touch_active = true;
                s_touch_start_time = esp_log_timestamp();
                s_total_move = 0;
                s_prev_touch_x = cur_x;
                s_prev_touch_y = cur_y;
            } else {
                int32_t dx = (int32_t)cur_x - (int32_t)s_prev_touch_x;
                int32_t dy = (int32_t)cur_y - (int32_t)s_prev_touch_y;

                s_prev_touch_x = cur_x;
                s_prev_touch_y = cur_y;

                s_total_move += (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);

                if (dx > 80) {
                    dx = 80;
                }
                if (dx < -80) {
                    dx = -80;
                }
                if (dy > 80) {
                    dy = 80;
                }
                if (dy < -80) {
                    dy = -80;
                }

                if (dx != 0 || dy != 0) {
                    ui_mouse_inject_motion((int8_t)dx, (int8_t)dy, 0, 0);
                }
            }
            return 0;
        }

        if (!kbd_routed && data[0] == 0x05) {
            /* Toque finalizado / dedo levantado do touchpad */
            if (s_touch_active) {
                uint32_t dur = esp_log_timestamp() - s_touch_start_time;
                if (dur < 350 && s_total_move < 30) {
                    ESP_LOGI(TAG, "Touchpad: TAP-TO-CLICK detectado (dur=%u ms, move=%d)", (unsigned)dur,
                             (int)s_total_move);
                    ui_mouse_inject_click();
                }
            }
            s_touch_active = false;
            s_prev_touch_x = 0;
            s_prev_touch_y = 0;
            s_total_move = 0;
            return 0;
        }

        /* 3. Relatório de Teclado (boot protocol, Report ID 0x01 ou roteado
         * pelo Report Map com qualquer ID) */
        uint8_t mod = 0;
        int key_offset = 0;

        if (kbd_routed) {
            mod = data[roffset];
            key_offset = roffset + 2;
        } else if (!mapped && len >= 9 && data[0] == 0x01) {
            mod = data[1];
            key_offset = 3;
        } else if (!mapped && len == 8 && data[0] != 0x01) {
            mod = data[0];
            key_offset = 2;
        } else {
            /* Outro tipo de relatório não reconhecido como teclado */
            return 0;
        }

        static uint8_t s_prev_keys[6] = {};
        uint8_t cur_keys[6] = {};
        for (int i = 0; i < 6; i++) {
            if (key_offset + i < (int)len) {
                cur_keys[i] = data[key_offset + i];
            }
        }

        bool shift = (mod & 0x22) != 0;

        for (int i = 0; i < 6; i++) {
            uint8_t k = cur_keys[i];
            if (k == 0) {
                continue;
            }

            bool was_pressed = false;
            for (int j = 0; j < 6; j++) {
                if (s_prev_keys[j] == k) {
                    was_pressed = true;
                    break;
                }
            }

            if (!was_pressed) {
                char ch = 0;
                uint32_t special_key = 0;

                if (k >= 0x04 && k <= 0x1D) { /* a..z / A..Z */
                    ch = (shift ? 'A' : 'a') + (k - 0x04);
                } else if (k >= 0x1E && k <= 0x27) { /* 1..0 / !..() */
                    const char num_normal[] = "1234567890";
                    const char num_shift[] = "!@#$%^&*()";
                    ch = shift ? num_shift[k - 0x1E] : num_normal[k - 0x1E];
                } else {
                    switch (k) {
                    case 0x28:
                        ch = '\n';
                        special_key = LV_KEY_ENTER;
                        break;
                    case 0x2A:
                        ch = '\b';
                        special_key = LV_KEY_BACKSPACE;
                        break;
                    case 0x2B:
                        ch = '\t';
                        break;
                    case 0x2C:
                        ch = ' ';
                        break;
                    case 0x2D:
                        ch = shift ? '_' : '-';
                        break;
                    case 0x2E:
                        ch = shift ? '+' : '=';
                        break;
                    case 0x2F:
                        ch = shift ? '{' : '[';
                        break;
                    case 0x30:
                        ch = shift ? '}' : ']';
                        break;
                    case 0x31:
                        ch = shift ? '|' : '\\';
                        break;
                    case 0x33:
                        ch = shift ? ':' : ';';
                        break;
                    case 0x34:
                        ch = shift ? '"' : '\'';
                        break;
                    case 0x35:
                        ch = shift ? '~' : '`';
                        break;
                    case 0x36:
                        ch = shift ? '<' : ',';
                        break;
                    case 0x37:
                        ch = shift ? '>' : '.';
                        break;
                    case 0x38:
                        ch = shift ? '?' : '/';
                        break;
                    case 0x4C:
                        special_key = LV_KEY_DEL;
                        break;
                    case 0x4F:
                        special_key = LV_KEY_RIGHT;
                        break;
                    case 0x50:
                        special_key = LV_KEY_LEFT;
                        break;
                    case 0x51:
                        special_key = LV_KEY_DOWN;
                        break;
                    case 0x52:
                        special_key = LV_KEY_UP;
                        break;
                    default:
                        break;
                    }
                }

                if (ch != 0) {
                    ESP_LOGI(TAG, "HID Tecla: '%c' (0x%02X, mod=0x%02X)", ch, k, mod);
                    ui_keyboard_inject_char(ch);
                } else if (special_key != 0) {
                    ESP_LOGI(TAG, "HID Tecla Especial: 0x%lX (0x%02X)", (unsigned long)special_key, k);
                    ui_keyboard_inject_key(special_key);
                }
            }
        }

        memcpy(s_prev_keys, cur_keys, sizeof(s_prev_keys));
    }
    return 0;
}

int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    if (event == nullptr) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        return handle_gap_disc(event);

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            finish_scan_locked();
        }
        if (!any_connected_locked()) {
            bt_saved_list_t list = {};
            if (bt_storage_load_all(&list) == ESP_OK && list.count > 0) {
                ESP_LOGI(TAG, "Reiniciando escuta passiva em segundo plano para auto-reconexao...");
                bt_mgr_scan(nullptr, nullptr);
            }
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        return handle_gap_connect(event);

    case BLE_GAP_EVENT_DISCONNECT:
        return handle_gap_disconnect(event);

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        ESP_LOGI(TAG, "BLE Passkey action=%d conn_handle=%d", event->passkey.params.action, event->passkey.conn_handle);
        struct ble_sm_io pkey = {};
        pkey.action = event->passkey.params.action;
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            pkey.numcmp_accept = 1;
        } else if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pkey.passkey = 123456;
            ESP_LOGI(TAG, "Digite no teclado e pressione Enter: 123456");
        } else if (event->passkey.params.action == BLE_SM_IOACT_INPUT) {
            pkey.passkey = 0;
        }
        int rc = ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        ESP_LOGI(TAG, "ble_sm_inject_io rc=%d", rc);
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        ESP_LOGI(TAG, "BLE Repetindo pareamento (limpando chaves antigas)");
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "GAP Encryption alterada status=%d", event->enc_change.status);
        if (event->enc_change.status == 0) {
            struct ble_gap_conn_desc enc_desc = {};
            if (ble_gap_conn_find(event->enc_change.conn_handle, &enc_desc) == 0 && s_bt_mutex != nullptr &&
                xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                active_conn_t *slot = find_slot_by_handle_locked(event->enc_change.conn_handle);
                if (slot != nullptr) {
                    format_mac_addr(enc_desc.peer_id_addr.val, slot->id_mac, sizeof(slot->id_mac));
                    slot->id_addr_type = enc_desc.peer_id_addr.type;
                    ESP_LOGI(TAG, "Bonding concluido: endereco identidade do peer %s (type=%d)", slot->id_mac,
                             (int)slot->id_addr_type);
                }
                xSemaphoreGive(s_bt_mutex);
            }
        }
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        return handle_gap_notify_rx(event);

    default:
        return 0;
    }
    return 0;
}

void ble_app_on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE Host sincronizado com o Controller!");
    s_nimble_synced = true;

    if (!s_bt_enabled) {
        ESP_LOGI(TAG, "Bluetooth desativado nas configuracoes - auto-conexao pausada");
        return;
    }

    /* Tenta conexão direta imediata com o dispositivo pareado */
    bt_saved_list_t list = {};
    if (bt_storage_load_all(&list) == ESP_OK && list.count > 0) {
        for (int i = 0; i < list.count; i++) {
            if (list.items[i].paired && list.items[i].auto_connect) {
                ESP_LOGI(TAG, "Iniciando auto-reconexao direta com %s [%s]...", list.items[i].name, list.items[i].mac);
                bt_mgr_connect(list.items[i].mac, list.items[i].name, list.items[i].type);
                return;
            }
        }
        ESP_LOGI(TAG, "Iniciando escuta para auto-reconexao de %d dispositivo(s)...", list.count);
        bt_mgr_scan(nullptr, nullptr);
    }
}

void ble_app_on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE Host resetado (motivo: %d)", reason);
    s_nimble_synced = false;

    char was_mac[18] = {};
    if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < s_active_count; i++) {
            if (!s_active_conns[i].connected) {
                remove_slot_locked(i);
                i--;
            }
        }
        if (s_pending.active) {
            snprintf(was_mac, sizeof(was_mac), "%s", s_pending.mac);
            s_pending.active = false;
            s_pending.mac[0] = '\0';
        }
        xSemaphoreGive(s_bt_mutex);
    }
    if (was_mac[0] != '\0') {
        notify_conn_event(was_mac, BT_CONN_FAILED, reason);
    }
}

void ble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "BLE Host Task iniciada");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void update_device_connection_flags_locked(void)
{
    for (int i = 0; i < s_discovered_count; i++) {
        s_discovered[i].connected = false;
        for (int j = 0; j < s_active_count; j++) {
            if (s_active_conns[j].connected && strcasecmp(s_discovered[i].mac, s_active_conns[j].mac) == 0) {
                s_discovered[i].connected = true;
                break;
            }
        }
        bt_saved_device_t saved = {};
        s_discovered[i].paired = bt_storage_find(s_discovered[i].mac, &saved) && saved.paired;
    }
}

} // namespace

bool bt_mgr_is_enabled(void)
{
    return s_bt_enabled;
}

esp_err_t bt_mgr_set_enabled(bool enabled)
{
    if (s_bt_enabled == enabled) {
        return ESP_OK;
    }
    s_bt_enabled = enabled;
    save_nvs_bt_enabled(enabled);
    ESP_LOGI(TAG, "Bluetooth %s pelo usuario", enabled ? "HABILITADO" : "DESABILITADO");

    if (!enabled) {
        ble_gap_disc_cancel();
        if (s_pending.active) {
            s_pending.cancel_expected = true;
            ble_gap_conn_cancel();
        }
        s_pending.active = false;
        s_pending.mac[0] = '\0';
        s_scanning = false;
        if (s_scan_watchdog != nullptr) {
            xTimerStop(s_scan_watchdog, 0);
        }

        if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (int i = 0; i < s_active_count; i++) {
                if (s_active_conns[i].connected && s_active_conns[i].conn_handle != BLE_HS_CONN_HANDLE_NONE) {
                    ble_gap_terminate(s_active_conns[i].conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                }
                s_active_conns[i].connected = false;
                s_active_conns[i].conn_handle = BLE_HS_CONN_HANDLE_NONE;
            }
            s_active_count = 0;
            for (int i = 0; i < s_discovered_count; i++) {
                s_discovered[i].connected = false;
            }
            xSemaphoreGive(s_bt_mutex);
        }
        s_curr_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ui_keyboard_notify_hardware_change();
        ui_mouse_set_connected(false);
    } else {
        if (s_nimble_synced) {
            bt_saved_list_t list = {};
            if (bt_storage_load_all(&list) == ESP_OK && list.count > 0) {
                for (int i = 0; i < list.count; i++) {
                    if (list.items[i].paired && list.items[i].auto_connect) {
                        ESP_LOGI(TAG, "Auto-reconectando ao dispositivo salvo: %s [%s]...", list.items[i].name,
                                 list.items[i].mac);
                        bt_mgr_connect(list.items[i].mac, list.items[i].name, list.items[i].type);
                        return ESP_OK;
                    }
                }
                ESP_LOGI(TAG, "Iniciando escuta para auto-reconexao de %d dispositivo(s)...", list.count);
                bt_mgr_scan(nullptr, nullptr);
            }
        }
    }
    return ESP_OK;
}

esp_err_t bt_mgr_start(void)
{
    load_nvs_bt_enabled();

    if (s_nimble_inited) {
        return ESP_OK;
    }

    if (s_bt_mutex == nullptr) {
        s_bt_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "Iniciando subsistema NimBLE Bluetooth (habilitado=%d)...", (int)s_bt_enabled);

    if (s_scan_watchdog == nullptr) {
        s_scan_watchdog = xTimerCreate("bt_sc_wd", pdMS_TO_TICKS(5500), pdFALSE, nullptr, scan_watchdog_cb);
    }

    /* Desde o esp-hosted 2.5.2 o controller BT do coprocessador (C6) nasce
     * desligado: inicializar e habilitar explicitamente antes do host stack. */
    esp_err_t hrc = esp_hosted_bt_controller_init();
    if (hrc != ESP_OK) {
        ESP_LOGW(TAG, "esp_hosted_bt_controller_init falhou: %d", hrc);
    }
    hrc = esp_hosted_bt_controller_enable();
    if (hrc != ESP_OK) {
        ESP_LOGW(TAG, "esp_hosted_bt_controller_enable falhou: %d", hrc);
    }

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init falhou: %d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.sync_cb = ble_app_on_sync;
    ble_hs_cfg.reset_cb = ble_app_on_reset;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_store_config_init();

    nimble_port_freertos_init(ble_host_task);
    s_nimble_inited = true;

    /* Carrega dispositivos salvos */
    bt_saved_list_t list = {};
    if (bt_storage_load_all(&list) == ESP_OK) {
        ESP_LOGI(TAG, "Dispositivos salvos carregados (%d)", list.count);
    }

    return ESP_OK;
}

esp_err_t bt_mgr_scan(bt_scan_cb_t cb, void *ctx)
{
    if (!s_bt_enabled) {
        ESP_LOGW(TAG, "Tentativa de scan ignorada: Bluetooth desativado");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_bt_mutex == nullptr) {
        s_bt_mutex = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    /* Um scan novo cancela tentativa de conexao pendente: o CONNECT que
     * chegar com falha nao deve contar como backoff nem alarmar a UI. */
    if (s_pending.active) {
        s_pending.cancel_expected = true;
    }
    ble_gap_conn_cancel();
    ble_gap_disc_cancel();
    s_scanning = false;

    s_scanning = true;
    s_scan_cb = cb;
    s_scan_ctx = ctx;
    s_discovered_count = 0;

    /* Carrega dispositivos ja salvos no SD primeiro */
    bt_saved_list_t saved_list = {};
    if (bt_storage_load_all(&saved_list) == ESP_OK) {
        for (int i = 0; i < saved_list.count && s_discovered_count < BT_SCAN_MAX_DEVICES; i++) {
            bt_device_info_t *dev = &s_discovered[s_discovered_count++];
            snprintf(dev->mac, sizeof(dev->mac), "%s", saved_list.items[i].mac);
            snprintf(dev->name, sizeof(dev->name), "%s", saved_list.items[i].name);
            dev->type = saved_list.items[i].type;
            dev->addr_type = saved_list.items[i].addr_type;
            dev->rssi = -55;
            dev->paired = saved_list.items[i].paired;
            dev->connected = false;
            for (int j = 0; j < s_active_count; j++) {
                if (s_active_conns[j].connected && strcasecmp(dev->mac, s_active_conns[j].mac) == 0) {
                    dev->connected = true;
                    break;
                }
            }
        }
    }

    xSemaphoreGive(s_bt_mutex);

    if (!s_nimble_inited) {
        bt_mgr_start();
    }

    struct ble_gap_disc_params disc_params = {};
    disc_params.filter_duplicates = 0;
    disc_params.passive = 0;
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    if (s_scan_watchdog != nullptr) {
        xTimerStop(s_scan_watchdog, 0);
        xTimerStart(s_scan_watchdog, 0);
    }

    uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
    ble_hs_id_infer_auto(0, &own_addr_type);

    ESP_LOGI(TAG, "Iniciando ble_gap_disc (duracao 5000ms, own_addr_type=%d)...", (int)own_addr_type);
    int rc = ble_gap_disc(own_addr_type, 5000, &disc_params, ble_gap_event_cb, nullptr);

    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc falhou: %d", rc);
        if (xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            finish_scan_locked();
        }
        return ESP_FAIL;
    }

    return ESP_OK;
}

/**
 * @brief Inicia conexão BLE com um dispositivo HID.
 *
 * Valida o estado do Bluetooth e do host NimBLE, o endereço MAC e inicia o
 * procedimento GAP Connect. Retorna erros reais (ocupado, controlador recusou)
 * e não persiste nada no bt.cfg: o resultado final chega pelo callback
 * registrado em bt_mgr_set_conn_callback, e a persistência como "pareado"
 * só ocorre quando a inicialização HID é concluída.
 *
 * @param mac   Endereço MAC do dispositivo no formato "XX:XX:XX:XX:XX:XX".
 * @param name  Nome amigável do dispositivo (para logs e UI).
 * @param type  Tipo do dispositivo HID (BT_DEV_KEYBOARD, BT_DEV_MOUSE, etc.).
 * @return      ESP_OK em sucesso, ESP_ERR_INVALID_STATE se BT desabilitado,
 *              ESP_ERR_INVALID_ARG se mac for nulo/vazio.
 */
esp_err_t bt_mgr_connect(const char *mac, const char *name, bt_dev_type_t type)
{
    /* Passo 1: Validação do estado geral do subsistema Bluetooth */
    if (!s_bt_enabled) {
        ESP_LOGW(TAG, "Tentativa de conexao ignorada: Bluetooth desativado");
        return ESP_ERR_INVALID_STATE;
    }

    /* Passo 2: Validação dos parâmetros obrigatórios de entrada */
    if (mac == nullptr || mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_nimble_inited || !s_nimble_synced) {
        ESP_LOGW(TAG, "Tentativa de conexao antes do host NimBLE sincronizar");
        return ESP_ERR_INVALID_STATE;
    }

    /* Passo 3: Prevenção de concorrência com procedimentos GAP já ativos */
    if (ble_gap_conn_active()) {
        if (s_pending.active && strcasecmp(s_pending.mac, mac) == 0) {
            ESP_LOGI(TAG, "Conexao com %s ja esta em andamento", mac);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "Conexao recusada: procedimento GAP ja ativo (alvo atual: %s)",
                 s_pending.active ? s_pending.mac : "desconhecido");
        return ESP_ERR_INVALID_STATE;
    }

    /* Passo 4: Sincronização e exclusão mútua com mutex do gerenciador */
    if (s_bt_mutex != nullptr) {
        xSemaphoreTake(s_bt_mutex, portMAX_DELAY);
    }

    /* Passo 5: Interrupção do scan ativo antes de iniciar a conexão GAP */
    ble_gap_disc_cancel();
    s_scanning = false;
    if (s_scan_watchdog != nullptr) {
        xTimerStop(s_scan_watchdog, 0);
    }

    /* Passo 6: Resolução do tipo de endereço BLE (Public vs Random) */
    uint8_t addr_type = BLE_ADDR_PUBLIC;
    bool found_addr_type = false;
    for (int i = 0; i < s_discovered_count; i++) {
        if (strcasecmp(s_discovered[i].mac, mac) == 0) {
            addr_type = s_discovered[i].addr_type;
            found_addr_type = true;
            break;
        }
    }
    if (!found_addr_type) {
        bt_saved_device_t saved = {};
        if (bt_storage_find(mac, &saved)) {
            addr_type = saved.addr_type;
        }
    }

    ESP_LOGI(TAG, "Iniciando conexao física GAP com %s [%s] (addr_type=%d)...", name ? name : "Dispositivo", mac,
             (int)addr_type);

    /* Passo 7: Alocação ou reutilização de slot na tabela de conexões ativas */
    int conn_idx = -1;
    for (int i = 0; i < s_active_count; i++) {
        if (strcasecmp(s_active_conns[i].mac, mac) == 0) {
            conn_idx = i;
            break;
        }
    }
    if (conn_idx < 0 && !any_connected_locked() && s_active_count > 0) {
        /* Reaproveita slot pendente órfão de tentativa anterior */
        conn_idx = 0;
        s_active_count = 1;
    }
    if (conn_idx < 0 && s_active_count < MAX_ACTIVE_CONNS) {
        conn_idx = s_active_count++;
    }

    /* Passo 8: Inicialização dos metadados da conexão ativa */
    if (conn_idx >= 0) {
        active_conn_t *conn = &s_active_conns[conn_idx];
        memset(conn, 0, sizeof(*conn));
        snprintf(conn->mac, sizeof(conn->mac), "%s", mac);
        snprintf(conn->name, sizeof(conn->name), "%s", name ? name : "Dispositivo Bluetooth");
        conn->type = type;
        conn->addr_type = addr_type;
        conn->connected = false;
        conn->conn_handle = BLE_HS_CONN_HANDLE_NONE;
    } else {
        ESP_LOGW(TAG, "Sem slot livre para registrar a tentativa de conexao de %s", mac);
    }

    /* Passo 9: Conversão do endereço e disparo da conexão GAP NimBLE.
     * Nada é gravado no bt.cfg aqui: a persistência como pareado só ocorre
     * quando o HID fica pronto (persist_paired_device_locked). */
    esp_err_t ret;
    ble_addr_t peer_addr = {};
    if (parse_mac_addr(mac, &peer_addr)) {
        peer_addr.type = addr_type;
        uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
        ble_hs_id_infer_auto(0, &own_addr_type);

        snprintf(s_pending.mac, sizeof(s_pending.mac), "%s", mac);
        s_pending.cancel_expected = false;
        s_pending.user_initiated = true;

        ESP_LOGI(TAG, "Executando ble_gap_connect own_addr_type=%d peer_addr.type=%d [%s]...", (int)own_addr_type,
                 (int)peer_addr.type, mac);
        int rc = ble_gap_connect(own_addr_type, &peer_addr, 30000, nullptr, ble_gap_event_cb, nullptr);
        if (rc != 0) {
            ESP_LOGW(TAG, "ble_gap_connect com type %d falhou (rc=%d), tentando tipo alternativo...",
                     (int)peer_addr.type, rc);
            peer_addr.type = (peer_addr.type == BLE_ADDR_PUBLIC) ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
            rc = ble_gap_connect(own_addr_type, &peer_addr, 30000, nullptr, ble_gap_event_cb, nullptr);
        }

        if (rc != 0) {
            ESP_LOGE(TAG, "ble_gap_connect falhou nos dois tipos de endereco: rc=%d", rc);
            autoconn_mark_failed_locked(mac);
            s_pending.active = false;
            s_pending.mac[0] = '\0';
            if (conn_idx >= 0) {
                remove_slot_locked(conn_idx);
            }
            update_device_connection_flags_locked();
            if (s_bt_mutex != nullptr) {
                xSemaphoreGive(s_bt_mutex);
            }
            notify_conn_event(mac, BT_CONN_FAILED, rc);
            return ESP_ERR_INVALID_RESPONSE;
        }

        s_pending.active = true;
        ret = ESP_OK;
    } else {
        ESP_LOGE(TAG, "MAC invalido para conexao: %s", mac);
        ret = ESP_ERR_INVALID_ARG;
        s_pending.mac[0] = '\0';
    }

    /* Passo 10: Atualização de flags de interface e liberação do mutex */
    update_device_connection_flags_locked();
    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Procedimento de conexao iniciado para %s", mac);
        notify_conn_event(mac, BT_CONN_STARTED, 0);
    }
    return ret;
}

void bt_mgr_set_conn_callback(bt_conn_cb_t cb, void *ctx)
{
    s_conn_cb = cb;
    s_conn_ctx = ctx;
}

esp_err_t bt_mgr_disconnect(const char *mac)
{
    if (mac == nullptr || mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_bt_mutex != nullptr) {
        xSemaphoreTake(s_bt_mutex, portMAX_DELAY);
    }

    ESP_LOGI(TAG, "Desconectando de %s...", mac);
    if (s_pending.active && strcasecmp(s_pending.mac, mac) == 0) {
        s_pending.cancel_expected = true;
        ble_gap_conn_cancel();
        s_pending.active = false;
        s_pending.mac[0] = '\0';
    }

    for (int i = 0; i < s_active_count; i++) {
        if (strcasecmp(s_active_conns[i].mac, mac) == 0) {
            if (s_active_conns[i].conn_handle != 0) {
                ble_gap_terminate(s_active_conns[i].conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                s_active_conns[i].conn_handle = 0;
            }
            s_active_conns[i].connected = false;
            for (int j = i; j < s_active_count - 1; j++) {
                s_active_conns[j] = s_active_conns[j + 1];
            }
            s_active_count--;
            break;
        }
    }

    update_device_connection_flags_locked();
    ui_mouse_set_connected(false);
    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }
    return ESP_OK;
}

esp_err_t bt_mgr_forget(const char *mac)
{
    if (mac == nullptr || mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Esquecendo dispositivo %s...", mac);
    bt_mgr_disconnect(mac);
    bt_storage_remove(mac);

    if (s_bt_mutex != nullptr) {
        xSemaphoreTake(s_bt_mutex, portMAX_DELAY);
    }
    autoconn_clear_locked(mac);
    update_device_connection_flags_locked();
    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }
    return ESP_OK;
}

esp_err_t bt_mgr_get_status(bt_status_t *status)
{
    if (status == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    if (!s_bt_enabled) {
        return ESP_OK;
    }

    if (s_bt_mutex != nullptr) {
        xSemaphoreTake(s_bt_mutex, portMAX_DELAY);
    }
    status->scanning = s_scanning;
    status->connected_count = 0;

    for (int i = 0; i < s_active_count; i++) {
        if (s_active_conns[i].connected) {
            status->connected_count++;
            status->any_connected = true;
            strlcpy(status->last_connected_mac, s_active_conns[i].mac, sizeof(status->last_connected_mac));
            strlcpy(status->last_connected_name, s_active_conns[i].name, sizeof(status->last_connected_name));

            if (s_active_conns[i].type == BT_DEV_TYPE_KEYBOARD) {
                status->keyboard_connected = true;
                status->mouse_connected = true;
            } else if (s_active_conns[i].type == BT_DEV_TYPE_MOUSE) {
                status->mouse_connected = true;
            } else if (s_active_conns[i].type == BT_DEV_TYPE_HEADPHONE) {
                status->audio_connected = true;
            }
        }
    }

    /* Fallback garantido: se ha conexao fisica ativa no host NimBLE */
    if (!status->any_connected && s_curr_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        status->any_connected = true;
        status->connected_count = (status->connected_count == 0) ? 1 : status->connected_count;
    }

    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }
    return ESP_OK;
}

bool bt_mgr_is_keyboard_connected(void)
{
    if (s_bt_mutex != nullptr) {
        xSemaphoreTake(s_bt_mutex, portMAX_DELAY);
    }
    for (int i = 0; i < s_active_count; i++) {
        if (s_active_conns[i].connected &&
            (s_active_conns[i].type == BT_DEV_TYPE_KEYBOARD || s_active_conns[i].type == BT_DEV_TYPE_GENERIC)) {
            if (s_bt_mutex != nullptr) {
                xSemaphoreGive(s_bt_mutex);
            }
            return true;
        }
    }
    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }
    return false;
}

bool bt_mgr_is_mouse_connected(void)
{
    if (s_bt_mutex != nullptr) {
        xSemaphoreTake(s_bt_mutex, portMAX_DELAY);
    }
    for (int i = 0; i < s_active_count; i++) {
        if (s_active_conns[i].connected &&
            (s_active_conns[i].type == BT_DEV_TYPE_MOUSE || s_active_conns[i].type == BT_DEV_TYPE_KEYBOARD)) {
            if (s_bt_mutex != nullptr) {
                xSemaphoreGive(s_bt_mutex);
            }
            return true;
        }
    }
    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }
    return false;
}

bool bt_mgr_is_audio_connected(void)
{
    if (s_bt_mutex != nullptr) {
        xSemaphoreTake(s_bt_mutex, portMAX_DELAY);
    }
    for (int i = 0; i < s_active_count; i++) {
        if (s_active_conns[i].connected && s_active_conns[i].type == BT_DEV_TYPE_HEADPHONE) {
            if (s_bt_mutex != nullptr) {
                xSemaphoreGive(s_bt_mutex);
            }
            return true;
        }
    }
    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }
    return false;
}
