#pragma once

/* Stub do esp_log para host: descarta as mensagens, mas consome o TAG
 * para nao gerar warnings de variavel nao utilizada nos modulos. */

#define ESP_LOGE(tag, format, ...)                                                                                     \
    do {                                                                                                               \
        (void)(tag);                                                                                                   \
        (void)(format);                                                                                                \
    } while (0)

#define ESP_LOGW(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
