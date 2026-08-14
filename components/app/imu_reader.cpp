#include "imu_reader.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "bsp/esp-bsp.h"
#include "iot_sensor_hub.h"
#include "sensor_type.h"
#include "orientation.h"
#include "ui_status.h"

static const char *TAG = "tab5_imu";

#define NVS_NAMESPACE "tab5"
#define NVS_KEY_ROT_ENABLED "rot_enabled"

static sensor_handle_t s_imu = NULL;
static lv_display_t *s_disp = NULL;
static volatile lv_disp_rotation_t s_target_rot = LV_DISPLAY_ROTATION_0;
static volatile bool s_rotation_enabled = true;
static uint32_t s_log_div = 0;

static void imu_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    const sensor_data_t *data = (const sensor_data_t *)event_data;
    if (data == NULL) {
        return;
    }

    switch (event_id) {
    case SENSOR_ACCE_DATA_READY:
        /* Somente calcula o alvo; a rotacao e aplicada pelo timer na task LVGL */
        s_target_rot = orientation_update(data->acce.x, data->acce.y, data->acce.z);
        if ((s_log_div++ % 50) == 0) {
            ESP_LOGI(TAG, "acce x=%.2f y=%.2f z=%.2f G", data->acce.x, data->acce.y, data->acce.z);
        }
        break;
    case SENSOR_GYRO_DATA_READY:
        break;
    default:
        break;
    }
}

/* Roda dentro da task LVGL (lv_timer_handler): chamadas LVGL seguras, sem lock */
static void rotation_timer_cb(lv_timer_t *timer)
{
    if (!s_rotation_enabled) {
        return;
    }

    lv_display_t *disp = (lv_display_t *)lv_timer_get_user_data(timer);
    lv_disp_rotation_t target = s_target_rot;
    if (target != lv_display_get_rotation(disp)) {
        lv_display_set_rotation(disp, target);
        ui_status_set_rotation(target);
        ESP_LOGI(TAG, "rotacao aplicada -> %d", (int)target);
    }
}

esp_err_t imu_reader_start(lv_display_t *disp)
{
    s_disp = disp;

    const bsp_sensor_config_t cfg = {
        .type = IMU_ID,
        .mode = MODE_POLLING,
        .period = 100, /* 10 Hz */
    };

    ESP_RETURN_ON_ERROR(bsp_sensor_init(&cfg, &s_imu), TAG, "bsp_sensor_init failed");
    ESP_RETURN_ON_ERROR(iot_sensor_handler_register(s_imu, imu_event_handler, NULL), TAG,
                        "handler register failed");
    ESP_RETURN_ON_ERROR(iot_sensor_start(s_imu), TAG, "sensor start failed");

    lv_timer_create(rotation_timer_cb, 150, disp);

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t enabled = 1;
        if (nvs_get_u8(nvs, NVS_KEY_ROT_ENABLED, &enabled) == ESP_OK) {
            s_rotation_enabled = (enabled != 0);
        }
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "rotacao via IMU %s", s_rotation_enabled ? "habilitada" : "desabilitada");

    ESP_LOGI(TAG, "IMU iniciado (10 Hz)");
    return ESP_OK;
}

void imu_reader_set_rotation_enabled(bool enabled)
{
    if (s_rotation_enabled == enabled) {
        return;
    }
    s_rotation_enabled = enabled;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_ROT_ENABLED, enabled ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "rotacao via IMU %s", enabled ? "habilitada" : "desabilitada");
}

bool imu_reader_is_rotation_enabled(void)
{
    return s_rotation_enabled;
}
