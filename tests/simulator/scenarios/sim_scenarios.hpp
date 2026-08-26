#pragma once

#include <cstdint>
#include <string>
#include <vector>

/* Um passo de cenario: acao a executar + tempo de acomodacao antes da
 * captura + nome do arquivo de saida. */
struct sim_step {
    void (*action)();
    uint32_t settle_ms;
    const char *shot_name;
};

struct sim_scenario {
    const char *name;
    const char *description;
    std::vector<sim_step> steps;
};

const std::vector<sim_scenario> &sim_scenarios();

/* Acoes auxiliares compartilhadas entre cenarios. */
namespace simact {

void click(int x, int y);
void type_text(const char *text);

} // namespace simact
