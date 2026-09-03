/**
 * @file storage_mgr.h
 * @brief Gerenciador de Armazenamento, Estatísticas de Memória e Pacotes
 */

#pragma once

#include "tab5_sdk.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
#include <vector>
#include <string>
extern "C" {
#endif

typedef struct {
    uint64_t total_bytes; /**< Espaço total em bytes */
    uint64_t used_bytes;  /**< Espaço ocupado em bytes */
    uint64_t free_bytes;  /**< Espaço livre em bytes */
    float usage_percent;  /**< Percentual de uso (0.0f a 100.0f) */
} tab5_storage_stats_t;

typedef struct {
    uint32_t internal_free_bytes; /**< Heap interna livre */
    uint32_t psram_free_bytes;    /**< PSRAM externa livre */
    uint32_t dma_free_bytes;      /**< Heap DMA livre */
    uint32_t total_free_bytes;    /**< Total de memória livre */
} tab5_ram_stats_t;

typedef struct {
    char id[64];
    char name[64];
    char version[32];
    bool is_embedded;
    uint32_t binary_size_bytes;
    uint32_t data_size_bytes;
} tab5_app_storage_item_t;

typedef struct {
    char filename[64];
    char full_path[256];
    char id[64];
    char name[64];
    char version[32];
    uint32_t package_size_bytes;
} tab5_pending_package_t;

/**
 * @brief Obtém as estatísticas de armazenamento da partição de apps embutidas (Flash /apps).
 */
tab5_err_t tab5_storage_mgr_get_flash_apps_stats(tab5_storage_stats_t *out_stats);

/**
 * @brief Obtém as estatísticas de armazenamento do Cartão SD (/sdcard).
 */
tab5_err_t tab5_storage_mgr_get_sd_stats(tab5_storage_stats_t *out_stats);

/**
 * @brief Obtém as estatísticas de memória RAM e PSRAM em tempo real.
 */
tab5_err_t tab5_storage_mgr_get_ram_stats(tab5_ram_stats_t *out_stats);

/**
 * @brief Calcula recursivamente o tamanho ocupado por um diretório ou arquivo.
 */
uint64_t tab5_storage_mgr_calculate_path_size(const char *path);

/**
 * @brief Verifica se há espaço livre suficiente no SD para instalar um pacote.
 * @param required_bytes Bytes necessários para instalação.
 * @return true se houver espaço suficiente com margem de segurança.
 */
bool tab5_storage_mgr_has_enough_sd_space(uint64_t required_bytes);

#ifdef __cplusplus
}

/**
 * @brief Retorna a lista de todas as aplicações instaladas com seus respectivos consumos de disco.
 */
std::vector<tab5_app_storage_item_t> tab5_storage_mgr_list_installed_apps(void);

/**
 * @brief Varre /sdcard/apps/ procurando pacotes .tab5pkg pendentes de instalação.
 */
std::vector<tab5_pending_package_t> tab5_storage_mgr_list_pending_packages(void);

#endif
