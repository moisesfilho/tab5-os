#pragma once

#include "esp_err.h"
#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SSH_CLIENT_DISCONNECTED = 0,
    SSH_CLIENT_CONNECTING,
    SSH_CLIENT_NEED_PASSWORD,
    SSH_CLIENT_AUTHENTICATING,
    SSH_CLIENT_CONNECTED,
    SSH_CLIENT_DISCONNECTING,
    SSH_CLIENT_ERROR
} ssh_client_state_t;

/**
 * @brief Callback chamado quando chegam dados da saida remota do SSH (stdout/stderr).
 *
 * @param data Ponteiro para os dados recebidos.
 * @param len Tamanho em bytes.
 */
typedef void (*ssh_rx_cb_t)(const char *data, size_t len);

/**
 * @brief Callback chamado quando o estado da conexao SSH muda.
 *
 * @param state Novo estado da conexao.
 * @param msg Mensagem descritiva opcional (ex: erro ou status).
 */
typedef void (*ssh_state_cb_t)(ssh_client_state_t state, const char *msg);

/**
 * @brief Inicia a conexao SSH assincrona em uma task dedicada.
 *
 * @param user Nome de usuario (ex: "root", "pi").
 * @param host Endereco IP ou hostname do servidor remoto.
 * @param port Porta SSH (padrao 22).
 * @param rx_cb Callback de recepcao de dados remotos.
 * @param state_cb Callback de mudanca de estado.
 * @return esp_err_t ESP_OK se a task foi disparada com sucesso.
 */
esp_err_t ssh_client_connect(const char *user, const char *host, int port, ssh_rx_cb_t rx_cb, ssh_state_cb_t state_cb);

/**
 * @brief Envia a senha de autenticacao para o cliente SSH em espera.
 *
 * @param password Senha em texto plano.
 * @return esp_err_t ESP_OK se a senha foi enfileirada.
 */
esp_err_t ssh_client_send_password(const char *password);

/**
 * @brief Envia dados de entrada do usuario (teclas, comandos) para o canal SSH interativo.
 *
 * @param data Dados a serem transmitidos.
 * @param len Tamanho em bytes.
 * @return esp_err_t ESP_OK se os dados foram enfileirados/enviados.
 */
esp_err_t ssh_client_send_data(const char *data, size_t len);

/**
 * @brief Solicita o encerramento da conexao e fechamento da sessao SSH.
 */
void ssh_client_disconnect(void);

/**
 * @brief Verifica se ha uma sessao SSH ativa ou em processo de conexao.
 *
 * @return true se conectando ou conectado.
 */
bool ssh_client_is_active(void);

/**
 * @brief Retorna o estado atual do cliente SSH.
 */
ssh_client_state_t ssh_client_get_state(void);

#ifdef __cplusplus
}
#endif
