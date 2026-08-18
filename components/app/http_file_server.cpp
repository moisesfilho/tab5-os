#include "http_file_server.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>

static const char *TAG = "tab5_http_server";
static httpd_handle_t s_server = nullptr;

static esp_err_t index_handler(httpd_req_t *req)
{
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Tab5 OS - Galeria & Arquivos</title>"
         << "<style>body{font-family:sans-serif;margin:30px;background:#1e1e2e;color:#cdd6f4}"
         << "h1{color:#89b4fa}table{width:100%;border-collapse:collapse;margin-top:20px}"
         << "th,td{padding:12px;text-align:left;border-bottom:1px solid #313244}"
         << "a{color:#f5c2e7;text-decoration:none;font-weight:bold}a:hover{text-decoration:underline}"
         << ".card{background:#181825;padding:20px;border-radius:12px;box-shadow:0 4px 6px rgba(0,0,0,0.3)}"
         << "</style></head><body><div class='card'>"
         << "<h1>📸 M5Stack Tab5 - Fotos & Arquivos</h1>"
         << "<p>Acesse ou baixe diretamente as fotos capturadas pela câmera:</p>"
         << "<table><tr><th>Arquivo</th><th>Tamanho</th><th>Download</th></tr>";

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
                 << "<td>" << (sz / 1024) << " KB (" << sz << " B)</td>"
                 << "<td><a href='/download?file=/sdcard/imagens/" << entry->d_name
                 << "' target='_blank'>Baixar / Visualizar</a></td></tr>";
        }
        closedir(d);
    }

    /* Também lista fotos na raiz do SD */
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
                     << "<td><a href='/download?file=/sdcard/" << entry->d_name
                     << "' target='_blank'>Baixar / Visualizar</a></td></tr>";
            }
        }
        closedir(d2);
    }

    html << "</table></div></body></html>";
    std::string response = html.str();

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, response.c_str(), response.size());
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
    size_t read_bytes;
    while ((read_bytes = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
            fclose(fp);
            return ESP_FAIL;
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
    config.max_uri_handlers = 8;
    config.stack_size = 8192;

    ESP_LOGI(TAG, "iniciando servidor HTTP de fotos na porta %d...", config.server_port);
    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "falha ao iniciar http server (%s)", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t uri_index = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(s_server, &uri_index);

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
