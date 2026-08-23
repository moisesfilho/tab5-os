#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BATTERY_SOURCE_BATTERY = 0,    /* alimentado pela bateria */
    BATTERY_SOURCE_EXTERNAL = 1,   /* na tomada, sem carregar (carregada) */
    BATTERY_SOURCE_CHARGING = 2,   /* carregando pela fonte externa */
    BATTERY_SOURCE_NO_BATTERY = 3, /* apenas cabo, sem bateria conectada */
} battery_source_t;

typedef struct {
    bool available;
    int percent; /* 0-100 (coulomb counting) */
    battery_source_t source;
    int32_t voltage_mv;  /* tensao VSYS medida pelo INA226 */
    int32_t current_ma;  /* positivo = descarregando, negativo = carregando */
    bool protect_active; /* corte de carga em execucao (protecao habilitada) */
} battery_status_t;

esp_err_t battery_reader_start(void);

bool battery_reader_get_status(battery_status_t *out);

/* Protecao de carregamento: interrompe a carga em 90% mesmo na tomada
 * (o aparelho passa a consumir apenas energia do cabo). */
void battery_reader_set_protection(bool enabled);

bool battery_reader_get_protection(void);

#ifdef __cplusplus
}
#endif
