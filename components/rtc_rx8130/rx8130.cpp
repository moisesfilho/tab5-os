/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "rx8130.h"

#include <string.h>

#include <freertos/FreeRTOS.h>

#include "esp_log.h"

static const char *TAG = "rtc_rx8130";

// Registradores do RX-8130
#define RX8130_REG_SEC   0x10
#define RX8130_REG_MIN   0x11
#define RX8130_REG_HOUR  0x12
#define RX8130_REG_WDAY  0x13
#define RX8130_REG_MDAY  0x14
#define RX8130_REG_MONTH 0x15
#define RX8130_REG_YEAR  0x16

#define RX8130_REG_FLAG  0x1D
#define RX8130_REG_CTRL1 0x1F

// Flag Register (1Dh) bit positions
#define RX8130_BIT_FLAG_VLF (1 << 1)

#define setbit(x, y) x |= (0x01 << y)

static uint8_t bcd2dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0f);
}

bool RX8130_Class::begin(i2c_master_bus_handle_t busHandle, uint8_t addr)
{
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = addr;
    dev_cfg.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(busHandle, &dev_cfg, &_i2c_device_handle));

    if (_i2c_device_handle == NULL) {
        return false;
    }

    return true;
}

void RX8130_Class::initBat()
{
    auto data = readRegister8(RX8130_REG_CTRL1);
    setbit(data, 4);
    setbit(data, 5);
    writeRegister8(RX8130_REG_CTRL1, data);
    data = readRegister8(RX8130_REG_CTRL1);
    ESP_LOGD(TAG, "rtc bat init: 0x1F: %02X", data);
}

void RX8130_Class::getTime(struct tm* time)
{
    uint8_t date[7];
    readRegister(RX8130_REG_SEC, date, 7);

    time->tm_sec  = bcd2dec(date[RX8130_REG_SEC - 0x10] & 0x7f);
    time->tm_min  = bcd2dec(date[RX8130_REG_MIN - 0x10] & 0x7f);
    time->tm_hour = bcd2dec(date[RX8130_REG_HOUR - 0x10] & 0x3f);  // somente relógio 24h
    time->tm_mday = bcd2dec(date[RX8130_REG_MDAY - 0x10] & 0x3f);
    time->tm_mon  = bcd2dec(date[RX8130_REG_MONTH - 0x10] & 0x1f);  // 1-based (1-12)
    time->tm_year = bcd2dec(date[RX8130_REG_YEAR - 0x10]);
    time->tm_wday = bcd2dec(date[RX8130_REG_WDAY - 0x10] & 0x7f);

    time->tm_year += 100;  // anos desde 1900 (convenção do C)
}

bool RX8130_Class::getLowVoltageFlag()
{
    return (readRegister8(RX8130_REG_FLAG) & RX8130_BIT_FLAG_VLF) != 0;
}

uint8_t RX8130_Class::readRegister8(uint8_t reg)
{
    uint8_t value;
    readRegister(reg, &value, 1);
    return value;
}

void RX8130_Class::writeRegister8(uint8_t reg, uint8_t value)
{
    uint8_t buf[1] = {value};
    writeRegister(reg, buf, 1);
}

void RX8130_Class::readRegister(uint8_t reg, uint8_t* buf, uint8_t len)
{
    uint8_t w_buffer[1] = {0};
    w_buffer[0]         = reg;
    i2c_master_transmit_receive(_i2c_device_handle, w_buffer, 1, buf, len, portMAX_DELAY);
}

void RX8130_Class::writeRegister(uint8_t reg, uint8_t* buf, uint8_t len)
{
    uint8_t w_buffer[1 + len];
    w_buffer[0] = reg;
    memcpy(w_buffer + 1, buf, len);
    i2c_master_transmit(_i2c_device_handle, w_buffer, 1 + len, portMAX_DELAY);
}