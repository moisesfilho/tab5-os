#pragma once

/* Shim do esp_netif para o simulador: o fileserver mostra o IP do device;
 * aqui devolvemos um IP fixo fake. Estilo IDF: handle e ponteiro opaco
 * (esp_netif_t *), macros IPSTR/IP2STR para snprintf. */

#include <cstdint>

typedef struct esp_netif_obj {
    uint32_t _unused;
} esp_netif_t;

typedef struct {
    uint32_t addr;
} esp_ip4_addr_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t netmask;
    esp_ip4_addr_t gw;
} esp_netif_ip_info_t;

#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip)                                                                                                     \
    ((unsigned)((ip)->addr & 0xFF)), ((unsigned)(((ip)->addr >> 8) & 0xFF)), ((unsigned)(((ip)->addr >> 16) & 0xFF)),  \
        ((unsigned)(((ip)->addr >> 24) & 0xFF))

inline esp_netif_t *esp_netif_get_handle_from_ifkey(const char *ifkey)
{
    (void)ifkey;
    static esp_netif_t handle = {1};
    return &handle;
}

inline int esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{
    (void)netif;
    if (info == nullptr) {
        return -1;
    }
    info->ip.addr = (192u) | (168u << 8) | (1u << 16) | (50u << 24);
    info->netmask.addr = 0x00ffffff;
    info->gw.addr = (192u) | (168u << 8) | (1u << 16);
    return 0;
}
