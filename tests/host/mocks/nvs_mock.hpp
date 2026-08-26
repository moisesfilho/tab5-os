#pragma once

#include <cstdint>
#include <string>

namespace hostmock {

/* Zera todo o armazenamento NVS simulado (chamar no SetUp). */
void nvs_reset();

/* Cria/redefine chaves para exercitar os caminhos de carga do modulo. */
void nvs_seed_i32(const std::string &ns, const std::string &key, int32_t value);
void nvs_seed_u8(const std::string &ns, const std::string &key, uint8_t value);

bool nvs_read_i32(const std::string &ns, const std::string &key, int32_t *out_value);
bool nvs_read_u8(const std::string &ns, const std::string &key, uint8_t *out_value);

} // namespace hostmock
