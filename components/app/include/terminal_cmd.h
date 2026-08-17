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

/**
 * @brief Identifica e extrai os parâmetros de um comando 'ssh [user@]host [-p port]'.
 *
 * @param line Linha digitada.
 * @param user Usuário extraído (padrão "root").
 * @param host Host/IP extraído.
 * @param port Porta extraída (padrão 22).
 * @param err_msg Mensagem de erro caso a sintaxe seja inválida.
 * @return true se a linha é um comando ssh válido.
 */
bool terminal_parse_ssh_cmd(const std::string &line, std::string &user, std::string &host, int &port,
                            std::string &err_msg);
