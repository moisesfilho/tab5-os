/**
 * @file tab5_package_mgr.h
 * @brief Gerenciador de Pacotes (.tab5pkg), Instalação e Registro Dinâmico
 */

#pragma once

#include "tab5_manifest.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAB5_APPS_DIR "/sdcard/apps"
#define TAB5_APPS_INSTALLED_DIR "/sdcard/apps/installed"
#define TAB5_APPS_EMBEDDED_DIR "/apps"
#define TAB5_APPS_DATA_DIR "/sdcard/data"

typedef struct {
    tab5_manifest_t manifest;
    char install_path[256];
    bool is_embedded;
} tab5_installed_app_info_t;

typedef void (*tab5_pkg_scan_cb_t)(const tab5_installed_app_info_t *app_info, void *user_data);

/**
 * @brief Inicializa o subsistema do gerenciador de pacotes.
 */
tab5_err_t tab5_package_mgr_init(void);

/**
 * @brief Instala uma aplicação a partir de um pacote/pasta para /sdcard/apps/installed/<app_id>/.
 */
tab5_err_t tab5_package_mgr_install(const char *source_path, char *out_app_id, size_t id_buf_size);

/**
 * @brief Desinstala uma aplicação instalada no SD.
 * @param app_id Identificador da aplicação (ex: "com.tab5.notas").
 * @param delete_user_data Se true, apaga também /sdcard/data/<app_id>.
 */
tab5_err_t tab5_package_mgr_uninstall(const char *app_id, bool delete_user_data);

/**
 * @brief Varre os diretórios de apps instaladas e embutidas, registrando todas no Desktop.
 */
int tab5_package_mgr_scan_and_register_all(void);

/**
 * @brief Obtém as informações/manifesto de uma app instalada pelo ID.
 */
tab5_err_t tab5_package_mgr_get_app_info(const char *app_id, tab5_installed_app_info_t *out_info);

/**
 * @brief Lança a execução de uma aplicação desacoplada/isolada Wasm.
 */
tab5_err_t tab5_package_mgr_launch(const char *app_id, const char *open_file_path);

/**
 * @brief Fecha a aplicação ativa atualmente em execução.
 */
tab5_err_t tab5_package_mgr_close_active(void);

#ifdef __cplusplus
}
#endif
