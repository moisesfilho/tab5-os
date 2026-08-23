#include "battery_reader.h"

#include <cmath>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_io_expander.h"
#include "nvs.h"
#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

static const char *TAG = "tab5_battery";

/* INA226 @ 0x41, shunt 5 mOhm (hardware Tab5):
 *   current_lsb 300 uA/LSB -> cal = 0.00512 / (0.0003 * 0.005) = 3413 (0x0D55)
 *   bus_lsb 1.25 mV/LSB; config: avg=16, conversoes ~588us, modo continuo */
#define INA226_I2C_ADDRESS 0x41
#define INA226_REG_CONFIG 0x00
#define INA226_REG_BUS 0x02
#define INA226_REG_CURRENT 0x04
#define INA226_REG_CAL 0x05
#define INA226_CONFIG_VALUE 0x4527U
#define INA226_CAL_VALUE 0x0D55U
#define INA226_CURRENT_UA_LSB 300

/* CHG_STAT (entrada, ativo-baixo = carregando) e CHG_EN (saida, 1 = carregador
 * habilitado) no IO expander B (0x44). O carregador IP2326 vem DESABILITADO
 * por padrao: sem subir o CHG_EN o aparelho nunca carrega. */
#define BSP_IOX_B_CHG_STAT (IO_EXPANDER_PIN_NUM_6)
#define BSP_IOX_B_CHG_EN (IO_EXPANDER_PIN_NUM_7)

#define SAMPLE_PERIOD_MS 1000
#define BATTERY_CAPACITY_MAH 2000
#define PERCENT_STEP_DIVISOR ((float)(BATTERY_CAPACITY_MAH * 36)) /* mA*s -> % */
#define VOLT_EMPTY_MV 6000
#define VOLT_FULL_MV 8400
#define EXT_CURRENT_LIMIT_MA 15
#define EXT_VOLTAGE_MIN_MV 7900
/* Sem bateria o carregador segura o VSYS em ~8380mV estavel (com pulsos
 * periodicos que derrubam a leitura a ~4250mV); bateria cheia flutua abaixo */
#define NO_BATTERY_VOLTAGE_MV 8330
#define EXT_STABLE_SAMPLES 5
#define VOTE_CLAMP 10
#define MAX_CONSECUTIVE_ERRORS 3
#define LOG_DIVIDER 10

/* Protecao de carregamento: corta a carga em 90% e retoma em 85%.
 * A guarda de tensao (>= 8200 mV sob carga) evita que uma estimativa
 * inicial otimista de percentual corte a carga antes da hora. */
#define NVS_NAMESPACE "tab5"
#define NVS_KEY_CHG_PROTECT "chg_protect"
#define CHG_PROTECT_LIMIT_PCT 90.0f
#define CHG_PROTECT_RESUME_PCT 85.0f
#define CHG_PROTECT_MIN_VOLT_MV 8200

static i2c_master_dev_handle_t s_ina = NULL;
static lv_timer_t *s_sample_timer = NULL;
static battery_status_t s_status = {};
static float s_percent_f = -1.0f;
static int s_ext_votes = 0;
static int s_nobat_votes = 0;
static int s_error_count = 0;
static uint32_t s_sample_count = 0;
static bool s_protect_enabled = true; /* padrao ligado */
static bool s_protect_active = false; /* corte em execucao */

static esp_err_t ina_read16(uint8_t reg, uint16_t *out)
{
    uint8_t rx[2];
    esp_err_t err = i2c_master_transmit_receive(s_ina, &reg, 1, rx, 2, 100);
    if (err != ESP_OK) {
        return err;
    }
    *out = ((uint16_t)rx[0] << 8) | rx[1];
    return ESP_OK;
}

static bool chg_stat_configure(void)
{
    static bool s_configured = false;
    esp_io_expander_handle_t exp1 = bsp_io_expander1_init();
    if (exp1 == NULL) {
        return false;
    }
    if (!s_configured) {
        /* CHG_STAT: entrada High-Z com pull-up (padrao do fone em bsp_audio.c) */
        esp_io_expander_set_dir(exp1, BSP_IOX_B_CHG_STAT, IO_EXPANDER_INPUT);
        esp_io_expander_set_output_mode(exp1, BSP_IOX_B_CHG_STAT, IO_EXPANDER_OUTPUT_MODE_OPEN_DRAIN);
        esp_io_expander_set_pullupdown(exp1, BSP_IOX_B_CHG_STAT, IO_EXPANDER_PULL_UP);
        /* CHG_EN: saida push-pull em alto = carregador habilitado */
        esp_io_expander_set_dir(exp1, BSP_IOX_B_CHG_EN, IO_EXPANDER_OUTPUT);
        esp_io_expander_set_output_mode(exp1, BSP_IOX_B_CHG_EN, IO_EXPANDER_OUTPUT_MODE_PUSH_PULL);
        esp_io_expander_set_level(exp1, BSP_IOX_B_CHG_EN, 1);
        s_configured = true;
        ESP_LOGI(TAG, "carregador habilitado (CHG_EN=1)");
    }
    return true;
}

/* Liga/desliga o carregador em tempo real (CHG_EN) */
static void set_chg_en(bool enabled)
{
    esp_io_expander_handle_t exp1 = bsp_io_expander1_init();
    if (exp1 == NULL) {
        return;
    }
    esp_io_expander_set_level(exp1, BSP_IOX_B_CHG_EN, enabled ? 1 : 0);
}

/* Le o nivel cru do CHG_STAT: 0/1, ou -1 se expander indisponivel */
static int read_chg_stat_raw(void)
{
    if (!chg_stat_configure()) {
        return -1;
    }
    uint32_t lvl = 0;
    if (esp_io_expander_get_level(bsp_io_expander1_init(), BSP_IOX_B_CHG_STAT, &lvl) != ESP_OK) {
        return -1;
    }
    return (lvl & BSP_IOX_B_CHG_STAT) ? 1 : 0;
}

static int estimate_percent(int32_t voltage_mv)
{
    int pct = (voltage_mv - VOLT_EMPTY_MV) * 100 / (VOLT_FULL_MV - VOLT_EMPTY_MV);
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    return pct;
}

static int clamp_vote(int vote)
{
    if (vote > VOTE_CLAMP) {
        return VOTE_CLAMP;
    }
    if (vote < -VOTE_CLAMP) {
        return -VOTE_CLAMP;
    }
    return vote;
}

/* Roda dentro da task LVGL (lv_timer_handler): I2C a 1 Hz e seguro sem lock */
static void sample_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    uint16_t raw_bus = 0;
    uint16_t raw_current = 0;
    esp_err_t err = ina_read16(INA226_REG_BUS, &raw_bus);
    if (err == ESP_OK) {
        err = ina_read16(INA226_REG_CURRENT, &raw_current);
    }
    if (err != ESP_OK) {
        if (++s_error_count >= MAX_CONSECUTIVE_ERRORS && s_status.available) {
            s_status.available = false;
            ESP_LOGW(TAG, "INA226 indisponivel (%s)", esp_err_to_name(err));
        }
        return;
    }
    s_error_count = 0;

    s_status.voltage_mv = (int32_t)raw_bus * 5 / 4; /* 1.25 mV/LSB exato */
    s_status.current_ma = ((int32_t)(int16_t)raw_current * INA226_CURRENT_UA_LSB) / 1000;
    int chg_raw = read_chg_stat_raw();
    /* Corrente forte negativa = carregando (sinal do shunt, inequivoco);
     * CHG_STAT segue em observacao para confirmar polaridade */
    bool charging = s_status.current_ma < -EXT_CURRENT_LIMIT_MA;

    if (charging) {
        s_status.source = BATTERY_SOURCE_CHARGING;
        s_ext_votes = 0;
        s_nobat_votes = 0;
    } else if (s_status.current_ma > EXT_CURRENT_LIMIT_MA) {
        /* Descarga forte: so acontece alimentado pela bateria */
        s_status.source = BATTERY_SOURCE_BATTERY;
        s_ext_votes = 0;
        s_nobat_votes = 0;
    } else {
        /* Corrente ~0: decide por tensao com votacao incremental
         * (glitch de leitura custa -1, nao zera a serie) */
        bool cable_ok = s_status.voltage_mv >= EXT_VOLTAGE_MIN_MV;
        bool nobat_ok = cable_ok && s_status.voltage_mv >= NO_BATTERY_VOLTAGE_MV;
        s_ext_votes = clamp_vote(s_ext_votes + (cable_ok ? 1 : -1));
        s_nobat_votes = clamp_vote(s_nobat_votes + (nobat_ok ? 1 : -1));

        if (s_ext_votes >= EXT_STABLE_SAMPLES) {
            s_status.source =
                (s_nobat_votes >= EXT_STABLE_SAMPLES) ? BATTERY_SOURCE_NO_BATTERY : BATTERY_SOURCE_EXTERNAL;
        } else if (s_ext_votes <= -EXT_STABLE_SAMPLES) {
            s_status.source = BATTERY_SOURCE_BATTERY;
        }
    }

    /* Semeia o percentual apenas com tensao plausivel de bateria */
    if (s_percent_f < 0.0f && s_status.source != BATTERY_SOURCE_NO_BATTERY && s_status.voltage_mv >= VOLT_EMPTY_MV &&
        s_status.voltage_mv <= VOLT_FULL_MV) {
        s_percent_f = (float)estimate_percent(s_status.voltage_mv);
    }
    if (s_percent_f >= 0.0f) {
        /* Corrente negativa (carregando) eleva o nivel; dt = 1 s */
        s_percent_f -= (float)s_status.current_ma / PERCENT_STEP_DIVISOR;
        if (s_percent_f > 100.0f) {
            s_percent_f = 100.0f;
        }
        if (s_percent_f < 0.0f) {
            s_percent_f = 0.0f;
        }
        s_status.percent = lroundf(s_percent_f);
    }

    /* Protecao: corta a carga em 90% (consome so do cabo) e retoma em 85% */
    if (s_percent_f >= 0.0f && s_status.source != BATTERY_SOURCE_NO_BATTERY) {
        if (s_protect_enabled && !s_protect_active && s_status.source == BATTERY_SOURCE_CHARGING &&
            s_percent_f >= CHG_PROTECT_LIMIT_PCT && s_status.voltage_mv >= CHG_PROTECT_MIN_VOLT_MV) {
            s_protect_active = true;
            set_chg_en(false);
            ESP_LOGI(TAG, "protecao: carga cortada em %d%% (%ld mV)", s_status.percent, (long)s_status.voltage_mv);
        } else if (s_protect_active && (!s_protect_enabled || s_percent_f <= CHG_PROTECT_RESUME_PCT)) {
            s_protect_active = false;
            set_chg_en(true);
            ESP_LOGI(TAG, "protecao: carregamento retomado");
        }
    }

    s_status.protect_active = s_protect_active;
    s_status.available = true;

    if ((++s_sample_count % LOG_DIVIDER) == 0) {
        ESP_LOGI(TAG, "V=%ldmV I=%ldmA chg_stat=%d fonte=%d pct=%d", (long)s_status.voltage_mv,
                 (long)s_status.current_ma, chg_raw, (int)s_status.source, s_status.percent);
    }
}

esp_err_t battery_reader_start(void)
{
    if (s_sample_timer != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    if (bus == NULL) {
        ESP_LOGW(TAG, "I2C do BSP indisponivel");
        return ESP_FAIL;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = INA226_I2C_ADDRESS,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,
        .flags = {},
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &dev_cfg, &s_ina);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao adicionar INA226: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t wr[3];
    wr[0] = INA226_REG_CONFIG;
    wr[1] = (uint8_t)(INA226_CONFIG_VALUE >> 8);
    wr[2] = (uint8_t)(INA226_CONFIG_VALUE & 0xFF);
    err = i2c_master_transmit(s_ina, wr, sizeof(wr), 100);
    if (err == ESP_OK) {
        wr[0] = INA226_REG_CAL;
        wr[1] = (uint8_t)(INA226_CAL_VALUE >> 8);
        wr[2] = (uint8_t)(INA226_CAL_VALUE & 0xFF);
        err = i2c_master_transmit(s_ina, wr, sizeof(wr), 100);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "INA226 nao respondeu: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(s_ina);
        s_ina = NULL;
        return err;
    }

    s_protect_enabled = true;
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t val = 1;
        if (nvs_get_u8(nvs, NVS_KEY_CHG_PROTECT, &val) == ESP_OK) {
            s_protect_enabled = (val != 0);
        }
        nvs_close(nvs);
    }
    ESP_LOGI(TAG, "protecao de carregamento (corte 90%%): %s", s_protect_enabled ? "ligada" : "desligada");

    s_sample_timer = lv_timer_create(sample_timer_cb, SAMPLE_PERIOD_MS, NULL);
    ESP_LOGI(TAG, "INA226 iniciado (poll %d ms)", SAMPLE_PERIOD_MS);
    return ESP_OK;
}

bool battery_reader_get_status(battery_status_t *out)
{
    if (out == NULL) {
        return false;
    }
    *out = s_status;
    return s_status.available;
}

void battery_reader_set_protection(bool enabled)
{
    if (s_protect_enabled == enabled) {
        return;
    }
    s_protect_enabled = enabled;

    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, NVS_KEY_CHG_PROTECT, enabled ? 1 : 0);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    /* Reage imediatamente: religar o carregador solta o corte;
     * cortar acontece no proximo ciclo de amostragem */
    if (!enabled && s_protect_active) {
        s_protect_active = false;
        set_chg_en(true);
        ESP_LOGI(TAG, "protecao desligada: carregador reativado");
    } else {
        ESP_LOGI(TAG, "protecao de carregamento %s", enabled ? "ligada" : "desligada");
    }
}

bool battery_reader_get_protection(void)
{
    return s_protect_enabled;
}
