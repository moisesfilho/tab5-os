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
    const char *icon_bg_color;              /**< Cor de fundo do ícone em hex (ex: "#2196F3"); NULL = paleta do tema */
    app_icon_builder_cb_t icon_builder;     /**< Callback opcional para desenhar ícones customizados */
    app_icon_theme_cb_t icon_theme_refresh; /**< Callback opcional para atualizar tema de ícone customizado */
    app_launch_cb_t on_launch;              /**< Callback para abrir a aplicação a partir da área de trabalho */
    const char *const *file_extensions;     /**< Lista de extensões suportadas terminada em NULL */
    app_open_file_cb_t on_open_file;        /**< Callback para abrir arquivos suportados */
} app_desc_t;

#ifdef __cplusplus
}
#endif

/*
 * A C++ snapshot owns every character buffer referenced by desc.  It is
 * intentionally a different type from app_desc_t: a borrowed registration
 * descriptor must never escape the registry, while this value may outlive an
 * unregister operation.
 */
#ifdef __cplusplus
struct app_desc_snapshot_t {
    app_desc_t desc{};
    /* Convenience view with the same field names as app_desc_t. These point
     * only into the owned storage below and are rebound after every move. */
    const char *id = nullptr;
    const char *name = nullptr;
    const char *icon_symbol = nullptr;
    const char *icon_bg_color = nullptr;
    app_icon_builder_cb_t icon_builder = nullptr;
    app_icon_theme_cb_t icon_theme_refresh = nullptr;
    app_launch_cb_t on_launch = nullptr;
    const char *const *file_extensions = nullptr;
    app_open_file_cb_t on_open_file = nullptr;
    std::string owned_id;
    std::string owned_name;
    std::string owned_icon_symbol;
    std::string owned_icon_bg_color;
    std::vector<std::string> extensions;
    std::vector<const char *> extension_ptrs;

    app_desc_snapshot_t() = default;
    explicit app_desc_snapshot_t(const app_desc_t &source);
    app_desc_snapshot_t(const app_desc_snapshot_t &other);
    app_desc_snapshot_t &operator=(const app_desc_snapshot_t &other);
    app_desc_snapshot_t(app_desc_snapshot_t &&other) noexcept;
    app_desc_snapshot_t &operator=(app_desc_snapshot_t &&other) noexcept;

  private:
    void rebind();
};
#endif

#ifdef __cplusplus
extern "C" {
#endif

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
 * @brief Copia o descritor da aplicação pelo índice.
 *
 * O resultado é um snapshot. O chamador não recebe uma referência ao vetor
 * interno e pode usá-lo depois de qualquer outra operação no registry.
 * @return ESP_OK, ESP_ERR_NOT_FOUND para índice inválido.
 */
esp_err_t app_registry_get_by_index(int index, app_desc_t *out);

/**
 * @brief Libera os buffers alocados por app_registry_get_by_index/find_by_id.
 *
 * Os callbacks são apenas copiados; somente as strings/lista de extensões são
 * liberadas. Passar NULL é seguro.
 */
void app_registry_release_snapshot(app_desc_t *snapshot);

/**
 * @brief Copia uma aplicação encontrada pelo ID único.
 * @return ESP_OK, ESP_ERR_NOT_FOUND quando o ID não existe.
 */
esp_err_t app_registry_find_by_id(const char *id, app_desc_t *out);

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
std::vector<app_desc_snapshot_t> app_registry_get_all(void);

#endif
