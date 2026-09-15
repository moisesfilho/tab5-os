#pragma once

#include <cstdint>
#include <string>
#include <thread>

namespace hostmock {

/* Zera todo o armazenamento NVS simulado (chamar no SetUp). */
void nvs_reset();

/* Cria/redefine chaves para exercitar os caminhos de carga do modulo. */
void nvs_seed_i32(const std::string &ns, const std::string &key, int32_t value);
void nvs_seed_u8(const std::string &ns, const std::string &key, uint8_t value);

bool nvs_read_i32(const std::string &ns, const std::string &key, int32_t *out_value);
bool nvs_read_u8(const std::string &ns, const std::string &key, uint8_t *out_value);

/* -- Ganchos de teste do tab5_nvs_worker --------------------------------
 * O mock passa a respeitar o namespace aberto por handle (nvs_open mapeia
 * handle -> ns; get/set/commit operam no ns do handle), instrumenta o
 * nvs_commit (contador + thread que executou) e permite emular uma
 * operacao NVS lenta para exercitar os timeouts REQUEST_TIMEOUT_MS do
 * worker de forma deterministica. */

/* A proxima chamada a nvs_open() aguarda delay_ms antes de retornar (one-shot). */
void nvs_stall_next_open_once(uint32_t delay_ms);
void nvs_stall_clear();

/* True quando o stall one-shot ja foi consumido por um nvs_open. */
bool nvs_stall_was_consumed();
/* Bloqueia ate o stall ser consumido (ou timeout_ms expirar); retorna o estado. */
bool nvs_wait_stall_consumed(uint32_t timeout_ms);

uint64_t nvs_commit_call_count();
std::thread::id nvs_last_commit_thread();

} // namespace hostmock
