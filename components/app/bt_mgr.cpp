#include "bt_mgr.h"
#include "bt_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_log.h"
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
};

#define MAX_ACTIVE_CONNS 4

SemaphoreHandle_t s_bt_mutex = nullptr;
active_conn_t s_active_conns[MAX_ACTIVE_CONNS] = {};
int s_active_count = 0;

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

/* Pipeline Sequencial de GATT, Descritores 0x2902 e CCCD */
int on_write_cccd_seq(uint16_t conn_handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg);

void write_next_cccd(void)
{
    if (s_cccd_queue_idx >= s_cccd_queue_count || s_curr_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGI(TAG, "Todas as notificacoes (%d CCCDs) foram ativadas com sucesso! Teclado e Mouse ativos.",
                 s_cccd_queue_count);
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

int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    if (event == nullptr) {
        return 0;
    }

    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
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
            if (bt_storage_find(mac_str, &saved) && saved.paired) {
                is_paired = true;
            } else if (name_str[0] != '\0') {
                bt_saved_list_t list = {};
                if (bt_storage_load_all(&list) == ESP_OK) {
                    for (int k = 0; k < list.count; k++) {
                        if (strcasecmp(list.items[k].name, name_str) == 0 && list.items[k].paired) {
                            saved = list.items[k];
                            is_paired = true;
                            break;
                        }
                    }
                }
            }

            if (is_paired && saved.auto_connect && s_active_count == 0 && !ble_gap_conn_active()) {
                ESP_LOGI(TAG,
                         "Dispositivo pareado detectado no ar! Auto-conectando imediatamente: %s [%s] (addr_type=%d)",
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

    case BLE_GAP_EVENT_DISC_COMPLETE:
        if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            finish_scan_locked();
        }
        if (s_active_count == 0) {
            bt_saved_list_t list = {};
            if (bt_storage_load_all(&list) == ESP_OK && list.count > 0) {
                ESP_LOGI(TAG, "Reiniciando escuta passiva em segundo plano para auto-reconexao...");
                bt_mgr_scan(nullptr, nullptr);
            }
        }
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "GAP Conectado com sucesso! conn_handle=%d", event->connect.conn_handle);
            s_curr_conn_handle = event->connect.conn_handle;

            if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                for (int i = 0; i < s_active_count; i++) {
                    s_active_conns[i].conn_handle = event->connect.conn_handle;
                    s_active_conns[i].connected = true;
                }
                for (int i = 0; i < s_discovered_count; i++) {
                    for (int j = 0; j < s_active_count; j++) {
                        if (strcasecmp(s_discovered[i].mac, s_active_conns[j].mac) == 0) {
                            s_discovered[i].connected = true;
                            s_discovered[i].paired = true;
                        }
                    }
                }
                xSemaphoreGive(s_bt_mutex);
            }

            ble_gap_security_initiate(event->connect.conn_handle);

            s_hid_start_handle = 0;
            s_hid_end_handle = 0;
            ble_gattc_disc_all_svcs(event->connect.conn_handle, on_disc_svc_seq, nullptr);
        } else {
            ESP_LOGW(TAG, "GAP Falha na conexao fisica status=%d", event->connect.status);
            if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                for (int i = 0; i < s_active_count; i++) {
                    if (s_active_conns[i].conn_handle == 0) {
                        s_active_conns[i].connected = false;
                    }
                }
                xSemaphoreGive(s_bt_mutex);
            }
            if (s_active_count == 0) {
                bt_mgr_scan(nullptr, nullptr);
            }
        }
        ui_keyboard_notify_hardware_change();
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "GAP Desconectado reason=%d", event->disconnect.reason);
        if (s_curr_conn_handle == event->disconnect.conn.conn_handle) {
            s_curr_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        }
        if (s_bt_mutex != nullptr && xSemaphoreTake(s_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (int i = 0; i < s_active_count; i++) {
                if (s_active_conns[i].conn_handle == event->disconnect.conn.conn_handle) {
                    s_active_conns[i].connected = false;
                    s_active_conns[i].conn_handle = BLE_HS_CONN_HANDLE_NONE;
                }
            }
            for (int i = 0; i < s_discovered_count; i++) {
                s_discovered[i].connected = false;
            }
            xSemaphoreGive(s_bt_mutex);
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
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
        if (len >= 3) {
            uint8_t data[32] = {};
            os_mbuf_copydata(event->notify_rx.om, 0, len < 32 ? len : 32, data);

            char hex_buf[80] = "";
            int pos = 0;
            for (int i = 0; i < len && i < 20; i++) {
                pos += snprintf(hex_buf + pos, sizeof(hex_buf) - pos, "%02X ", data[i]);
            }
            ESP_LOGI(TAG, "NOTIFY len=%d [%s]", (int)len, hex_buf);

            /* 1. Relatório de Mouse Padrão (Report ID 0x02 ou len 3..5 sem ID) */
            if ((len <= 7 && data[0] == 0x02) || (len <= 5 && data[0] != 0x01)) {
                int offset = (data[0] == 0x02) ? 1 : 0;
                uint8_t buttons = data[offset];
                int8_t dx = (int8_t)data[offset + 1];
                int8_t dy = (int8_t)data[offset + 2];
                int8_t wheel = (len > (uint16_t)(offset + 3)) ? (int8_t)data[offset + 3] : 0;
                ESP_LOGI(TAG, "HID Mouse Padrão: btn=0x%02X dx=%d dy=%d wheel=%d", buttons, dx, dy, wheel);
                ui_mouse_inject_motion(dx, dy, buttons, wheel);
                return 0;
            }

            /* 2. Relatório de Touchpad / Multi-Touch (Report ID 0x07 e 0x05) */
            static uint16_t s_prev_touch_x = 0;
            static uint16_t s_prev_touch_y = 0;
            static bool s_touch_active = false;
            static uint32_t s_touch_start_time = 0;
            static int32_t s_total_move = 0;

            if (data[0] == 0x07 && len >= 4) {
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
            } else if (data[0] == 0x05) {
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

            /* 3. Relatório de Teclado (Report ID 0x01 com len >= 9 ou 8 bytes sem ID) */
            uint8_t mod = 0;
            int key_offset = 0;

            if (len >= 9 && data[0] == 0x01) {
                mod = data[1];
                key_offset = 3;
            } else if (len == 8 && data[0] != 0x01) {
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
                if (k == 0)
                    continue;

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
        ble_gap_conn_cancel();
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

esp_err_t bt_mgr_connect(const char *mac, const char *name, bt_dev_type_t type)
{
    if (!s_bt_enabled) {
        ESP_LOGW(TAG, "Tentativa de conexao ignorada: Bluetooth desativado");
        return ESP_ERR_INVALID_STATE;
    }

    if (mac == nullptr || mac[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (ble_gap_conn_active()) {
        ESP_LOGI(TAG, "Procedimento GAP Connect ja esta em andamento no rádio! Aguardando...");
        return ESP_OK;
    }

    if (s_bt_mutex != nullptr) {
        xSemaphoreTake(s_bt_mutex, portMAX_DELAY);
    }

    ble_gap_disc_cancel();
    s_scanning = false;
    if (s_scan_watchdog != nullptr) {
        xTimerStop(s_scan_watchdog, 0);
    }

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

    int conn_idx = -1;
    for (int i = 0; i < s_active_count; i++) {
        if (strcasecmp(s_active_conns[i].mac, mac) == 0) {
            conn_idx = i;
            break;
        }
    }
    if (conn_idx < 0 && s_active_count < MAX_ACTIVE_CONNS) {
        conn_idx = s_active_count++;
    }

    if (conn_idx >= 0) {
        active_conn_t *conn = &s_active_conns[conn_idx];
        snprintf(conn->mac, sizeof(conn->mac), "%s", mac);
        snprintf(conn->name, sizeof(conn->name), "%s", name ? name : "Dispositivo Bluetooth");
        conn->type = type;
        conn->addr_type = addr_type;
        conn->connected = false;
    }

    ble_addr_t peer_addr = {};
    peer_addr.type = addr_type;
    if (parse_mac_addr(mac, &peer_addr)) {
        uint8_t own_addr_type = BLE_OWN_ADDR_PUBLIC;
        ble_hs_id_infer_auto(0, &own_addr_type);

        ESP_LOGI(TAG, "Executando ble_gap_connect own_addr_type=%d peer_addr.type=%d [%s]...", (int)own_addr_type,
                 (int)peer_addr.type, mac);
        int rc = ble_gap_connect(own_addr_type, &peer_addr, 30000, nullptr, ble_gap_event_cb, nullptr);
        if (rc != 0) {
            ESP_LOGW(TAG, "ble_gap_connect com type %d falhou (rc=%d), tentando tipo alternativo...",
                     (int)peer_addr.type, rc);
            peer_addr.type = (peer_addr.type == BLE_ADDR_PUBLIC) ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
            rc = ble_gap_connect(own_addr_type, &peer_addr, 30000, nullptr, ble_gap_event_cb, nullptr);
            if (rc != 0) {
                ESP_LOGE(TAG, "ble_gap_connect falhou: %d", rc);
            }
        }
    }

    bt_saved_device_t dev = {};
    snprintf(dev.mac, sizeof(dev.mac), "%s", mac);
    snprintf(dev.name, sizeof(dev.name), "%s", name ? name : "Dispositivo Bluetooth");
    dev.type = type;
    dev.addr_type = addr_type;
    dev.paired = true;
    dev.auto_connect = true;
    bt_storage_add_or_update(&dev);

    update_device_connection_flags_locked();
    if (s_bt_mutex != nullptr) {
        xSemaphoreGive(s_bt_mutex);
    }

    ESP_LOGI(TAG, "Dispositivo %s registrado para conexao", mac);
    return ESP_OK;
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
    ble_gap_conn_cancel();

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
            snprintf(status->last_connected_mac, sizeof(status->last_connected_mac), "%s", s_active_conns[i].mac);
            snprintf(status->last_connected_name, sizeof(status->last_connected_name), "%s", s_active_conns[i].name);

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
