#include "file_assoc.h"
#include "ui_shell.h"
#include <string>
#include <vector>
#include <cstring>
#include <cctype>
#include "esp_log.h"

static const char *TAG = "tab5_file_assoc";

namespace {

struct AssocEntry {
    std::string ext;
    file_open_handler_t handler;
};

std::vector<AssocEntry> s_associations;

std::string normalize_ext(const char *ext)
{
    if (ext == nullptr) {
        return "";
    }
    std::string result = ext;
    if (!result.empty() && result[0] == '.') {
        result = result.substr(1);
    }
    for (char &c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

std::string extract_ext(const char *filepath)
{
    if (filepath == nullptr) {
        return "";
    }
    const char *dot = std::strrchr(filepath, '.');
    if (dot == nullptr || *(dot + 1) == '\0') {
        return "";
    }
    return normalize_ext(dot + 1);
}

} // namespace

void file_assoc_init(void)
{
    s_associations.clear();
    /* Registra o app Notas como handler padrao para arquivos de texto e configuracao */
    file_assoc_register("txt", ui_shell_open_notas_with_file);
    file_assoc_register("cfg", ui_shell_open_notas_with_file);
    /* Registra o app Galeria como handler padrao para imagens */
    file_assoc_register("jpg", ui_shell_open_gallery_with_file);
    file_assoc_register("jpeg", ui_shell_open_gallery_with_file);
    file_assoc_register("png", ui_shell_open_gallery_with_file);
    file_assoc_register("bmp", ui_shell_open_gallery_with_file);
    /* Registra o app Gravador como handler padrao para audios */
    file_assoc_register("wav", ui_shell_open_recorder_with_file);
    file_assoc_register("pcm", ui_shell_open_recorder_with_file);
    ESP_LOGI(TAG, "sistema de associacao inicializado (.txt/.cfg -> Notas, .jpg/.jpeg/.png/.bmp -> Galeria, .wav/.pcm "
                  "-> Gravador)");
}

esp_err_t file_assoc_register(const char *ext, file_open_handler_t handler)
{
    if (ext == nullptr || handler == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    std::string norm = normalize_ext(ext);
    if (norm.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    for (auto &entry : s_associations) {
        if (entry.ext == norm) {
            entry.handler = handler;
            return ESP_OK;
        }
    }

    s_associations.push_back({norm, handler});
    return ESP_OK;
}

esp_err_t file_assoc_open(const char *filepath)
{
    if (filepath == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    std::string ext = extract_ext(filepath);
    if (ext.empty()) {
        ESP_LOGW(TAG, "arquivo sem extensao: %s", filepath);
        return ESP_ERR_NOT_FOUND;
    }

    for (const auto &entry : s_associations) {
        if (entry.ext == ext) {
            ESP_LOGI(TAG, "abrindo arquivo %s com handler da extensao .%s", filepath, ext.c_str());
            entry.handler(filepath);
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "nenhum app associado a extensao .%s", ext.c_str());
    return ESP_ERR_NOT_FOUND;
}
