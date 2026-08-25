#pragma once

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
} hid_report_entry_t;

/* Parseia um descritor HID (Report Map 0x2A4B) e classifica cada Report ID.
 * Retorna a quantidade de entradas preenchidas (0 se nada util foi extraido). */
int hid_report_map_parse(const uint8_t *desc, size_t len, hid_report_entry_t *out, int max_out);

/* Classifica um report ID em uma tabela gerada por hid_report_map_parse. */
hid_report_kind_t hid_report_map_lookup(const hid_report_entry_t *entries, int count, uint8_t report_id);

#ifdef __cplusplus
}
#endif
