#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CALENDAR_MONTHS_COUNT 12
#define CALENDAR_DAYS_PER_WEEK 7

/**
 * @brief Verifica se um determinado ano é bissexto no calendário Gregoriano.
 */
bool calendar_logic_is_leap_year(int year);

/**
 * @brief Retorna o total de dias em um mês de um determinado ano.
 * @param year Ano (ex: 2026).
 * @param month Mês de 1 a 12.
 * @return Quantidade de dias (28-31) ou 0 se mês inválido.
 */
int calendar_logic_get_days_in_month(int year, int month);

/**
 * @brief Retorna o dia da semana do 1º dia do mês (0 = Domingo, 1 = Segunda, ..., 6 = Sábado).
 * @param year Ano (ex: 2026).
 * @param month Mês de 1 a 12.
 * @return Dia da semana de 0 a 6 ou -1 se mês inválido.
 */
int calendar_logic_get_first_day_of_week(int year, int month);

/**
 * @brief Navega para o mês anterior, atualizando ano e mês (trata virada de Janeiro para Dezembro).
 */
void calendar_logic_prev_month(int *year, int *month);

/**
 * @brief Navega para o próximo mês, atualizando ano e mês (trata virada de Dezembro para Janeiro).
 */
void calendar_logic_next_month(int *year, int *month);

/**
 * @brief Retorna o nome do mês em português (ex: "Janeiro", "Fevereiro").
 * @param month Mês de 1 a 12.
 * @return Nome do mês ou string vazia se inválido.
 */
const char *calendar_logic_get_month_name(int month);

/**
 * @brief Retorna o nome abreviado do dia da semana em português (ex: "Dom", "Seg").
 * @param weekday Dia da semana de 0 (Domingo) a 6 (Sábado).
 * @return Nome abreviado ou string vazia se inválido.
 */
const char *calendar_logic_get_weekday_name(int weekday);

/**
 * @brief Obtém o ano, mês e dia da data atual do sistema via timezone_mgr.
 * @param out_year Ponteiro para receber o ano (ex: 2026).
 * @param out_month Ponteiro para receber o mês (1 a 12).
 * @param out_day Ponteiro para receber o dia do mês (1 a 31).
 */
void calendar_logic_get_current_date(int *out_year, int *out_month, int *out_day);

#ifdef __cplusplus
}
#endif
