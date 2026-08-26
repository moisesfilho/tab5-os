#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HID_REPORT_MAP_MAX 8

typedef enum {
    HID_REPORT_UNKNOWN = 0,
    HID_REPORT_MOUSE,
    HID_REPORT_KEYBOARD,
    HID_REPORT_CONSUMER,
} hid_report_kind_t;

typedef struct {
    uint8_t report_id;
    hid_report_kind_t kind;
    /* Geometria extraida do descritor (permite decodificar relatorios
     * compostos, ex.: Lift da Logitech: 16 botoes, X/Y de 12 bits,
     * wheel e AC Pan de 8 bits). */
    uint16_t button_count;
    uint8_t x_bits;
    uint8_t y_bits;
    uint8_t wheel_bits;
    uint8_t pan_bits;
} hid_report_entry_t;

typedef struct {
    uint8_t report_id;
    uint8_t buttons;
    int32_t dx;
    int32_t dy;
    int32_t wheel;
    int32_t pan;
} hid_mouse_sample_t;

/* Parseia um descritor HID (Report Map 0x2A4B) e classifica cada Report ID.
 * Retorna a quantidade de entradas preenchidas (0 se nada util foi extraido). */
int hid_report_map_parse(const uint8_t *desc, size_t len, hid_report_entry_t *out, int max_out);

/* Classifica um report ID em uma tabela gerada por hid_report_map_parse. */
hid_report_kind_t hid_report_map_lookup(const hid_report_entry_t *entries, int count, uint8_t report_id);

const hid_report_entry_t *hid_report_map_find(const hid_report_entry_t *entries, int count, uint8_t report_id);

/* Decodifica um relatorio de mouse composto usando a geometria do descritor.
 * Retorna true quando o sample foi preenchido. */
bool hid_report_map_decode_mouse(const hid_report_entry_t *e, const uint8_t *data, size_t len, hid_mouse_sample_t *out);

#ifdef __cplusplus
}
#endif
