/**
 * @file tab5_lifecycle_host.cpp
 * @brief Implementação do Gerenciador de Ciclo de Vida no Host
 */

#include "tab5_lifecycle_host.h"
#include "tab5_ui_host.h"
#include "tab5_storage_sandbox.h"
#include <cstring>

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "tab5_lifecycle";
#define LOG_I(fmt, ...) ESP_LOGI(TAG, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) ESP_LOGW(TAG, fmt, ##__VA_ARGS__)
#else
#include <cstdio>
#define LOG_I(fmt, ...)
#define LOG_W(fmt, ...)
#endif

tab5_err_t tab5_lifecycle_host_init_app(tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    if (ctx->state != TAB5_APP_STATE_UNINITIALIZED) {
        LOG_W("App %s ja inicializada (estado=%d)", ctx->app_id, (int)ctx->state);
        return TAB5_ERR_INVALID_STATE;
    }

    tab5_host_set_active_app(ctx);

    // Cria tela UI
    tab5_ui_host_create_app_screen(ctx->app_name[0] != '\0' ? ctx->app_name : ctx->app_id, ctx);

    ctx->state = TAB5_APP_STATE_INITIALIZED;

    if (ctx->lifecycle.on_init) {
        ctx->lifecycle.on_init();
    }

    LOG_I("App %s inicializada com sucesso", ctx->app_id);
    return TAB5_OK;
}

tab5_err_t tab5_lifecycle_host_resume_app(tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    if (ctx->state == TAB5_APP_STATE_UNINITIALIZED || ctx->state == TAB5_APP_STATE_DESTROYED) {
        return TAB5_ERR_INVALID_STATE;
    }

    ctx->state = TAB5_APP_STATE_RESUMED;

    if (ctx->lifecycle.on_resume) {
        ctx->lifecycle.on_resume();
    }

    return TAB5_OK;
}

tab5_err_t tab5_lifecycle_host_pause_app(tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    if (ctx->state != TAB5_APP_STATE_RESUMED) {
        return TAB5_ERR_INVALID_STATE;
    }

    ctx->state = TAB5_APP_STATE_PAUSED;

    if (ctx->lifecycle.on_pause) {
        ctx->lifecycle.on_pause();
    }

    return TAB5_OK;
}

tab5_err_t tab5_lifecycle_host_open_file(tab5_app_context_t *ctx, const char *filepath)
{
    if (ctx == nullptr || filepath == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    if (ctx->state == TAB5_APP_STATE_UNINITIALIZED || ctx->state == TAB5_APP_STATE_DESTROYED) {
        return TAB5_ERR_INVALID_STATE;
    }

    // Valida acesso de leitura na sandbox
    char safe_path[256];
    tab5_err_t err =
        tab5_storage_sandbox_resolve_path(filepath, safe_path, sizeof(safe_path), ctx->app_id, ctx->permissions, false);
    if (err != TAB5_OK) {
        LOG_W("Acesso negado para abrir arquivo: %s", filepath);
        return err;
    }

    if (ctx->lifecycle.on_open_file) {
        ctx->lifecycle.on_open_file(safe_path);
    }

    return TAB5_OK;
}

tab5_err_t tab5_lifecycle_host_destroy_app(tab5_app_context_t *ctx)
{
    if (ctx == nullptr) {
        return TAB5_ERR_INVALID_ARG;
    }

    if (ctx->state == TAB5_APP_STATE_DESTROYED) {
        return TAB5_OK;
    }

    if (ctx->lifecycle.on_destroy) {
        ctx->lifecycle.on_destroy();
    }

    // Destrói tela UI e limpa contexto
    tab5_ui_host_destroy_app_screen(ctx);

    ctx->state = TAB5_APP_STATE_DESTROYED;

    if (tab5_host_get_active_app() == ctx) {
        tab5_host_clear_active_app();
    }

    LOG_I("App %s destruida", ctx->app_id);
    return TAB5_OK;
}
