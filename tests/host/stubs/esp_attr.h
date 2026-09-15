#pragma once

/* Stub de esp_attr.h para o build host: sem atributos de memória ESP32.
 * O contrato "stack da task do worker em SRAM interna" é verificado
 * estaticamente por tests/test_nvs_worker_contracts.py (exige a presença de
 * DRAM_ATTR no fonte de produção); aqui a macro é neutra para o compilador. */
#define DRAM_ATTR
#define IRAM_ATTR
