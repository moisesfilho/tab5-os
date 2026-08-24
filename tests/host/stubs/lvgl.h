#pragma once

/* Stub minimo do LVGL 9 para os testes em host: apenas os tipos usados
 * pelos headers dos modulos sob teste (orientation.h / app_registry.h /
 * display_storage.h). Os valores do enum seguem o LVGL 9 real (0..3),
 * mesma codificacao persistida pelo display_storage. */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _lv_obj_t lv_obj_t;

typedef enum {
    LV_DISPLAY_ROTATION_0 = 0,
    LV_DISPLAY_ROTATION_90 = 1,
    LV_DISPLAY_ROTATION_180 = 2,
    LV_DISPLAY_ROTATION_270 = 3,
} lv_disp_rotation_t;

#ifdef __cplusplus
}
#endif
