#include "ai_storage.h"
#include "wifi_storage.h"
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"

static const char *TAG = "tab5_ai_storage";

static void trim_inplace(char *str)
{
    if (str == NULL) {
        return;
    }
    // Trim leading
    char *start = str;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    // Trim trailing
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[len - 1] = '\0';
        len--;
    }
}

void ai_storage_get_default(ai_cfg_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(ai_cfg_t));
    snprintf(cfg->base_url, sizeof(cfg->base_url), "%s", AI_DEFAULT_BASE_URL);
    cfg->token[0] = '\0';
    snprintf(cfg->model, sizeof(cfg->model), "%s", AI_DEFAULT_MODEL);
    cfg->max_tokens = AI_DEFAULT_MAX_TOKENS;
    cfg->timeout_sec = AI_DEFAULT_TIMEOUT_SEC;
}

esp_err_t ai_storage_load(ai_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ai_storage_get_default(cfg);

    if (wifi_storage_mount() != ESP_OK) {
        return ESP_FAIL;
    }

    FILE *f = fopen(AI_CFG_PATH, "r");
    if (f == NULL) {
        ESP_LOGI(TAG, "%s nao encontrado, usando configuracao padrao", AI_CFG_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    char line[2048];
    while (fgets(line, sizeof(line), f) != NULL) {

        char *start = line;
        while (*start != '\0' && isspace((unsigned char)*start)) {
            start++;
        }
        if (*start == '#' || *start == ';' || *start == '\0') {
            continue;
        }

        char *eq = strchr(start, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        char *key = start;
        char *val = eq + 1;

        trim_inplace(key);
        trim_inplace(val);

        if (strcmp(key, "base_url") == 0) {
            snprintf(cfg->base_url, sizeof(cfg->base_url), "%s", val);
        } else if (strcmp(key, "token") == 0) {
            snprintf(cfg->token, sizeof(cfg->token), "%s", val);
        } else if (strcmp(key, "model") == 0) {
            snprintf(cfg->model, sizeof(cfg->model), "%s", val);
        } else if (strcmp(key, "max_tokens") == 0) {
            int mt = (int)strtol(val, NULL, 10);
            if (mt > 0 && mt <= 8192) {
                cfg->max_tokens = mt;
            }
        } else if (strcmp(key, "timeout_sec") == 0) {
            int to = (int)strtol(val, NULL, 10);
            if (to >= 5 && to <= 300) {
                cfg->timeout_sec = to;
            }
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Configuracao de IA carregada de %s (base_url=%s, model=%s, max_tokens=%d)", AI_CFG_PATH,
             cfg->base_url, cfg->model, cfg->max_tokens);
    return ESP_OK;
}

esp_err_t ai_storage_save(const ai_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (wifi_storage_mount() != ESP_OK) {
        return ESP_FAIL;
    }

    int fd = open(AI_CFG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        ESP_LOGW(TAG, "Falha ao abrir %s para gravacao", AI_CFG_PATH);
        return ESP_FAIL;
    }

    FILE *f = fdopen(fd, "w");
    if (f == NULL) {
        close(fd);
        ESP_LOGW(TAG, "Falha ao associar stream para %s", AI_CFG_PATH);
        return ESP_FAIL;
    }

    fprintf(f, "# Configuracoes do Cliente Chat IA (OpenAI-compativel)\n");
    fprintf(f, "base_url=%s\n", cfg->base_url);
    fprintf(f, "token=%s\n", cfg->token);
    fprintf(f, "model=%s\n", cfg->model);
    fprintf(f, "max_tokens=%d\n", cfg->max_tokens > 0 ? cfg->max_tokens : AI_DEFAULT_MAX_TOKENS);
    fprintf(f, "timeout_sec=%d\n", cfg->timeout_sec > 0 ? cfg->timeout_sec : AI_DEFAULT_TIMEOUT_SEC);

    fclose(f);
    ESP_LOGI(TAG, "Configuracao de IA salva com sucesso em %s", AI_CFG_PATH);
    return ESP_OK;
}
