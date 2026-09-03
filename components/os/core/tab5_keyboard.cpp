#include "tab5_keyboard.h"
#include "tab5_keyboard_keys.h"
#include "ui_keyboard.h"

#include <string.h>
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG = "tab5_kb";

/* ------------------------------------------------------------------ */
/* Pinos da Ext.Port1 do Tab5 onde o teclado se conecta                */
/* ------------------------------------------------------------------ */
#define KB_SDA_GPIO (GPIO_NUM_0)
#define KB_SCL_GPIO (GPIO_NUM_1)
#define KB_INT_GPIO (GPIO_NUM_50)
#define KB_I2C_PORT (I2C_NUM_0) /* barramento separado do BSP (bus 1) */
#define KB_I2C_ADDR (0x6D)
#define KB_I2C_SPEED_HZ (400000)

/* ------------------------------------------------------------------ */
/* Registradores do protocolo I2C (modo Character)                     */
/* ------------------------------------------------------------------ */
#define REG_INT_CFG 0x00
#define REG_INT_STAT 0x01
#define REG_EVENT_NUM 0x02
#define REG_BRIGHTNESS 0x03
#define REG_KEYBOARD_MODE 0x10
#define REG_RGB_MODE 0x11
#define REG_CHAR_EVENT_LEN 0x40
#define REG_CHAR_EVENT_BASE 0x50
#define REG_VERSION 0xFE
#define REG_I2C_ADDR 0xFF

#define KB_MODE_CHARACTER 0x02

/* RGB em modo Bind (0) = comportamento natural do firmware: LED esquerdo
 * indica Caps Lock, LED direito indica o modo do teclado (roxo em Character).
 * Brilho global default do firmware. */
#define RGB_MODE_BIND 0x00
#define KB_BRIGHTNESS_DEFAULT 10

/* INT_CFG bit 2 = Character mode */
#define INT_CFG_CHAR_BIT (1 << 2)

#define CHAR_EVENT_MAX 16 /* 1 modificador + ate 9 chars, folga p/ seguranca */
#define DETECT_INTERVAL_MS 2000
#define TASK_STACK_SIZE 4096
#define TASK_PRIORITY 5

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static TaskHandle_t s_task = NULL;
static QueueHandle_t s_intr_queue = NULL;
static bool s_connected = false;
static bool s_initialized = false;
static bool s_gpio_isr_ready = false;
static SemaphoreHandle_t s_i2c_mutex = NULL;

static esp_err_t kb_configure(void);
static bool detect_change(void);

static esp_err_t kb_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
    xSemaphoreGive(s_i2c_mutex);
    return err;
}

static esp_err_t kb_read_reg(uint8_t reg, uint8_t *out)
{
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, out, 1, 100);
    xSemaphoreGive(s_i2c_mutex);
    return err;
}

static esp_err_t kb_read_bytes(uint8_t reg, uint8_t *out, size_t len)
{
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, out, len, 100);
    xSemaphoreGive(s_i2c_mutex);
    return err;
}

/* Empacota um caractere (ou tecla especial) para o LVGL, sob lock de UI. */
static void emit_event(const char *str, uint8_t str_len, uint8_t modifier)
{
    if (str_len == 0) {
        return;
    }

    /* Cria uma string NUL-terminada a partir dos bytes recebidos. */
    char buf[CHAR_EVENT_MAX + 1];
    size_t copy = str_len < CHAR_EVENT_MAX ? str_len : CHAR_EVENT_MAX;
    memcpy(buf, str, copy);
    buf[copy] = '\0';

    const tab5_key_entry_t *entry = tab5_keymap_lookup(buf);
    if (entry == nullptr) {
        /* String nao reconhecida: tenta injetar o primeiro byte como char
         * (cobre chars multibyte nao listados, ex. acentos do layout). */
        if (copy == 1 && buf[0] != 0 && (uint8_t)buf[0] >= 0x20 && (uint8_t)buf[0] != 0x7F) {
            ui_keyboard_inject_char(buf[0]);
        } else {
            ESP_LOGW(TAG, "Evento nao mapeado: len=%u '%.*s'", str_len, (int)copy, buf);
        }
        return;
    }

    switch (entry->type) {
    case TAB5_KEY_CHAR:
        if (modifier != 0) {
            /* Com modificador (Ctrl/Alt): entrega como LV_EVENT_KEY com o
             * codigo do char para que apps/widger respondam a atalhos. */
            ui_keyboard_inject_key_ex((uint8_t)entry->ch, modifier);
        } else {
            ui_keyboard_inject_char(entry->ch);
        }
        break;
    case TAB5_KEY_SPECIAL:
        ui_keyboard_inject_key_ex(entry->lvgl_key, modifier);
        break;
    case TAB5_KEY_MODIFIER:
    case TAB5_KEY_IGNORE:
    default:
        break;
    }
}

/* Esvazia a fila de eventos do teclado (modo Character). */
static void drain_events(void)
{
    uint8_t count = 0;
    if (kb_read_reg(REG_EVENT_NUM, &count) != ESP_OK) {
        return;
    }

    for (uint8_t i = 0; i < count && i < 32; i++) {
        uint8_t len = 0;
        if (kb_read_reg(REG_CHAR_EVENT_LEN, &len) != ESP_OK || len == 0) {
            break;
        }
        if (len > CHAR_EVENT_MAX) {
            len = CHAR_EVENT_MAX;
        }

        /* Byte 0 = modificador, bytes 1..len = string UTF-8.
         * O firmware reporta o comprimento total (modificador incluso);
         * a biblioteca oficial le len+1 bytes de 0x50. */
        uint8_t buf[CHAR_EVENT_MAX + 1];
        if (kb_read_bytes(REG_CHAR_EVENT_BASE, buf, len + 1) != ESP_OK) {
            break;
        }

        uint8_t modifier = buf[0];
        emit_event((const char *)&buf[1], len, modifier);
    }

    /* Libera o pino de interrupcao apos drenar. */
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    uint8_t stat[2] = {REG_INT_STAT, 0x00};
    i2c_master_transmit(s_dev, stat, sizeof(stat), 100);
    xSemaphoreGive(s_i2c_mutex);
}

static void kb_task(void *arg)
{
    (void)arg;
    uint32_t last_detect = 0;
    for (;;) {
        uint32_t dummy = 0;
        TickType_t elapsed = xTaskGetTickCount() - last_detect;
        TickType_t wait = pdMS_TO_TICKS(DETECT_INTERVAL_MS) > elapsed ? pdMS_TO_TICKS(DETECT_INTERVAL_MS) - elapsed : 0;

        if (xQueueReceive(s_intr_queue, &dummy, wait) == pdTRUE) {
            /* Coalesce rajadas de IRQ (o teclado acumula na propria fila). */
            while (xQueueReceive(s_intr_queue, &dummy, 0) == pdTRUE) {
            }
            if (s_connected) {
                drain_events();
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        /* Deteccao periodica de conexao/desconexao (hot-plug por probe). */
        if (xTaskGetTickCount() - last_detect >= pdMS_TO_TICKS(DETECT_INTERVAL_MS)) {
            last_detect = xTaskGetTickCount();
            detect_change();
        }
    }
}

static void IRAM_ATTR kb_isr(void *arg)
{
    (void)arg;
    BaseType_t higher = pdFALSE;
    uint32_t val = 1;
    xQueueSendFromISR(s_intr_queue, &val, &higher);
    if (higher == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/* Reconfigura o teclado para o modo Character e habilita a interrupcao. */
static esp_err_t kb_configure(void)
{
    esp_err_t err;

    /* Modo Character. A troca de modo limpa a fila e libera o INT. */
    err = kb_write_reg(REG_KEYBOARD_MODE, KB_MODE_CHARACTER);
    if (err != ESP_OK) {
        return err;
    }

    /* Habilitar interrupcao para o modo Character (bit 2). */
    err = kb_write_reg(REG_INT_CFG, INT_CFG_CHAR_BIT);
    if (err != ESP_OK) {
        return err;
    }

    /* LIMPA estado pendente apos a troca de modo (a troca ja limpa, mas
     * garantimos uma escrita em INT_STAT para liberar o pino se ficar preso). */
    xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
    uint8_t stat[2] = {REG_INT_STAT, 0x00};
    i2c_master_transmit(s_dev, stat, sizeof(stat), 100);

    /* Restaura o estado natural dos LEDs: modo Bind (0) + brilho default.
     * Em Bind, o firmware controla os indicadores: LED esquerdo = Caps Lock
     * (azul quando Aa ativo), LED direito = cor do modo (roxo em Character). */
    uint8_t rgb[2] = {REG_RGB_MODE, RGB_MODE_BIND};
    i2c_master_transmit(s_dev, rgb, sizeof(rgb), 100);
    uint8_t bri[2] = {REG_BRIGHTNESS, KB_BRIGHTNESS_DEFAULT};
    i2c_master_transmit(s_dev, bri, sizeof(bri), 100);
    xSemaphoreGive(s_i2c_mutex);

    return ESP_OK;
}

/* Verifica periodicamente se o teclado esta presente. Retorna true se houver
 * mudanca (conectou ou desconectou). */
static bool detect_change(void)
{
    bool present = false;
    if (s_bus != NULL) {
        xSemaphoreTake(s_i2c_mutex, portMAX_DELAY);
        present = (i2c_master_probe(s_bus, KB_I2C_ADDR, 100) == ESP_OK);
        xSemaphoreGive(s_i2c_mutex);
    }

    if (present != s_connected) {
        s_connected = present;
        ESP_LOGI(TAG, "Teclado fisico %s", present ? "CONECTADO" : "desconectado");
        if (present) {
            kb_configure();
        }
        /* Notifica a UI para mostrar/esconder o teclado virtual. */
        ui_keyboard_notify_hardware_change();
        return true;
    }
    return false;
}

esp_err_t tab5_keyboard_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err;

    s_i2c_mutex = xSemaphoreCreateMutex();
    if (s_i2c_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Cria o barramento I2C 0 dedicado a Ext.Port1 (nao usa o do BSP). */
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port = KB_I2C_PORT;
    bus_cfg.sda_io_num = KB_SDA_GPIO;
    bus_cfg.scl_io_num = KB_SCL_GPIO;
    bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_cfg.glitch_ignore_cnt = 7;
    bus_cfg.flags.enable_internal_pullup = true;
    err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao criar barramento I2C 0: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = KB_I2C_ADDR;
    dev_cfg.scl_speed_hz = KB_I2C_SPEED_HZ;
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Falha ao adicionar device I2C: %s", esp_err_to_name(err));
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
        return err;
    }

    /* Verifica presenca do teclado agora. */
    bool present = (i2c_master_probe(s_bus, KB_I2C_ADDR, 100) == ESP_OK);
    s_connected = present;
    ESP_LOGI(TAG, "Teclado %s no boot", present ? "detectado" : "nao encontrado");
    if (present) {
        kb_configure();
    }

    /* Interrupcao de hardware no GPIO50 (ativo-baixo). */
    s_intr_queue = xQueueCreate(4, sizeof(uint32_t));
    if (s_intr_queue == NULL) {
        i2c_master_bus_rm_device(s_dev);
        i2c_del_master_bus(s_bus);
        s_dev = NULL;
        s_bus = NULL;
        return ESP_ERR_NO_MEM;
    }

    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << KB_INT_GPIO;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type = GPIO_INTR_NEGEDGE;
    gpio_config(&io);

    if (!s_gpio_isr_ready) {
        esp_err_t isr = gpio_install_isr_service(0);
        if (isr != ESP_OK && isr != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Falha ao instalar ISR: %s", esp_err_to_name(isr));
        } else {
            s_gpio_isr_ready = true;
        }
    }
    if (s_gpio_isr_ready) {
        gpio_isr_handler_add(KB_INT_GPIO, kb_isr, NULL);
    }

    xTaskCreate(kb_task, "tab5_kb", TASK_STACK_SIZE, NULL, TASK_PRIORITY, &s_task);

    s_initialized = true;
    ESP_LOGI(TAG, "Driver do teclado fisico inicializado");
    return ESP_OK;
}

esp_err_t tab5_keyboard_deinit(void)
{
    if (s_task != NULL) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_intr_queue != NULL) {
        vQueueDelete(s_intr_queue);
        s_intr_queue = NULL;
    }
    if (s_gpio_isr_ready) {
        gpio_isr_handler_remove(KB_INT_GPIO);
    }
    if (s_dev != NULL) {
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
    }
    if (s_bus != NULL) {
        i2c_del_master_bus(s_bus);
        s_bus = NULL;
    }
    if (s_i2c_mutex != NULL) {
        vSemaphoreDelete(s_i2c_mutex);
        s_i2c_mutex = NULL;
    }
    s_connected = false;
    s_initialized = false;
    return ESP_OK;
}

bool tab5_keyboard_is_connected(void)
{
    return s_connected;
}
