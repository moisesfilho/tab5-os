#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
#include <vector>
#include <string>
extern "C" {
#endif

typedef void (*app_launch_cb_t)(void);
typedef void (*app_open_file_cb_t)(const char *filepath);
typedef void (*app_icon_builder_cb_t)(lv_obj_t *icon_box);
typedef void (*app_icon_theme_cb_t)(lv_obj_t *icon_box);

typedef struct {
    const char *id;                         /**< ID único da aplicação (ex: "notas", "gallery") */
    const char *name;                       /**< Nome exibido na área de trabalho (ex: "Notas", "Galeria") */
    const char *icon_symbol;                /**< Símbolo LVGL ou texto curto (ex: LV_SYMBOL_EDIT, ">_") */
    app_icon_builder_cb_t icon_builder;     /**< Callback opcional para desenhar ícones customizados */
    app_icon_theme_cb_t icon_theme_refresh; /**< Callback opcional para atualizar tema de ícone customizado */
    app_launch_cb_t on_launch;              /**< Callback para abrir a aplicação a partir da área de trabalho */
    const char *const *file_extensions;     /**< Lista de extensões suportadas terminada em NULL */
    app_open_file_cb_t on_open_file;        /**< Callback para abrir arquivos suportados */
} app_desc_t;

/**
 * @brief Inicializa o registro de aplicações do sistema.
 */
void app_registry_init(void);

/**
 * @brief Registra uma nova aplicação no sistema operacional.
 *
 * Automaticamente registra suas extensões de arquivo no subsistema file_assoc.
 *
 * @param desc Ponteiro para o descritor da aplicação.
 * @return ESP_OK em caso de sucesso.
 */
esp_err_t app_registry_register(const app_desc_t *desc);

/**
 * @brief Retorna o total de aplicações registradas.
 */
int app_registry_get_count(void);

/**
 * @brief Retorna o descritor da aplicação pelo índice.
 */
const app_desc_t *app_registry_get_by_index(int index);

/**
 * @brief Busca uma aplicação pelo ID único.
 */
const app_desc_t *app_registry_find_by_id(const char *id);

/**
 * @brief Remove uma aplicação do registro e desassocia suas extensões.
 * @param id ID único da aplicação.
 * @return ESP_OK se removido, ESP_ERR_NOT_FOUND se não encontrado.
 */
esp_err_t app_registry_unregister(const char *id);

#ifdef __cplusplus
}

/**
 * @brief Retorna a lista de todas as aplicações registradas (API C++).
 */
const std::vector<app_desc_t> &app_registry_get_all(void);

#endif
