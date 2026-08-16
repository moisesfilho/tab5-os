#pragma once

#include <string>

/**
 * @brief Executa uma linha de comando e atualiza o diretório de trabalho atual.
 *
 * @param line Linha de comando digitada pelo usuário.
 * @param cwd Diretório de trabalho atual (in/out, ex: "/sdcard").
 * @return std::string Saída formatada para exibição no terminal.
 */
std::string terminal_exec(const std::string &line, std::string &cwd);
