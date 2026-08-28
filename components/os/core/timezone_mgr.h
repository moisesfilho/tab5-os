#pragma once

#include "esp_err.h"
#include <ctime>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

#define TIMEZONE_DEFAULT_OFFSET (-3)
#define TIMEZONE_MIN_OFFSET (-12)
#define TIMEZONE_MAX_OFFSET (14)
#define TIMEZONE_CFG_PATH "/sdcard/.tab5_os/timezone.cfg"

/**
 * @brief Inicializa o subsistema de fuso horário restaurando do NVS/SD.
 * Aplica a variável de ambiente TZ e invoca tzset().
 */
esp_err_t timezone_mgr_init(void);

/**
 * @brief Obtém o offset do fuso horário atual em horas (ex: -3 para UTC-3).
 */
int timezone_mgr_get_offset(void);

/**
 * @brief Define e persiste o novo fuso horário, atualizando o ambiente POSIX.
 * @param offset_hours Valor de -12 a +14.
 */
esp_err_t timezone_mgr_set_offset(int offset_hours);

/**
 * @brief Obtém a data/hora local atual já calculada conforme o fuso horário ativo.
 * @param out_tm Ponteiro para struct tm a ser preenchida.
 * @return Ponteiro para out_tm ou nullptr em caso de erro.
 */
struct tm *timezone_mgr_get_localtime(struct tm *out_tm);

/**
 * @brief Formata o offset em string amigável (ex: "-3" ou "UTC-3" / "+5").
 */
void timezone_mgr_format_offset(int offset_hours, char *buf, size_t buf_size);

#ifdef __cplusplus
}
#endif
