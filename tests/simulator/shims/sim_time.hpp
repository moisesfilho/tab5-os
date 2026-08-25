#pragma once

#include <cstdint>
#include <ctime>

/* Tempo controlavel do simulador: no modo cenario o relogio e congelado
 * (determinismo das capturas); no modo interativo segue a hora real.
 * Implementado via --wrap=time/localtime_r no link. */

namespace simtime {

constexpr time_t FROZEN_EPOCH = 1767225600; /* 2026-01-01 00:00:00 UTC */

void set_frozen(bool frozen);

} // namespace simtime
