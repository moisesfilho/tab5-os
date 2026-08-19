#include "http_file_server.h"
#include "ai_storage.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>

static const char *TAG = "tab5_http_server";
static httpd_handle_t s_server = nullptr;

static std::string url_decode(const std::string &src)
{
    std::string ret;
    for (size_t i = 0; i < src.length(); i++) {
        if (src[i] == '%' && i + 2 < src.length()) {
            int ii = 0;
            if (sscanf(src.substr(i + 1, 2).c_str(), "%x", &ii) == 1) {
                ret += static_cast<char>(ii);
                i += 2;
            } else {
                ret += src[i];
            }
        } else if (src[i] == '+') {
            ret += ' ';
        } else {
            ret += src[i];
        }
    }
    return ret;
}

static void parse_form_urlencoded(const std::string &body, ai_cfg_t &cfg)
{
    std::istringstream stream(body);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq != std::string::npos) {
            std::string key = url_decode(pair.substr(0, eq));
            std::string val = url_decode(pair.substr(eq + 1));
            if (key == "base_url") {
                snprintf(cfg.base_url, sizeof(cfg.base_url), "%s", val.c_str());
            } else if (key == "token") {
                snprintf(cfg.token, sizeof(cfg.token), "%s", val.c_str());
            } else if (key == "model") {
                snprintf(cfg.model, sizeof(cfg.model), "%s", val.c_str());
            } else if (key == "max_tokens") {
                int mt = atoi(val.c_str());
                if (mt > 0 && mt <= 8192) {
                    cfg.max_tokens = mt;
                }
            } else if (key == "timeout_sec") {
                int to = atoi(val.c_str());
                if (to >= 5 && to <= 300) {
                    cfg.timeout_sec = to;
                }
            }
        }
    }
}

static esp_err_t index_handler(httpd_req_t *req)
{
    char query[128] = {0};
    bool saved_alert = false;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16] = {0};
        if (httpd_query_key_value(query, "saved", val, sizeof(val)) == ESP_OK && strcmp(val, "1") == 0) {
            saved_alert = true;
        }
    }

    ai_cfg_t ai_cfg;
    ai_storage_load(&ai_cfg);

    std::ostringstream html;
    html
        << "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='utf-8'>"
        << "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        << "<title>M5Stack Tab5 - Painel Web</title>"
        << "<style>"
        << "*{box-sizing:border-box;margin:0;padding:0}"
        << "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;"
        << "background:#11111b;color:#cdd6f4;padding:24px 16px;line-height:1.5}"
        << ".container{max-width:860px;margin:0 auto}"
        << "header{margin-bottom:24px;text-align:center}"
        << "h1{font-size:1.8rem;color:#89b4fa;margin-bottom:6px}"
        << "p.subtitle{color:#a6adc8;font-size:0.95rem}"
        << ".alert{background:#1e382b;border:1px solid #a6e3a1;color:#a6e3a1;padding:12px 16px;"
        << "border-radius:10px;margin-bottom:20px;display:flex;align-items:center;font-weight:600}"
        << ".tabs{display:flex;gap:8px;margin-bottom:20px;border-bottom:1px solid #313244;padding-bottom:10px}"
        << ".tab-btn{background:#181825;color:#cdd6f4;border:1px solid #313244;padding:10px 18px;"
        << "border-radius:8px;font-size:0.95rem;font-weight:600;cursor:pointer;transition:all .2s}"
        << ".tab-btn.active{background:#89b4fa;color:#11111b;border-color:#89b4fa}"
        << ".tab-btn:hover:not(.active){background:#313244}"
        << ".card{background:#181825;border:1px solid #313244;border-radius:14px;padding:24px;box-shadow:0 8px 24px "
           "rgba(0,0,0,0.35);margin-bottom:24px}"
        << ".card h2{font-size:1.25rem;color:#cdd6f4;margin-bottom:16px;display:flex;align-items:center;gap:8px}"
        << ".form-group{margin-bottom:18px}"
        << "label{display:block;font-size:0.9rem;font-weight:600;color:#bac2de;margin-bottom:6px}"
        << "input[type='text'],input[type='password'],input[type='number']{"
        << "width:100%;padding:12px 14px;background:#1e1e2e;border:1px solid #45475a;border-radius:8px;"
        << "color:#cdd6f4;font-size:0.95rem;transition:border-color .2s;outline:none}"
        << "input:focus{border-color:#89b4fa}"
        << ".help{font-size:0.8rem;color:#9399b2;margin-top:4px}"
        << ".btn-row{display:flex;gap:12px;margin-top:24px;flex-wrap:wrap}"
        << ".btn{padding:12px "
           "24px;border-radius:8px;font-size:0.95rem;font-weight:600;cursor:pointer;border:none;transition:opacity .2s}"
        << ".btn:hover{opacity:0.9}"
        << ".btn-primary{background:#89b4fa;color:#11111b}"
        << ".btn-secondary{background:#313244;color:#cdd6f4}"
        << ".presets{display:flex;gap:8px;margin-bottom:18px;flex-wrap:wrap}"
        << ".preset-btn{background:#313244;color:#cdd6f4;border:none;padding:6px "
           "12px;border-radius:6px;font-size:0.8rem;cursor:pointer}"
        << ".preset-btn:hover{background:#45475a}"
        << "table{width:100%;border-collapse:collapse;margin-top:12px}"
        << "th,td{padding:12px;text-align:left;border-bottom:1px solid #313244;font-size:0.9rem}"
        << "th{color:#9399b2;font-weight:600}"
        << "a.dl-link{color:#f5c2e7;text-decoration:none;font-weight:600;padding:6px "
           "12px;background:#313244;border-radius:6px;display:inline-block}"
        << "a.dl-link:hover{background:#45475a;text-decoration:underline}"
        << ".token-wrap{position:relative;display:flex;align-items:center}"
        << ".token-wrap input{padding-right:80px}"
        << ".token-toggle{position:absolute;right:8px;background:none;border:none;color:#89b4fa;font-size:0.8rem;font-"
           "weight:600;cursor:pointer;padding:6px}"
        << "</style></head><body><div class='container'>"
        << "<header><h1>⚡ M5Stack Tab5 OS</h1><p class='subtitle'>Painel de Controle e Configuração</p></header>";

    if (saved_alert) {
        html << "<div class='alert'>✓ Configurações do Chat IA salvas com sucesso no cartão SD!</div>";
    }

    html << "<div class='tabs'>"
         << "<button class='tab-btn active' onclick='showTab(\"tab-ai\", this)'>🤖 Configuração Chat IA</button>"
         << "<button class='tab-btn' onclick='showTab(\"tab-files\", this)'>📸 Galeria & Arquivos</button>"
         << "</div>";

    // Tab 1: AI Settings
    html
        << "<div id='tab-ai' class='tab-content'>"
        << "<div class='card'>"
        << "<h2>🤖 Parâmetros da API OpenAI-compatível</h2>"
        << "<p style='color:#a6adc8;margin-bottom:16px;font-size:0.9rem'>Configure os dados de acesso ao modelo de IA "
           "(OpenCode Go, OpenAI, OpenRouter, etc.).</p>"
        << "<div class='presets'><span style='font-size:0.8rem;color:#9399b2;line-height:26px'>Atalhos rápidos: </span>"
        << "<button type='button' class='preset-btn' onclick='setPreset(\"https://opencode.ai/zen/go/v1\", "
           "\"deepseek-v4-pro\")'>OpenCode Go (DeepSeek-v4)</button>"
        << "<button type='button' class='preset-btn' onclick='setPreset(\"https://api.openai.com/v1\", "
           "\"gpt-4o-mini\")'>OpenAI (GPT-4o mini)</button>"
        << "<button type='button' class='preset-btn' onclick='setPreset(\"https://openrouter.ai/api/v1\", "
           "\"deepseek/deepseek-chat\")'>OpenRouter</button>"
        << "</div>"
        << "<form method='POST' action='/ai/save'>"
        << "<div class='form-group'>"
        << "<label for='base_url'>URL Base da API (Endpoint):</label>"
        << "<input type='text' id='base_url' name='base_url' value='" << ai_cfg.base_url << "' required>"
        << "<div class='help'>Exemplo: https://opencode.ai/zen/go/v1 ou https://api.openai.com/v1</div>"
        << "</div>"
        << "<div class='form-group'>"
        << "<label for='token'>Token de Autenticação (Bearer Token):</label>"
        << "<div class='token-wrap'>"
        << "<input type='password' id='token' name='token' value='" << ai_cfg.token << "' placeholder='sk-...'>"
        << "<button type='button' class='token-toggle' onclick='toggleToken()'>Mostrar</button>"
        << "</div>"
        << "<div class='help'>Chave de API enviada no cabeçalho Authorization: Bearer &lt;token&gt;</div>"
        << "</div>"
        << "<div class='form-group'>"
        << "<label for='model'>Modelo de Linguagem (LLM):</label>"
        << "<input type='text' id='model' name='model' value='" << ai_cfg.model << "' required>"
        << "<div class='help'>Exemplo: deepseek-v4-pro, deepseek-v4-flash, gpt-4o-mini, kimi-k2.6</div>"
        << "</div>"
        << "<div style='display:grid;grid-template-columns:1fr 1fr;gap:16px'>"
        << "<div class='form-group'>"
        << "<label for='max_tokens'>Max Tokens:</label>"
        << "<input type='number' id='max_tokens' name='max_tokens' value='" << ai_cfg.max_tokens
        << "' min='64' max='8192'>"
        << "<div class='help'>Limite de tokens na resposta (padrão: 2048)</div>"
        << "</div>"
        << "<div class='form-group'>"
        << "<label for='timeout_sec'>Timeout (segundos):</label>"
        << "<input type='number' id='timeout_sec' name='timeout_sec' value='" << ai_cfg.timeout_sec
        << "' min='5' max='300'>"
        << "<div class='help'>Tempo limite de espera (padrão: 120s)</div>"

        << "</div>"
        << "</div>"
        << "<div class='btn-row'>"
        << "<button type='submit' class='btn btn-primary'>💾 Salvar Configurações</button>"
        << "<button type='button' class='btn btn-secondary' onclick='resetDefaults()'>↺ Restaurar Padrões</button>"
        << "</div>"
        << "</form>"
        << "</div>"
        << "</div>";

    // Tab 2: Files & Gallery
    html << "<div id='tab-files' class='tab-content' style='display:none'>"
         << "<div class='card'>"
         << "<h2>📸 Fotos & Arquivos Gravados</h2>"
         << "<p style='color:#a6adc8;margin-bottom:16px;font-size:0.9rem'>Baixe as fotos e mídias salvas no cartão "
            "microSD do Tab5:</p>"
         << "<table><tr><th>Arquivo</th><th>Tamanho</th><th>Ação</th></tr>";

    DIR *d = opendir("/sdcard/imagens");
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != nullptr) {
            if (entry->d_name[0] == '.')
                continue;
            std::string path = std::string("/sdcard/imagens/") + entry->d_name;
            struct stat st;
            size_t sz = 0;
            if (stat(path.c_str(), &st) == 0)
                sz = st.st_size;
            html << "<tr><td>" << entry->d_name << "</td>"
                 << "<td>" << (sz / 1024) << " KB</td>"
                 << "<td><a class='dl-link' href='/download?file=/sdcard/imagens/" << entry->d_name
                 << "' target='_blank'>⬇ Baixar</a></td></tr>";
        }
        closedir(d);
    }

    DIR *d2 = opendir("/sdcard");
    if (d2) {
        struct dirent *entry;
        while ((entry = readdir(d2)) != nullptr) {
            if (entry->d_name[0] == '.')
                continue;
            const char *dot = strrchr(entry->d_name, '.');
            if (dot &&
                (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".png") == 0)) {
                std::string path = std::string("/sdcard/") + entry->d_name;
                struct stat st;
                size_t sz = 0;
                if (stat(path.c_str(), &st) == 0)
                    sz = st.st_size;
                html << "<tr><td>" << entry->d_name << " (raiz)</td>"
                     << "<td>" << (sz / 1024) << " KB</td>"
                     << "<td><a class='dl-link' href='/download?file=/sdcard/" << entry->d_name
                     << "' target='_blank'>⬇ Baixar</a></td></tr>";
            }
        }
        closedir(d2);
    }

    html << "</table></div></div>";

    // JavaScript
    html << "<script>"
         << "function showTab(tabId, btn){"
         << "document.querySelectorAll('.tab-content').forEach(el=>el.style.display='none');"
         << "document.querySelectorAll('.tab-btn').forEach(el=>el.classList.remove('active'));"
         << "document.getElementById(tabId).style.display='block';"
         << "btn.classList.add('active');"
         << "}"
         << "function toggleToken(){"
         << "var t=document.getElementById('token');"
         << "if(t.type==='password'){t.type='text';event.target.innerText='Ocultar';}"
         << "else{t.type='password';event.target.innerText='Mostrar';}"
         << "}"
         << "function setPreset(url, model){"
         << "document.getElementById('base_url').value=url;"
         << "document.getElementById('model').value=model;"
         << "}"
         << "function resetDefaults(){"
         << "if(confirm('Restaurar parâmetros padrão do OpenCode Go?')){"
         << "setPreset('https://opencode.ai/zen/go/v1', 'deepseek-v4-pro');"
         << "document.getElementById('max_tokens').value=512;"
         << "document.getElementById('timeout_sec').value=30;"
         << "}}"
         << "</script>";

    html << "</div></body></html>";
    std::string response = html.str();

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, response.c_str(), response.size());
    return ESP_OK;
}

static esp_err_t ai_save_handler(httpd_req_t *req)
{
    char buf[1024];
    int total_len = req->content_len;
    int cur_len = 0;
    int received = 0;

    if (total_len >= (int)sizeof(buf)) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';

    ai_cfg_t cfg;
    ai_storage_load(&cfg);

    // Se for JSON ou Form URL-encoded
    if (buf[0] == '{') {
        cJSON *root = cJSON_Parse(buf);
        if (root) {
            cJSON *json_url = cJSON_GetObjectItem(root, "base_url");
            cJSON *json_token = cJSON_GetObjectItem(root, "token");
            cJSON *json_model = cJSON_GetObjectItem(root, "model");
            cJSON *json_max_tokens = cJSON_GetObjectItem(root, "max_tokens");
            cJSON *json_timeout = cJSON_GetObjectItem(root, "timeout_sec");
            if (json_url && json_url->valuestring)
                snprintf(cfg.base_url, sizeof(cfg.base_url), "%s", json_url->valuestring);
            if (json_token && json_token->valuestring)
                snprintf(cfg.token, sizeof(cfg.token), "%s", json_token->valuestring);
            if (json_model && json_model->valuestring)
                snprintf(cfg.model, sizeof(cfg.model), "%s", json_model->valuestring);
            if (json_max_tokens && json_max_tokens->valueint > 0)
                cfg.max_tokens = json_max_tokens->valueint;
            if (json_timeout && json_timeout->valueint > 0)
                cfg.timeout_sec = json_timeout->valueint;
            cJSON_Delete(root);
        }
    } else {

        parse_form_urlencoded(buf, cfg);
    }

    ai_storage_save(&cfg);
    ESP_LOGI(TAG, "Configuracao de IA atualizada via web server (base_url=%s, model=%s)", cfg.base_url, cfg.model);

    // Redireciona de volta para a página principal com parâmetro saved=1
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/?saved=1");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t api_ai_get_handler(httpd_req_t *req)
{
    ai_cfg_t cfg;
    ai_storage_load(&cfg);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "base_url", cfg.base_url);
    cJSON_AddStringToObject(root, "token", cfg.token);
    cJSON_AddStringToObject(root, "model", cfg.model);
    cJSON_AddNumberToObject(root, "max_tokens", cfg.max_tokens);
    cJSON_AddNumberToObject(root, "timeout_sec", cfg.timeout_sec);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return ESP_OK;
}

static esp_err_t download_handler(httpd_req_t *req)
{
    char filepath[256] = {0};
    char query[256] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "file", filepath, sizeof(filepath));
    }

    if (filepath[0] == '\0') {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "arquivo nao encontrado: %s", filepath);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    const char *dot = strrchr(filepath, '.');
    if (dot && (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)) {
        httpd_resp_set_type(req, "image/jpeg");
    } else if (dot && strcasecmp(dot, ".png") == 0) {
        httpd_resp_set_type(req, "image/png");
    } else {
        httpd_resp_set_type(req, "application/octet-stream");
    }

    char chunk[2048];
    while (!feof(fp) && !ferror(fp)) {
        size_t read_bytes = fread(chunk, 1, sizeof(chunk), fp);
        if (read_bytes > 0) {
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                fclose(fp);
                return ESP_FAIL;
            }
        }
        if (read_bytes < sizeof(chunk)) {
            break;
        }
    }
    fclose(fp);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t http_file_server_start(void)
{
    if (s_server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.max_uri_handlers = 12;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "iniciando servidor HTTP (painel web e fotos) na porta %d...", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "falha ao iniciar http server (%s)", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t uri_index = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_index);

    httpd_uri_t uri_ai_save = {.uri = "/ai/save", .method = HTTP_POST, .handler = ai_save_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_ai_save);

    httpd_uri_t uri_api_ai = {.uri = "/api/ai", .method = HTTP_GET, .handler = api_ai_get_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_api_ai);

    httpd_uri_t uri_download = {
        .uri = "/download", .method = HTTP_GET, .handler = download_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_download);

    ESP_LOGI(TAG, "servidor HTTP ativo em http://<IP>:8080/");
    return ESP_OK;
}

esp_err_t http_file_server_stop(void)
{
    if (s_server == nullptr) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "parando servidor HTTP...");
    esp_err_t ret = httpd_stop(s_server);
    s_server = nullptr;
    return ret;
}

bool http_file_server_is_running(void)
{
    return s_server != nullptr;
}

uint16_t http_file_server_get_port(void)
{
    return 8080;
}
