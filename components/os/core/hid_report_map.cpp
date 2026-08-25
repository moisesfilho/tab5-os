#include "hid_report_map.h"

/*
 * Parser minimalista de Report Descriptor HID (USB HID Item format, usado
 * verbatim no BLE HOGP). Percorre os itens rastreando Usage Page global,
 * Usage local, Report ID e a pilha de Collections para classificar cada
 * bloco de Input em mouse / teclado / consumer.
 *
 * Formato do item: 1 byte header + payload little-endian.
 *   bits 1..0 = tamanho (0,1,2,4 bytes); bits 3..2 = tipo; bits 7..4 = tag.
 */

#define ITEM_TYPE_MAIN 0x0
#define ITEM_TYPE_GLOBAL 0x1
#define ITEM_TYPE_LOCAL 0x2

#define TAG_MAIN_INPUT 0x8
#define TAG_MAIN_COLLECTION 0xA
#define TAG_MAIN_END_COLLECTION 0xC
#define TAG_GLOBAL_USAGE_PAGE 0x0
#define TAG_GLOBAL_REPORT_ID 0x8
#define TAG_LOCAL_USAGE 0x0

#define USAGE_PAGE_GENERIC_DESKTOP 0x01
#define USAGE_PAGE_KEYBOARD 0x07
#define USAGE_PAGE_BUTTON 0x09
#define USAGE_PAGE_CONSUMER 0x0C

#define COLLECTION_MAX_DEPTH 6

typedef struct {
    uint16_t page;
    uint16_t usage;
    bool valid;
} collection_ctx_t;

typedef struct {
    uint16_t page;
    uint16_t local_usage;
    bool local_usage_valid;
    uint8_t report_id;
    collection_ctx_t stack[COLLECTION_MAX_DEPTH];
    int depth;
    hid_report_entry_t *out;
    int max_out;
    int count;
} parser_state_t;

static size_t item_payload_size(uint8_t header)
{
    switch (header & 0x03) {
    case 0:
        return 0;
    case 1:
        return 1;
    case 2:
        return 2;
    default:
        return 4;
    }
}

static uint32_t item_value(const uint8_t *data, size_t len)
{
    uint32_t val = 0;
    for (size_t i = 0; i < len && i < 4; i++) {
        val |= ((uint32_t)data[i]) << (8 * i);
    }
    return val;
}

/* Prioridade: mouse > teclado > consumer > unknown. Compostos (ex.: um mesmo
 * report ID com inputs de paginas distintas) ficam com o tipo mais util. */
static int kind_priority(hid_report_kind_t kind)
{
    switch (kind) {
    case HID_REPORT_MOUSE:
        return 3;
    case HID_REPORT_KEYBOARD:
        return 2;
    case HID_REPORT_CONSUMER:
        return 1;
    default:
        return 0;
    }
}

static void record_report(parser_state_t *st, uint8_t report_id, hid_report_kind_t kind)
{
    if (kind == HID_REPORT_UNKNOWN) {
        return;
    }

    for (int i = 0; i < st->count; i++) {
        if (st->out[i].report_id == report_id) {
            if (kind_priority(kind) > kind_priority(st->out[i].kind)) {
                st->out[i].kind = kind;
            }
            return;
        }
    }

    if (st->count < st->max_out) {
        st->out[st->count].report_id = report_id;
        st->out[st->count].kind = kind;
        st->count++;
    }
}

/* Classifica um Input pelo contexto das collections, da mais externa para a
 * mais interna: a funcao do dispositivo costuma estar na raiz (Application),
 * enquanto as internas (Physical/Logical) apenas agrupam campos. */
static hid_report_kind_t classify_input(parser_state_t *st)
{
    for (int i = 0; i < st->depth; i++) {
        const collection_ctx_t *c = &st->stack[i];
        if (!c->valid) {
            continue;
        }

        if (c->page == USAGE_PAGE_KEYBOARD) {
            return HID_REPORT_KEYBOARD;
        }
        if (c->page == USAGE_PAGE_CONSUMER) {
            return HID_REPORT_CONSUMER;
        }
        if (c->page == USAGE_PAGE_GENERIC_DESKTOP) {
            switch (c->usage) {
            case 0x02: /* Mouse */
                return HID_REPORT_MOUSE;
            case 0x06: /* Keyboard */
                return HID_REPORT_KEYBOARD;
            case 0x80: /* System Control (teclas de midia de sistema) */
                return HID_REPORT_CONSUMER;
            default:
                continue;
            }
        }
    }
    return HID_REPORT_UNKNOWN;
}

int hid_report_map_parse(const uint8_t *desc, size_t len, hid_report_entry_t *out, int max_out)
{
    if (desc == nullptr || out == nullptr || max_out <= 0) {
        return 0;
    }

    parser_state_t st = {};
    st.out = out;
    st.max_out = max_out;

    size_t pos = 0;
    while (pos < len) {
        uint8_t header = desc[pos++];
        size_t size = item_payload_size(header);

        /* Item longo (0xFE): os 2 bytes seguintes sao o tamanho real. */
        if (header == 0xFE) {
            if (pos + 2 > len) {
                break;
            }
            size = (size_t)desc[pos] | (((size_t)desc[pos + 1]) << 8);
            pos += 2 + size;
            continue;
        }

        if (pos + size > len) {
            break;
        }

        uint32_t value = item_value(desc + pos, size);
        pos += size;

        uint8_t type = (header >> 2) & 0x03;
        uint8_t tag = (header >> 4) & 0x0F;

        if (type == ITEM_TYPE_GLOBAL) {
            if (tag == TAG_GLOBAL_USAGE_PAGE) {
                st.page = (uint16_t)value;
            } else if (tag == TAG_GLOBAL_REPORT_ID) {
                st.report_id = (uint8_t)value;
            }
        } else if (type == ITEM_TYPE_LOCAL) {
            if (tag == TAG_LOCAL_USAGE) {
                st.local_usage = (uint16_t)value;
                st.local_usage_valid = true;
            }
        } else if (type == ITEM_TYPE_MAIN) {
            switch (tag) {
            case TAG_MAIN_COLLECTION:
                if (st.depth < COLLECTION_MAX_DEPTH) {
                    collection_ctx_t *c = &st.stack[st.depth];
                    c->page = st.page;
                    c->usage = st.local_usage_valid ? st.local_usage : 0;
                    c->valid = true;
                    st.depth++;
                }
                break;
            case TAG_MAIN_END_COLLECTION:
                if (st.depth > 0) {
                    st.depth--;
                }
                break;
            case TAG_MAIN_INPUT:
                record_report(&st, st.report_id, classify_input(&st));
                break;
            default:
                break;
            }

            /* Itens main consomem os usages locais pendentes. */
            st.local_usage_valid = false;
        }
    }

    return st.count;
}

hid_report_kind_t hid_report_map_lookup(const hid_report_entry_t *entries, int count, uint8_t report_id)
{
    if (entries == nullptr) {
        return HID_REPORT_UNKNOWN;
    }
    for (int i = 0; i < count; i++) {
        if (entries[i].report_id == report_id) {
            return entries[i].kind;
        }
    }
    return HID_REPORT_UNKNOWN;
}
