#include "ai_client.h"
#include "wifi_mgr.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "esp_log.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstring>
#include <string>
#include <vector>

static const char *TAG = "tab5_ai_client";

namespace {

struct RequestContext {
    ai_cfg_t cfg;
    std::vector<ai_msg_t> messages;
    ai_response_cb_t on_response{nullptr};
    ai_state_cb_t on_state{nullptr};
    void *user_data{nullptr};
};

struct HttpUserData {
    std::string response_buffer;
};

SemaphoreHandle_t s_mutex = nullptr;
TaskHandle_t s_task_handle = nullptr;
ai_state_t s_state = AI_STATE_IDLE;
bool s_cancel_requested = false;
RequestContext s_req;

void set_state_locked(ai_state_t new_state, const char *status_msg, RequestContext &req)
{
    s_state = new_state;
    ai_state_cb_t cb = req.on_state;
    void *ud = req.user_data;

    if (cb != nullptr) {
        if (bsp_display_lock(pdMS_TO_TICKS(500))) {
            cb(new_state, status_msg, ud);
            bsp_display_unlock();
        }
    }
}

void dispatch_response_locked(const char *text, RequestContext &req)
{
    ai_response_cb_t cb = req.on_response;
    void *ud = req.user_data;

    if (cb != nullptr) {
        if (bsp_display_lock(pdMS_TO_TICKS(500))) {
            cb(text, ud);
            bsp_display_unlock();
        }
    }
}

std::string build_full_url(const std::string &base_url)
{
    std::string url = base_url;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    const std::string suffix = "/chat/completions";
    if (url.length() >= suffix.length() && url.compare(url.length() - suffix.length(), suffix.length(), suffix) == 0) {
        return url;
    }
    return url + suffix;
}

std::string sanitize_llm_text(const std::string &raw)
{
    std::string text = raw;

    // 1. Remove tag <think>...</think> se existir (DeepSeek CoT), preservando o conteúdo caso não haja texto pós-think
    size_t think_start = text.find("<think>");
    if (think_start != std::string::npos) {
        size_t think_end = text.find("</think>", think_start);
        if (think_end != std::string::npos) {
            std::string after = text.substr(think_end + 8);
            bool has_after = false;
            for (char ch : after) {
                if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
                    has_after = true;
                    break;
                }
            }
            if (has_after) {
                text = after;
            } else {
                text = text.substr(think_start + 7, think_end - (think_start + 7));
            }
        } else {
            text.erase(think_start, 7);
        }
    }

    // Trim inicial de quebras de linha e espaços
    while (!text.empty() && (text.front() == '\n' || text.front() == '\r' || text.front() == ' ')) {
        text.erase(text.begin());
    }

    std::string out;
    out.reserve(text.size());

    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];

        // ASCII comum (0x00 .. 0x7F)
        if (c < 0x80) {
            out.push_back(text[i++]);
            continue;
        }

        // 2-byte UTF-8
        if ((c & 0xE0) == 0xC0 && (i + 1 < text.size())) {
            // Latin-1 válido em UTF-8: 0xC2 (0x80..0xBF) e 0xC3 (0x80..0xBF)
            if (c == 0xC2 || c == 0xC3) {
                out.push_back(text[i++]);
                out.push_back(text[i++]);
            } else {
                i += 2;
            }
            continue;
        }

        // 3-byte UTF-8 (como aspas curvas, travessões, reticências, símbolos)
        if ((c & 0xF0) == 0xE0 && (i + 2 < text.size())) {
            unsigned char c2 = (unsigned char)text[i + 1];
            unsigned char c3 = (unsigned char)text[i + 2];

            if (c == 0xE2 && c2 == 0x80) {
                if (c3 == 0x98 || c3 == 0x99 || c3 == 0x9B) {
                    out.push_back('\'');
                    i += 3;
                    continue;
                }
                if (c3 == 0x9C || c3 == 0x9D || c3 == 0x9E || c3 == 0x9F) {
                    out.push_back('"');
                    i += 3;
                    continue;
                }
                if (c3 == 0x93 || c3 == 0x94) {
                    out.push_back('-');
                    i += 3;
                    continue;
                }
                if (c3 == 0xA6) {
                    out.append("...");
                    i += 3;
                    continue;
                }
                if (c3 == 0xA2) {
                    out.append("- ");
                    i += 3;
                    continue;
                } // Bullet •
            } else if (c == 0xE2 && c2 == 0x86 && c3 == 0x92) {
                out.append("->");
                i += 3;
                continue;
            } else if (c == 0xE2 && c2 == 0x86 && c3 == 0x90) {
                out.append("<-");
                i += 3;
                continue;
            }

            // Descarta outro caractere de 3 bytes não suportado
            i += 3;
            continue;
        }

        // 4-byte UTF-8 (emojis como 👋, 🤖, etc.)
        if ((c & 0xF8) == 0xF0 && (i + 3 < text.size())) {
            i += 4;
            continue;
        }

        i++;
    }

    return out;
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    HttpUserData *data = (HttpUserData *)evt->user_data;
    if (data == nullptr) {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (evt->data != nullptr && evt->data_len > 0) {
            data->response_buffer.append((const char *)evt->data, evt->data_len);
        }
    }
    return ESP_OK;
}

void ai_client_task(void *pvParameters)
{
    RequestContext req;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        req = s_req;
        xSemaphoreGive(s_mutex);
    }

    wifi_status_t ws = {};
    wifi_mgr_get_status(&ws);
    if (!ws.connected) {
        ESP_LOGW(TAG, "Wi-Fi nao conectado");
        set_state_locked(AI_STATE_ERROR, "Wi-Fi desconectado. Conecte-se a uma rede primeiro.", req);
        s_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    set_state_locked(AI_STATE_CONNECTING, "Conectando ao gateway de IA...", req);

    std::string full_url = build_full_url(req.cfg.base_url);
    ESP_LOGI(TAG, "Enviando requisicao para %s (modelo: %s, max_tokens: %d)", full_url.c_str(), req.cfg.model,
             req.cfg.max_tokens);

    // Constrói payload JSON
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", req.cfg.model);
    cJSON_AddNumberToObject(root, "max_tokens", req.cfg.max_tokens >= 1024 ? req.cfg.max_tokens : 2048);

    cJSON *messages_arr = cJSON_AddArrayToObject(root, "messages");
    for (const auto &m : req.messages) {
        cJSON *msg_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(msg_obj, "role", m.role.c_str());
        cJSON_AddStringToObject(msg_obj, "content", m.content.c_str());
        cJSON_AddItemToArray(messages_arr, msg_obj);
    }

    char *json_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_body == nullptr) {
        ESP_LOGE(TAG, "Falha ao serializar payload JSON");
        set_state_locked(AI_STATE_ERROR, "Erro ao gerar JSON da mensagem.", req);
        s_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    int timeout_ms = (req.cfg.timeout_sec >= 120 ? req.cfg.timeout_sec : 120) * 1000;
    HttpUserData http_data;

    esp_http_client_config_t config = {};
    config.url = full_url.c_str();
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = timeout_ms;

    config.buffer_size = 8192;
    config.buffer_size_tx = 4096;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = http_event_handler;
    config.user_data = &http_data;

    config.keep_alive_enable = false;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Falha ao inicializar esp_http_client");
        free(json_body);
        set_state_locked(AI_STATE_ERROR, "Erro ao inicializar cliente HTTP.", req);
        s_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Connection", "close");
    if (strlen(req.cfg.token) > 0) {
        std::string auth_hdr = "Bearer " + std::string(req.cfg.token);
        esp_http_client_set_header(client, "Authorization", auth_hdr.c_str());
    }

    ESP_LOGI(TAG, "ai_client_task: url='%s', token_len=%zu, body_len=%zu", full_url.c_str(), strlen(req.cfg.token),
             strlen(json_body));

    esp_http_client_set_post_field(client, json_body, strlen(json_body));

    set_state_locked(AI_STATE_SENDING, "Enviando mensagem...", req);

    esp_err_t perform_err = esp_http_client_perform(client);
    if (perform_err != ESP_OK && !s_cancel_requested) {
        ESP_LOGW(TAG, "Tentativa 1 falhou (%s), reinicializando conexao apos 600ms...", esp_err_to_name(perform_err));
        esp_http_client_cleanup(client);
        client = NULL;
        vTaskDelay(pdMS_TO_TICKS(600));
        http_data.response_buffer.clear();

        client = esp_http_client_init(&config);
        if (client != NULL) {
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_header(client, "Connection", "close");
            if (strlen(req.cfg.token) > 0) {
                std::string auth_hdr = "Bearer " + std::string(req.cfg.token);
                esp_http_client_set_header(client, "Authorization", auth_hdr.c_str());
            }
            esp_http_client_set_post_field(client, json_body, strlen(json_body));
            perform_err = esp_http_client_perform(client);
        }
    }
    free(json_body);

    int status_code = (client != NULL) ? esp_http_client_get_status_code(client) : 0;
    int content_length = (client != NULL) ? esp_http_client_get_content_length(client) : 0;

    ESP_LOGI(TAG, "esp_http_client_perform result: %s (status_code=%d, len=%d, resp_bytes=%zu)",
             esp_err_to_name(perform_err), status_code, content_length, http_data.response_buffer.size());

    if (client != NULL) {
        esp_http_client_cleanup(client);
    }

    if (s_cancel_requested) {
        set_state_locked(AI_STATE_CANCELLED, "Requisicao cancelada.", req);
        s_task_handle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    if (perform_err == ESP_OK && status_code == 200) {
        cJSON *res_root = cJSON_Parse(http_data.response_buffer.c_str());
        if (res_root != nullptr) {
            cJSON *choices = cJSON_GetObjectItem(res_root, "choices");
            cJSON *first_choice =
                (choices != nullptr && cJSON_GetArraySize(choices) > 0) ? cJSON_GetArrayItem(choices, 0) : nullptr;
            cJSON *message = (first_choice != nullptr) ? cJSON_GetObjectItem(first_choice, "message") : nullptr;
            cJSON *content = (message != nullptr) ? cJSON_GetObjectItem(message, "content") : nullptr;

            const char *raw_text = nullptr;
            if (content != nullptr && content->valuestring != nullptr && strlen(content->valuestring) > 0) {
                raw_text = content->valuestring;
            } else {
                cJSON *reasoning = (message != nullptr) ? cJSON_GetObjectItem(message, "reasoning_content") : nullptr;
                if (reasoning != nullptr && reasoning->valuestring != nullptr && strlen(reasoning->valuestring) > 0) {
                    raw_text = reasoning->valuestring;
                }
            }

            if (raw_text != nullptr) {
                std::string clean = sanitize_llm_text(raw_text);
                ESP_LOGI(TAG, "IA resposta original (%zu bytes), sanitizada (%zu bytes): %s", strlen(raw_text),
                         clean.size(), clean.c_str());
                dispatch_response_locked(clean.c_str(), req);
                set_state_locked(AI_STATE_COMPLETED, "Resposta recebida.", req);
            } else {
                ESP_LOGW(TAG, "Estrutura de resposta inesperada: %s", http_data.response_buffer.c_str());
                set_state_locked(AI_STATE_ERROR, "Resposta sem campo de conteudo.", req);
            }

            cJSON_Delete(res_root);
        } else {
            ESP_LOGE(TAG, "Falha ao analisar JSON de resposta: %s", http_data.response_buffer.c_str());
            set_state_locked(AI_STATE_ERROR, "Erro ao decodificar JSON da resposta.", req);
        }
    } else {
        ESP_LOGE(TAG, "Erro HTTP %d / Perform error %s. Resposta: %s", status_code, esp_err_to_name(perform_err),
                 http_data.response_buffer.c_str());

        // Tenta extrair mensagem de erro do JSON do servidor
        std::string server_err_msg;
        if (!http_data.response_buffer.empty()) {
            cJSON *err_root = cJSON_Parse(http_data.response_buffer.c_str());
            if (err_root != nullptr) {
                cJSON *err_obj = cJSON_GetObjectItem(err_root, "error");
                if (err_obj != nullptr) {
                    cJSON *err_msg = cJSON_GetObjectItem(err_obj, "message");
                    if (err_msg != nullptr && err_msg->valuestring != nullptr) {
                        server_err_msg = err_msg->valuestring;
                    }
                }
                cJSON_Delete(err_root);
            }
        }

        if (!server_err_msg.empty()) {
            char full_err[256];
            snprintf(full_err, sizeof(full_err), "%d: %s", status_code, server_err_msg.c_str());
            set_state_locked(AI_STATE_ERROR, full_err, req);
        } else if (status_code == 401) {
            set_state_locked(AI_STATE_ERROR, "Erro 401: Token invalido ou nao autorizado.", req);
        } else if (status_code == 429) {
            set_state_locked(AI_STATE_ERROR, "Erro 429: Limite de requisicoes ou cota excedida.", req);
        } else if (status_code == 404) {
            set_state_locked(AI_STATE_ERROR, "Erro 404: Endpoint /chat/completions nao encontrado.", req);
        } else if (status_code > 0) {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "Erro HTTP %d do gateway.", status_code);
            set_state_locked(AI_STATE_ERROR, err_msg, req);
        } else if (perform_err == ESP_ERR_HTTP_EAGAIN) {
            set_state_locked(AI_STATE_ERROR, "Tempo limite esgotado. O modelo demorou para responder.", req);
        } else if (perform_err == ESP_ERR_HTTP_CONNECT) {
            set_state_locked(AI_STATE_ERROR, "Falha de conexao com o servidor/gateway.", req);
        } else {
            char err_msg[128];
            snprintf(err_msg, sizeof(err_msg), "Falha na requisicao (%s).", esp_err_to_name(perform_err));
            set_state_locked(AI_STATE_ERROR, err_msg, req);
        }
    }

    s_task_handle = nullptr;
    vTaskDelete(NULL);
}

} // namespace

void ai_client_init(void)
{
    if (s_mutex == nullptr) {
        s_mutex = xSemaphoreCreateMutex();
    }
}

esp_err_t ai_client_send(const ai_cfg_t *cfg, const std::vector<ai_msg_t> &messages, ai_response_cb_t on_response,
                         ai_state_cb_t on_state, void *user_data)
{
    ai_client_init();

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_task_handle != nullptr) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE; // Já em execução
    }

    s_cancel_requested = false;
    s_state = AI_STATE_CONNECTING;
    s_req.cfg = *cfg;
    s_req.messages = messages;
    s_req.on_response = on_response;
    s_req.on_state = on_state;
    s_req.user_data = user_data;

    BaseType_t res = xTaskCreate(ai_client_task, "ai_client_task", 24576, NULL, 5, &s_task_handle);
    xSemaphoreGive(s_mutex);

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Falha ao criar task ai_client_task");
        s_state = AI_STATE_ERROR;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void ai_client_cancel(void)
{
    s_cancel_requested = true;
}

ai_state_t ai_client_get_state(void)
{
    return s_state;
}

bool ai_client_is_busy(void)
{
    return s_task_handle != nullptr;
}
