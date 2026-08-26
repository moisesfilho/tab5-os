#pragma once

/* Shim do esp_log para o simulador: imprime na saida padrao. */

#include <cstdio>

#define ESP_LOGE(tag, format, ...) fprintf(stderr, "E %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) fprintf(stderr, "W %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) printf("I %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) printf("D %s: " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) printf("V %s: " format "\n", tag, ##__VA_ARGS__)
