#include "hid_report_map.h"

/*
 * Parser de Report Descriptor HID (formato USB HID Item, usado verbatim no
 * BLE HOGP). Percorre os itens rastreando Usage Page global, usages locais
 * pendentes, Report ID e a pilha de Collections para classificar cada bloco
 * de Input e extrair a geometria dos campos (botoes, X/Y, wheel, pan) —
 * necessaria para decodificar mouses compostos como o Logitech Lift.
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
#define TAG_GLOBAL_REPORT_SIZE 0x7
#define TAG_GLOBAL_REPORT_COUNT 0x9
#define TAG_LOCAL_USAGE 0x0
#define TAG_LOCAL_USAGE_MIN 0x1
#define TAG_LOCAL_USAGE_MAX 0x2

#define USAGE_PAGE_GENERIC_DESKTOP 0x01
#define USAGE_PAGE_KEYBOARD 0x07
#define USAGE_PAGE_BUTTON 0x09
#define USAGE_PAGE_CONSUMER 0x0C

#define COLLECTION_MAX_DEPTH 6
#define PENDING_USAGES_MAX 12

/* Usages relevantes para a geometria do mouse */
#define USAGE_GDT_X 0x30
#define USAGE_GDT_Y 0x31
#define USAGE_GDT_WHEEL 0x38
#define USAGE_CONSUMER_AC_PAN 0x0238

typedef struct {
    uint16_t page;
    uint16_t usage;
    bool valid;
} collection_ctx_t;

typedef struct {
    uint16_t page;
    uint16_t local_usage;
    bool local_usage_valid;
    uint16_t pending_usages[PENDING_USAGES_MAX];
    int pending_count;
    uint16_t usage_min;
    uint16_t usage_max;
    bool range_pending;
    uint32_t report_size;
    uint32_t report_count;
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

static hid_report_entry_t *entry_for(parser_state_t *st, uint8_t report_id)
{
    for (int i = 0; i < st->count; i++) {
        if (st->out[i].report_id == report_id) {
            return &st->out[i];
        }
    }
    if (st->count < st->max_out) {
        hid_report_entry_t *e = &st->out[st->count++];
        e->report_id = report_id;
        return e;
    }
    return nullptr;
}

static void record_input(parser_state_t *st, hid_report_kind_t kind)
{
    if (kind != HID_REPORT_UNKNOWN) {
        hid_report_entry_t *e = entry_for(st, st->report_id);
        if (e != nullptr && kind_priority(kind) > kind_priority(e->kind)) {
            e->kind = kind;
        }
    }

    /* Itens main consomem os usages locais pendentes. */
    st->pending_count = 0;
    st->range_pending = false;
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
            } else if (tag == TAG_GLOBAL_REPORT_SIZE) {
                st.report_size = value;
            } else if (tag == TAG_GLOBAL_REPORT_COUNT) {
                st.report_count = value;
            }
        } else if (type == ITEM_TYPE_LOCAL) {
            if (tag == TAG_LOCAL_USAGE) {
                if (st.pending_count < PENDING_USAGES_MAX) {
                    st.pending_usages[st.pending_count++] = (uint16_t)value;
                }
                st.local_usage = (uint16_t)value;
                st.local_usage_valid = true;
            } else if (tag == TAG_LOCAL_USAGE_MIN) {
                st.usage_min = (uint16_t)value;
                st.range_pending = true;
            } else if (tag == TAG_LOCAL_USAGE_MAX) {
                st.usage_max = (uint16_t)value;
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
            case TAG_MAIN_INPUT: {
                hid_report_kind_t kind = classify_input(&st);
                hid_report_entry_t *e = entry_for(&st, st.report_id);

                if (kind == HID_REPORT_MOUSE && e != nullptr) {
                    /* Geometria a partir dos itens globais/locais pendentes:
                     * botoes (pagina Button), X/Y/Wheel (GDT) e AC Pan. Cada
                     * usage mapeia um campo de report_size bits — quando X e
                     * Y dividem o mesmo Input (report_count 2), cada um fica
                     * com metade dos bits. */
                    if (st.page == USAGE_PAGE_BUTTON && st.report_size > 0 && st.report_count > 0) {
                        uint32_t add = st.report_size * st.report_count;
                        uint32_t room = 0xFFFFu - e->button_count;
                        e->button_count += (uint16_t)(add > room ? room : add);
                    }
                    for (int i = 0; i < st.pending_count; i++) {
                        uint8_t bits = (uint8_t)st.report_size;
                        switch (st.pending_usages[i]) {
                        case USAGE_GDT_X:
                            e->x_bits = bits;
                            break;
                        case USAGE_GDT_Y:
                            e->y_bits = bits;
                            break;
                        case USAGE_GDT_WHEEL:
                            e->wheel_bits = bits;
                            break;
                        case USAGE_CONSUMER_AC_PAN:
                            e->pan_bits = bits;
                            break;
                        default:
                            break;
                        }
                    }
                }

                record_input(&st, kind);
                break;
            }
            default:
                break;
            }

            /* Itens main consomem os usages locais pendentes. */
            st.local_usage_valid = false;
        }
    }

    /* Descarta entradas sem classificacao util (ex.: paginas de vendor). */
    int kept = 0;
    for (int i = 0; i < st.count; i++) {
        if (st.out[i].kind != HID_REPORT_UNKNOWN) {
            st.out[kept++] = st.out[i];
        }
    }
    return kept;
}

hid_report_kind_t hid_report_map_lookup(const hid_report_entry_t *entries, int count, uint8_t report_id)
{
    const hid_report_entry_t *e = hid_report_map_find(entries, count, report_id);
    return (e != nullptr) ? e->kind : HID_REPORT_UNKNOWN;
}

const hid_report_entry_t *hid_report_map_find(const hid_report_entry_t *entries, int count, uint8_t report_id)
{
    if (entries == nullptr) {
        return nullptr;
    }
    for (int i = 0; i < count; i++) {
        if (entries[i].report_id == report_id) {
            return &entries[i];
        }
    }
    return nullptr;
}

static int32_t sign_extend(uint32_t value, uint8_t bits)
{
    if (bits == 0 || bits >= 32) {
        return (int32_t)value;
    }
    uint32_t sign_bit = 1u << (bits - 1);
    return (int32_t)((value ^ sign_bit) - sign_bit);
}

bool hid_report_map_decode_mouse(const hid_report_entry_t *e, const uint8_t *data, size_t len,
                                 hid_mouse_sample_t *out_sample)
{
    if (e == nullptr || data == nullptr || out_sample == nullptr || len == 0 || e->x_bits == 0 || e->y_bits == 0) {
        return false;
    }

    uint16_t button_bits = (e->button_count > 32) ? 32 : e->button_count;
    size_t need = ((button_bits + 7) & ~7u) + e->x_bits + e->y_bits + e->wheel_bits + e->pan_bits;
    if (need > len * 8) {
        return false;
    }

    size_t bit_pos = 0;
    auto read_bits = [&](uint8_t nbits) -> uint32_t {
        uint32_t val = 0;
        for (uint8_t b = 0; b < nbits; b++) {
            size_t byte_idx = bit_pos >> 3;
            if (byte_idx >= len) {
                bit_pos += nbits - b;
                return val;
            }
            if ((data[byte_idx] >> (bit_pos & 7)) & 1) {
                val |= (1u << b);
            }
            bit_pos++;
        }
        return val;
    };

    uint32_t buttons_raw = read_bits((uint8_t)((button_bits + 7) & ~7u));

    int32_t dx = sign_extend(read_bits(e->x_bits), e->x_bits);
    int32_t dy = sign_extend(read_bits(e->y_bits), e->y_bits);
    int32_t wheel = (e->wheel_bits > 0) ? sign_extend(read_bits(e->wheel_bits), e->wheel_bits) : 0;
    int32_t pan = (e->pan_bits > 0) ? sign_extend(read_bits(e->pan_bits), e->pan_bits) : 0;

    out_sample->report_id = e->report_id;
    out_sample->buttons = (uint8_t)buttons_raw;
    out_sample->dx = dx;
    out_sample->dy = dy;
    out_sample->wheel = wheel;
    out_sample->pan = pan;
    return true;
}
