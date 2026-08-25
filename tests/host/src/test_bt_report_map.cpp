/* Testes do parser de Report Map HID (hid_report_map.cpp) com descritores
 * reais em miniatura: mouse boot, teclado, consumer, system control, vendor
 * e um composto estilo Logitech (teclado+mouse+consumer+system+vendor). */
#include "hid_report_map.h"

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>

namespace {

constexpr uint8_t TYPE_MAIN = 0x0;
constexpr uint8_t TYPE_GLOBAL = 0x1;
constexpr uint8_t TYPE_LOCAL = 0x2;

void push_item(std::vector<uint8_t> &d, uint8_t tag, uint8_t type, std::initializer_list<uint8_t> payload)
{
    size_t n = payload.size();
    uint8_t size_code = (n == 4) ? 3 : (uint8_t)n;
    d.push_back((uint8_t)((tag << 4) | (type << 2) | size_code));
    for (uint8_t b : payload) {
        d.push_back(b);
    }
}

void usage_page(std::vector<uint8_t> &d, uint16_t page)
{
    if (page > 0xFF) {
        d.push_back(0x06);
        d.push_back((uint8_t)(page & 0xFF));
        d.push_back((uint8_t)(page >> 8));
        return;
    }
    push_item(d, 0x0, TYPE_GLOBAL, {(uint8_t)page});
}

void report_id(std::vector<uint8_t> &d, uint8_t id)
{
    push_item(d, 0x8, TYPE_GLOBAL, {id});
}

void usage(std::vector<uint8_t> &d, uint16_t u)
{
    if (u > 0xFF) {
        d.push_back(0x0A);
        d.push_back((uint8_t)(u & 0xFF));
        d.push_back((uint8_t)(u >> 8));
        return;
    }
    push_item(d, 0x0, TYPE_LOCAL, {(uint8_t)u});
}

void collection(std::vector<uint8_t> &d)
{
    push_item(d, 0xA, TYPE_MAIN, {0x01});
}

void end_collection(std::vector<uint8_t> &d)
{
    push_item(d, 0xC, TYPE_MAIN, {});
}

void input(std::vector<uint8_t> &d, uint8_t flags = 0x02)
{
    push_item(d, 0x8, TYPE_MAIN, {flags});
}

std::vector<uint8_t> boot_mouse_map(bool with_id, uint8_t id = 0)
{
    std::vector<uint8_t> d;
    usage_page(d, 0x01);
    usage(d, 0x02);
    collection(d);
    if (with_id) {
        report_id(d, id);
    }
    usage(d, 0x01);
    collection(d);
    usage_page(d, 0x09);
    push_item(d, 0x1, TYPE_LOCAL, {0x01});
    push_item(d, 0x2, TYPE_LOCAL, {0x03});
    input(d);
    usage_page(d, 0x01);
    usage(d, 0x30);
    usage(d, 0x31);
    input(d, 0x06);
    end_collection(d);
    end_collection(d);
    return d;
}

std::vector<uint8_t> keyboard_map(uint8_t id)
{
    std::vector<uint8_t> d;
    usage_page(d, 0x01);
    usage(d, 0x06);
    collection(d);
    report_id(d, id);
    usage_page(d, 0x07);
    push_item(d, 0x1, TYPE_LOCAL, {0xE0});
    push_item(d, 0x2, TYPE_LOCAL, {0xE7});
    input(d);
    usage(d, 0x00);
    input(d);
    end_collection(d);
    return d;
}

std::vector<uint8_t> consumer_map(uint8_t id)
{
    std::vector<uint8_t> d;
    usage_page(d, 0x0C);
    usage(d, 0x01);
    collection(d);
    report_id(d, id);
    push_item(d, 0x1, TYPE_LOCAL, {0x00});
    d.push_back(0x2A); /* Usage Max 16 bits (0x02AC) */
    d.push_back(0xAC);
    d.push_back(0x02);
    input(d);
    end_collection(d);
    return d;
}

std::vector<uint8_t> system_control_map(uint8_t id)
{
    std::vector<uint8_t> d;
    usage_page(d, 0x01);
    usage(d, 0x80);
    collection(d);
    report_id(d, id);
    push_item(d, 0x1, TYPE_LOCAL, {0x82});
    input(d);
    end_collection(d);
    return d;
}

std::vector<uint8_t> vendor_map(uint8_t id)
{
    std::vector<uint8_t> d;
    usage_page(d, 0xFF60); /* pagina de vendor 16 bits */
    usage(d, 0x61);
    collection(d);
    report_id(d, id);
    input(d, 0x03);
    end_collection(d);
    return d;
}

} // namespace

TEST(HidReportMap, MouseBootSemReportId)
{
    auto desc = boot_mouse_map(false);
    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(desc.data(), desc.size(), out, HID_REPORT_MAP_MAX);

    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].report_id, 0);
    EXPECT_EQ(out[0].kind, HID_REPORT_MOUSE);
}

TEST(HidReportMap, MouseComReportId2)
{
    auto desc = boot_mouse_map(true, 0x02);
    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(desc.data(), desc.size(), out, HID_REPORT_MAP_MAX);

    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].report_id, 0x02);
    EXPECT_EQ(out[0].kind, HID_REPORT_MOUSE);
}

TEST(HidReportMap, TecladoComReportId1)
{
    auto desc = keyboard_map(0x01);
    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(desc.data(), desc.size(), out, HID_REPORT_MAP_MAX);

    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].report_id, 0x01);
    EXPECT_EQ(out[0].kind, HID_REPORT_KEYBOARD);
}

TEST(HidReportMap, ConsumerESystemControlViramConsumer)
{
    auto cons = consumer_map(0x03);
    auto sys = system_control_map(0x04);

    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(cons.data(), cons.size(), out, HID_REPORT_MAP_MAX);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].kind, HID_REPORT_CONSUMER);

    n = hid_report_map_parse(sys.data(), sys.size(), out, HID_REPORT_MAP_MAX);
    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].report_id, 0x04);
    EXPECT_EQ(out[0].kind, HID_REPORT_CONSUMER);
}

TEST(HidReportMap, VendorPageIgnorada)
{
    auto desc = vendor_map(0x05);
    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(desc.data(), desc.size(), out, HID_REPORT_MAP_MAX);

    EXPECT_EQ(n, 0);
}

/* Estilo Logitech Lift: teclado + mouse + consumer + system + vendor no mesmo
 * descritor, cada funcao em sua collection Application. */
TEST(HidReportMap, DispositivoCompostoEstiloLogitech)
{
    std::vector<uint8_t> d;
    const auto &kbd = keyboard_map(0x01);
    const auto &mouse = boot_mouse_map(true, 0x02);
    const auto &cons = consumer_map(0x03);
    const auto &sys = system_control_map(0x04);
    const auto &vend = vendor_map(0x05);
    d.insert(d.end(), kbd.begin(), kbd.end());
    d.insert(d.end(), mouse.begin(), mouse.end());
    d.insert(d.end(), cons.begin(), cons.end());
    d.insert(d.end(), sys.begin(), sys.end());
    d.insert(d.end(), vend.begin(), vend.end());

    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(d.data(), d.size(), out, HID_REPORT_MAP_MAX);

    ASSERT_EQ(n, 4);
    EXPECT_EQ(hid_report_map_lookup(out, n, 0x01), HID_REPORT_KEYBOARD);
    EXPECT_EQ(hid_report_map_lookup(out, n, 0x02), HID_REPORT_MOUSE);
    EXPECT_EQ(hid_report_map_lookup(out, n, 0x03), HID_REPORT_CONSUMER);
    EXPECT_EQ(hid_report_map_lookup(out, n, 0x04), HID_REPORT_CONSUMER);
    EXPECT_EQ(hid_report_map_lookup(out, n, 0x05), HID_REPORT_UNKNOWN);
    EXPECT_EQ(hid_report_map_lookup(out, n, 0x99), HID_REPORT_UNKNOWN);
}

TEST(HidReportMap, MesmoIdComInputsDiversosPriorizaMouse)
{
    /* Report ID 7 declarado com input de teclado e depois input de mouse */
    std::vector<uint8_t> d;
    usage_page(d, 0x07);
    usage(d, 0x06);
    collection(d);
    report_id(d, 0x07);
    input(d);
    end_collection(d);
    const auto &mouse_part = boot_mouse_map(true, 0x07);
    d.insert(d.end(), mouse_part.begin(), mouse_part.end());

    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(d.data(), d.size(), out, HID_REPORT_MAP_MAX);

    ASSERT_GE(n, 1);
    bool found = false;
    for (int i = 0; i < n; i++) {
        if (out[i].report_id == 0x07) {
            found = true;
            EXPECT_EQ(out[i].kind, HID_REPORT_MOUSE);
        }
    }
    EXPECT_TRUE(found);
}

TEST(HidReportMap, ItemLongoEEncapsuladoSemPerderSincronia)
{
    /* Item longo (0xFE) invalido no meio; o parser deve pula-lo e ainda
     * classificar o mouse que vem depois. */
    std::vector<uint8_t> d;
    d.push_back(0xFE);
    d.push_back(0x04);
    d.push_back(0x00);
    d.push_back(0xDE);
    d.push_back(0xAD);
    d.push_back(0xBE);
    d.push_back(0xEF);

    const auto &mouse = boot_mouse_map(true, 0x02);
    d.insert(d.end(), mouse.begin(), mouse.end());

    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(d.data(), d.size(), out, HID_REPORT_MAP_MAX);

    ASSERT_EQ(n, 1);
    EXPECT_EQ(out[0].report_id, 0x02);
    EXPECT_EQ(out[0].kind, HID_REPORT_MOUSE);
}

TEST(HidReportMap, DescritorTruncadoNaoCorrompeParser)
{
    auto full = boot_mouse_map(true, 0x02);
    std::vector<uint8_t> cut(full.begin(), full.begin() + full.size() / 2);

    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    int n = hid_report_map_parse(cut.data(), cut.size(), out, HID_REPORT_MAP_MAX);
    EXPECT_GE(n, 0);
    EXPECT_LE(n, HID_REPORT_MAP_MAX);
}

TEST(HidReportMap, EntradasInvalidasRetornamZero)
{
    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};

    EXPECT_EQ(hid_report_map_parse(nullptr, 10, out, HID_REPORT_MAP_MAX), 0);

    const uint8_t lixo[] = {0xFF, 0xEE, 0xDD, 0xCC};
    EXPECT_EQ(hid_report_map_parse(lixo, sizeof(lixo), out, HID_REPORT_MAP_MAX), 0);
    EXPECT_EQ(hid_report_map_parse(lixo, sizeof(lixo), nullptr, HID_REPORT_MAP_MAX), 0);
    EXPECT_EQ(hid_report_map_parse(lixo, sizeof(lixo), out, 0), 0);

    const uint8_t vazio[] = {};
    EXPECT_EQ(hid_report_map_parse(vazio, 0, out, HID_REPORT_MAP_MAX), 0);
}

TEST(HidReportMap, LookupToleranteATabelaVazia)
{
    EXPECT_EQ(hid_report_map_lookup(nullptr, 0, 0x02), HID_REPORT_UNKNOWN);

    hid_report_entry_t out[HID_REPORT_MAP_MAX] = {};
    EXPECT_EQ(hid_report_map_lookup(out, 0, 0x02), HID_REPORT_UNKNOWN);
}
