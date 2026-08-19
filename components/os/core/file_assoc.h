#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*file_open_handler_t)(const char *filepath);

/**
 * @brief Inicializa o registro de associacoes de arquivos com os apps padrao do SO.
 */
void file_assoc_init(void);

/**
 * @brief Registra uma associacao de extensao de arquivo com um handler de abertura.
 *
 * @param ext Extensao do arquivo com ou sem ponto (ex: ".txt" ou "txt").
 * @param handler Funcao callback que realiza a abertura do app correspondente.
 * @return ESP_OK se registrado com sucesso.
 */
esp_err_t file_assoc_register(const char *ext, file_open_handler_t handler);

/**
 * @brief Abre o arquivo com o aplicativo registrado para a sua extensao.
 *
 * @param filepath Caminho absoluto do arquivo (ex: "/sdcard/notas/nota_01.txt").
 * @return ESP_OK se houver handler registrado e executado, ESP_ERR_NOT_FOUND caso contrario.
 */
esp_err_t file_assoc_open(const char *filepath);

#ifdef __cplusplus
}
#endif
