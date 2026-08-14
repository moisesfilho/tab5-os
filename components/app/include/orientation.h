#pragma once

#include "lvgl.h"

void orientation_reset(void);

/**
 * @brief Atualiza a orientacao a partir do vetor gravidade (unidade G).
 *
 * @param ax,ay,az leitura do IMU (driver bmi270 retorna o vetor gravidade)
 * @return rotacao LVGL alvo, com histerese e debounce
 */
lv_disp_rotation_t orientation_update(float ax, float ay, float az);
