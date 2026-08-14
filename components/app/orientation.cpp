#include "orientation.h"
#include <math.h>

#define ORIENT_FLAT_THRESHOLD 0.45f /* gravidade no plano da tela abaixo disso = deitado */
#define ORIENT_DEBOUNCE 5            /* leituras consecutivas antes de trocar */

static lv_disp_rotation_t s_rotation = LV_DISPLAY_ROTATION_0;
static lv_disp_rotation_t s_target = LV_DISPLAY_ROTATION_0;
static int s_stable = 0;

void orientation_reset(void)
{
    s_rotation = LV_DISPLAY_ROTATION_0;
    s_target = LV_DISPLAY_ROTATION_0;
    s_stable = 0;
}

/* Vetor gravidade (g = -acce) projetado no plano da tela.
 * idx: 0 = gravidade p/ +Y, 1 = +X, 2 = -Y, 3 = -X */
static lv_disp_rotation_t gravity_to_rotation(float gx, float gy)
{
    float plane = sqrtf(gx * gx + gy * gy);
    if (plane < ORIENT_FLAT_THRESHOLD) {
        return s_rotation; /* deitado: mantem a atual */
    }

    float ang = atan2f(gx, gy) * 180.0f / M_PI; /* 0 = grav p/ +Y, +90 = grav p/ +X */
    if (ang < 0.0f) {
        ang += 360.0f;
    }

    int idx = (int)((ang + 45.0f) / 90.0f) % 4;
    static const lv_disp_rotation_t map[4] = {
        LV_DISPLAY_ROTATION_0,   /* grav +Y  -> retrato */
        LV_DISPLAY_ROTATION_90,  /* grav +X  -> paisagem A */
        LV_DISPLAY_ROTATION_180, /* grav -Y  -> retrato invertido */
        LV_DISPLAY_ROTATION_270, /* grav -X  -> paisagem B */
    };
    return map[idx];
}

lv_disp_rotation_t orientation_update(float ax, float ay, float az)
{
    (void)az;
    /* O driver bmi270 (via bsp_sensor_init) retorna o VETOR GRAVIDADE diretamente
     * (ex.: flat com tela p/ cima => z=-1 G). Portanto g = +acce (nao -acce). */
    float gx = ax;
    float gy = ay;

    lv_disp_rotation_t target = gravity_to_rotation(gx, gy);
    if (target == s_rotation) {
        s_stable = 0;
        return s_rotation;
    }

    if (target == s_target) {
        if (++s_stable >= ORIENT_DEBOUNCE) {
            s_rotation = target;
            s_stable = 0;
        }
    } else {
        s_target = target;
        s_stable = 0;
    }
    return s_rotation;
}
