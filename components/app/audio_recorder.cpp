#include "audio_recorder.h"
#include "timezone_mgr.h"
#include "bsp/m5stack_tab5.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "wifi_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static const char *TAG = "tab5_audio_recorder";

#define DEFAULT_SAMPLE_RATE 16000
#define DEFAULT_CHANNELS 1
#define DEFAULT_BITS_PER_SAMPLE 16
#define MAX_RECORDING_SECONDS 300 // 5 minutos maximo
#define AUDIO_BUFFER_SIZE 4096
#define RECORD_DIR "/sdcard/gravacoes"

#pragma pack(push, 1)
typedef struct {
    char riff_id[4];        // "RIFF"
    uint32_t riff_size;     // Tamanho total do arquivo - 8
    char wave_id[4];        // "WAVE"
    char fmt_id[4];         // "fmt "
    uint32_t fmt_size;      // 16 para PCM linear
    uint16_t audio_format;  // 1 para PCM
    uint16_t num_channels;  // 1 (mono) ou 2 (stereo)
    uint32_t sample_rate;   // ex: 16000
    uint32_t byte_rate;     // sample_rate * num_channels * bits/8
    uint16_t block_align;   // num_channels * bits/8
    uint16_t bits_per_samp; // 16
    char data_id[4];        // "data"
    uint32_t data_size;     // Tamanho dos dados de audio PCM
} wav_header_t;
#pragma pack(pop)

namespace {

SemaphoreHandle_t s_audio_mutex = nullptr;
esp_codec_dev_handle_t s_mic_dev = nullptr;
esp_codec_dev_handle_t s_spk_dev = nullptr;

audio_recorder_status_t s_status = {
    .state = AUDIO_RECORDER_STATE_IDLE,
    .current_time_sec = 0,
    .total_time_sec = 0,
    .current_filepath = {0},
};

TaskHandle_t s_task_handle = nullptr;
volatile bool s_stop_requested = false;
int s_current_volume = 80;

void ensure_record_dir_exists(void)
{
    struct stat st;
    if (stat(RECORD_DIR, &st) != 0) {
        wifi_storage_mount();
        mkdir(RECORD_DIR, 0755);
    }
}

void build_recording_filename(char *out, size_t out_len)
{
    ensure_record_dir_exists();

    struct tm tm_info;
    timezone_mgr_get_localtime(&tm_info);

    if (tm_info.tm_year < 120) { // Ano anterior a 2020 (RTC nao inicializado)
        snprintf(out, out_len, "%s/REC_%04d.wav", RECORD_DIR, (int)esp_log_timestamp());
    } else {
        snprintf(out, out_len, "%s/REC_%04d%02d%02d_%02d%02d%02d.wav", RECORD_DIR, tm_info.tm_year + 1900,
                 tm_info.tm_mon + 1, tm_info.tm_mday, tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    }
}

void record_task(void *param)
{
    char filepath[256];
    strncpy(filepath, (const char *)param, sizeof(filepath) - 1);
    filepath[sizeof(filepath) - 1] = '\0';
    free(param);

    ESP_LOGI(TAG, "Iniciando gravacao em: %s", filepath);

    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        // Tenta montar o storage caso nao esteja montado
        wifi_storage_mount();
        ensure_record_dir_exists();
        fp = fopen(filepath, "wb");
    }

    if (!fp) {
        ESP_LOGE(TAG, "Falha ao criar arquivo de audio: %s", filepath);
        xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
        s_status.state = AUDIO_RECORDER_STATE_IDLE;
        s_task_handle = nullptr;
        xSemaphoreGive(s_audio_mutex);
        vTaskDelete(nullptr);
        return;
    }

    /* Escreve cabecalho placeholder de 44 bytes */
    wav_header_t header;
    memset(&header, 0, sizeof(header));
    memcpy(header.riff_id, "RIFF", 4);
    memcpy(header.wave_id, "WAVE", 4);
    memcpy(header.fmt_id, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;
    header.num_channels = DEFAULT_CHANNELS;
    header.sample_rate = DEFAULT_SAMPLE_RATE;
    header.bits_per_samp = DEFAULT_BITS_PER_SAMPLE;
    header.byte_rate = DEFAULT_SAMPLE_RATE * DEFAULT_CHANNELS * (DEFAULT_BITS_PER_SAMPLE / 8);
    header.block_align = DEFAULT_CHANNELS * (DEFAULT_BITS_PER_SAMPLE / 8);
    memcpy(header.data_id, "data", 4);
    header.data_size = 0;
    header.riff_size = sizeof(wav_header_t) - 8;

    fwrite(&header, 1, sizeof(header), fp);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = static_cast<uint8_t>(DEFAULT_BITS_PER_SAMPLE),
        .channel = static_cast<uint8_t>(DEFAULT_CHANNELS),
        .channel_mask = 0,
        .sample_rate = DEFAULT_SAMPLE_RATE,
        .mclk_multiple = 0,
    };

    int open_ret = esp_codec_dev_open(s_mic_dev, &fs);
    if (open_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Falha ao abrir stream do microfone (erro=%d)", open_ret);
        fclose(fp);
        unlink(filepath);
        xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
        s_status.state = AUDIO_RECORDER_STATE_IDLE;
        s_task_handle = nullptr;
        xSemaphoreGive(s_audio_mutex);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t *rec_buf = (uint8_t *)heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rec_buf) {
        rec_buf = (uint8_t *)malloc(AUDIO_BUFFER_SIZE);
    }

    if (!rec_buf) {
        ESP_LOGE(TAG, "Falha ao alocar buffer de gravacao");
        esp_codec_dev_close(s_mic_dev);
        fclose(fp);
        unlink(filepath);
        xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
        s_status.state = AUDIO_RECORDER_STATE_IDLE;
        s_task_handle = nullptr;
        xSemaphoreGive(s_audio_mutex);
        vTaskDelete(nullptr);
        return;
    }

    uint32_t total_pcm_bytes = 0;
    const uint32_t max_bytes = MAX_RECORDING_SECONDS * header.byte_rate;

    ESP_LOGI(TAG, "Gravando PCM... byte_rate=%u max_bytes=%u", (unsigned)header.byte_rate, (unsigned)max_bytes);

    while (!s_stop_requested && total_pcm_bytes < max_bytes) {
        int ret = esp_codec_dev_read(s_mic_dev, rec_buf, AUDIO_BUFFER_SIZE);
        if (ret == ESP_CODEC_DEV_OK) {
            size_t written = fwrite(rec_buf, 1, AUDIO_BUFFER_SIZE, fp);
            if (written > 0) {
                total_pcm_bytes += written;
            }
            xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
            s_status.current_time_sec = total_pcm_bytes / header.byte_rate;
            xSemaphoreGive(s_audio_mutex);
        } else {
            ESP_LOGW(TAG, "Leitura do codec retornou: %d", ret);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    free(rec_buf);
    esp_codec_dev_close(s_mic_dev);

    /* Atualiza o cabecalho com os tamanhos reais gravados */
    header.data_size = total_pcm_bytes;
    header.riff_size = sizeof(wav_header_t) - 8 + total_pcm_bytes;
    fseek(fp, 0, SEEK_SET);
    fwrite(&header, 1, sizeof(header), fp);
    fclose(fp);

    ESP_LOGI(TAG, "Gravacao finalizada com sucesso: %u bytes (%u seg)", (unsigned)total_pcm_bytes,
             (unsigned)(total_pcm_bytes / header.byte_rate));

    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    s_status.state = AUDIO_RECORDER_STATE_IDLE;
    s_status.current_time_sec = total_pcm_bytes / header.byte_rate;
    s_status.total_time_sec = s_status.current_time_sec;
    s_task_handle = nullptr;
    xSemaphoreGive(s_audio_mutex);

    vTaskDelete(nullptr);
}

void play_task(void *param)
{
    char filepath[256];
    strncpy(filepath, (const char *)param, sizeof(filepath) - 1);
    filepath[sizeof(filepath) - 1] = '\0';
    free(param);

    ESP_LOGI(TAG, "Iniciando reproducao de: %s", filepath);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        ESP_LOGE(TAG, "Falha ao abrir arquivo para reproducao: %s", filepath);
        xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
        s_status.state = AUDIO_RECORDER_STATE_IDLE;
        s_task_handle = nullptr;
        xSemaphoreGive(s_audio_mutex);
        vTaskDelete(nullptr);
        return;
    }

    /* Leitura do cabecalho WAV */
    wav_header_t header;
    bool valid_wav = false;
    if (fread(&header, 1, sizeof(header), fp) == sizeof(header)) {
        if (memcmp(header.riff_id, "RIFF", 4) == 0 && memcmp(header.wave_id, "WAVE", 4) == 0 &&
            memcmp(header.fmt_id, "fmt ", 4) == 0) {
            valid_wav = true;
        }
    }

    uint32_t sample_rate = DEFAULT_SAMPLE_RATE;
    uint16_t channels = DEFAULT_CHANNELS;
    uint16_t bits = DEFAULT_BITS_PER_SAMPLE;
    uint32_t data_bytes = 0;

    if (valid_wav) {
        sample_rate = header.sample_rate;
        channels = header.num_channels;
        bits = header.bits_per_samp;
        data_bytes = header.data_size;
        clearerr(fp);
        ESP_LOGI(TAG, "Header WAV valido: sr=%u, ch=%u, bits=%u, data_bytes=%u", (unsigned)sample_rate,
                 (unsigned)channels, (unsigned)bits, (unsigned)data_bytes);
    } else {
        clearerr(fp);
        if (fseek(fp, 0, SEEK_END) == 0) {
            long sz = ftell(fp);
            data_bytes = (sz > 0) ? (uint32_t)sz : 0;
            fseek(fp, 0, SEEK_SET);
        }
        clearerr(fp);
        ESP_LOGW(TAG, "Header WAV ausente, assumindo PCM cru: %u bytes", (unsigned)data_bytes);
    }

    uint32_t byte_rate = sample_rate * channels * (bits / 8);
    if (byte_rate == 0) {
        byte_rate = DEFAULT_SAMPLE_RATE * DEFAULT_CHANNELS * (DEFAULT_BITS_PER_SAMPLE / 8);
    }

    uint32_t total_sec = (byte_rate > 0) ? (data_bytes / byte_rate) : 0;

    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    s_status.total_time_sec = total_sec;
    s_status.current_time_sec = 0;
    xSemaphoreGive(s_audio_mutex);

    esp_codec_dev_set_out_vol(s_spk_dev, s_current_volume);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = static_cast<uint8_t>(bits),
        .channel = static_cast<uint8_t>(channels),
        .channel_mask = 0,
        .sample_rate = sample_rate,
        .mclk_multiple = 0,
    };

    int open_ret = esp_codec_dev_open(s_spk_dev, &fs);
    if (open_ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Falha ao abrir stream do speaker (erro=%d)", open_ret);
        fclose(fp);
        xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
        s_status.state = AUDIO_RECORDER_STATE_IDLE;
        s_task_handle = nullptr;
        xSemaphoreGive(s_audio_mutex);
        vTaskDelete(nullptr);
        return;
    }

    uint8_t *play_buf = (uint8_t *)heap_caps_malloc(AUDIO_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!play_buf) {
        play_buf = (uint8_t *)malloc(AUDIO_BUFFER_SIZE);
    }

    if (!play_buf) {
        ESP_LOGE(TAG, "Falha ao alocar buffer de reproducao");
        esp_codec_dev_close(s_spk_dev);
        fclose(fp);
        xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
        s_status.state = AUDIO_RECORDER_STATE_IDLE;
        s_task_handle = nullptr;
        xSemaphoreGive(s_audio_mutex);
        vTaskDelete(nullptr);
        return;
    }

    uint32_t bytes_played = 0;

    while (!s_stop_requested && (data_bytes == 0 || bytes_played < data_bytes)) {
        if (s_status.state == AUDIO_RECORDER_STATE_PAUSED) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (feof(fp) || ferror(fp)) {
            break;
        }

        size_t to_read = AUDIO_BUFFER_SIZE;
        if (data_bytes > 0 && (data_bytes - bytes_played) < to_read) {
            to_read = data_bytes - bytes_played;
        }

        size_t bytes_read = fread(play_buf, 1, to_read, fp);
        if (bytes_read == 0) {
            break; // Fim do arquivo
        }

        int written = esp_codec_dev_write(s_spk_dev, play_buf, bytes_read);
        if (written == ESP_CODEC_DEV_OK) {
            bytes_played += bytes_read;
            xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
            s_status.current_time_sec = bytes_played / byte_rate;
            xSemaphoreGive(s_audio_mutex);
        } else {
            ESP_LOGW(TAG, "esp_codec_dev_write retornou: %d", written);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    free(play_buf);
    esp_codec_dev_close(s_spk_dev);
    fclose(fp);

    ESP_LOGI(TAG, "Reproducao finalizada com sucesso: %u bytes", (unsigned)bytes_played);

    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    s_status.state = AUDIO_RECORDER_STATE_IDLE;
    s_status.current_time_sec = 0;
    s_task_handle = nullptr;
    xSemaphoreGive(s_audio_mutex);

    vTaskDelete(nullptr);
}

} // namespace

esp_err_t audio_recorder_init(void)
{
    if (s_audio_mutex == nullptr) {
        s_audio_mutex = xSemaphoreCreateMutex();
        if (!s_audio_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_mic_dev == nullptr) {
        s_mic_dev = bsp_audio_codec_microphone_init();
        if (s_mic_dev) {
            esp_codec_dev_set_in_gain(s_mic_dev, 42.0);
            ESP_LOGI(TAG, "Codec de microfone ES7210 inicializado");
        } else {
            ESP_LOGW(TAG, "Nao foi possivel inicializar codec de microfone");
        }
    }

    if (s_spk_dev == nullptr) {
        s_spk_dev = bsp_audio_codec_speaker_init();
        if (s_spk_dev) {
            esp_codec_dev_set_out_vol(s_spk_dev, s_current_volume);
            ESP_LOGI(TAG, "Codec de speaker ES8388 inicializado");
        } else {
            ESP_LOGW(TAG, "Nao foi possivel inicializar codec de speaker");
        }
    }

    return ESP_OK;
}

esp_err_t audio_recorder_start_recording(char *out_filepath, size_t out_len)
{
    if (audio_recorder_init() != ESP_OK || s_mic_dev == nullptr) {
        ESP_LOGE(TAG, "Microfone nao disponivel");
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    if (s_status.state != AUDIO_RECORDER_STATE_IDLE) {
        xSemaphoreGive(s_audio_mutex);
        ESP_LOGW(TAG, "Gravador ou player ocupado (estado=%d)", (int)s_status.state);
        return ESP_ERR_INVALID_STATE;
    }

    char filepath[256];
    build_recording_filename(filepath, sizeof(filepath));

    if (out_filepath != nullptr && out_len > 0) {
        strncpy(out_filepath, filepath, out_len - 1);
        out_filepath[out_len - 1] = '\0';
    }

    strncpy(s_status.current_filepath, filepath, sizeof(s_status.current_filepath) - 1);
    s_status.current_filepath[sizeof(s_status.current_filepath) - 1] = '\0';
    s_status.state = AUDIO_RECORDER_STATE_RECORDING;
    s_status.current_time_sec = 0;
    s_status.total_time_sec = MAX_RECORDING_SECONDS;
    s_stop_requested = false;

    char *task_arg = strdup(filepath);
    BaseType_t res = xTaskCreatePinnedToCore(record_task, "audio_rec_task", 8192, task_arg, 5, &s_task_handle, 1);
    if (res != pdPASS) {
        s_status.state = AUDIO_RECORDER_STATE_IDLE;
        free(task_arg);
        xSemaphoreGive(s_audio_mutex);
        ESP_LOGE(TAG, "Falha ao criar task de gravacao");
        return ESP_FAIL;
    }

    xSemaphoreGive(s_audio_mutex);
    return ESP_OK;
}

esp_err_t audio_recorder_stop_recording(void)
{
    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    if (s_status.state != AUDIO_RECORDER_STATE_RECORDING) {
        xSemaphoreGive(s_audio_mutex);
        return ESP_OK;
    }

    s_stop_requested = true;
    xSemaphoreGive(s_audio_mutex);

    // Aguarda a finalizacao da task
    while (s_task_handle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

esp_err_t audio_recorder_start_playback(const char *filepath)
{
    if (filepath == nullptr || strlen(filepath) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (audio_recorder_init() != ESP_OK || s_spk_dev == nullptr) {
        ESP_LOGE(TAG, "Speaker nao disponivel");
        return ESP_ERR_NOT_SUPPORTED;
    }

    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    if (s_status.state != AUDIO_RECORDER_STATE_IDLE) {
        xSemaphoreGive(s_audio_mutex);
        // Para qualquer operacao em andamento antes de iniciar a nova
        if (s_status.state == AUDIO_RECORDER_STATE_PLAYING || s_status.state == AUDIO_RECORDER_STATE_PAUSED) {
            audio_recorder_stop_playback();
        } else if (s_status.state == AUDIO_RECORDER_STATE_RECORDING) {
            audio_recorder_stop_recording();
        }
        xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    }

    strncpy(s_status.current_filepath, filepath, sizeof(s_status.current_filepath) - 1);
    s_status.current_filepath[sizeof(s_status.current_filepath) - 1] = '\0';
    s_status.state = AUDIO_RECORDER_STATE_PLAYING;
    s_status.current_time_sec = 0;
    s_status.total_time_sec = 0;
    s_stop_requested = false;

    char *task_arg = strdup(filepath);
    BaseType_t res = xTaskCreatePinnedToCore(play_task, "audio_play_task", 8192, task_arg, 5, &s_task_handle, 1);
    if (res != pdPASS) {
        s_status.state = AUDIO_RECORDER_STATE_IDLE;
        free(task_arg);
        xSemaphoreGive(s_audio_mutex);
        ESP_LOGE(TAG, "Falha ao criar task de reproducao");
        return ESP_FAIL;
    }

    xSemaphoreGive(s_audio_mutex);
    return ESP_OK;
}

esp_err_t audio_recorder_pause_playback(void)
{
    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    if (s_status.state == AUDIO_RECORDER_STATE_PLAYING) {
        s_status.state = AUDIO_RECORDER_STATE_PAUSED;
    }
    xSemaphoreGive(s_audio_mutex);
    return ESP_OK;
}

esp_err_t audio_recorder_resume_playback(void)
{
    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    if (s_status.state == AUDIO_RECORDER_STATE_PAUSED) {
        s_status.state = AUDIO_RECORDER_STATE_PLAYING;
    }
    xSemaphoreGive(s_audio_mutex);
    return ESP_OK;
}

esp_err_t audio_recorder_stop_playback(void)
{
    xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
    if (s_status.state != AUDIO_RECORDER_STATE_PLAYING && s_status.state != AUDIO_RECORDER_STATE_PAUSED) {
        xSemaphoreGive(s_audio_mutex);
        return ESP_OK;
    }

    s_stop_requested = true;
    xSemaphoreGive(s_audio_mutex);

    // Aguarda a finalizacao da task
    while (s_task_handle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    return ESP_OK;
}

void audio_recorder_get_status(audio_recorder_status_t *status)
{
    if (status == nullptr) {
        return;
    }
    if (s_audio_mutex != nullptr) {
        xSemaphoreTake(s_audio_mutex, portMAX_DELAY);
        *status = s_status;
        xSemaphoreGive(s_audio_mutex);
    } else {
        *status = s_status;
    }
}

bool audio_recorder_is_recording(void)
{
    audio_recorder_status_t st;
    audio_recorder_get_status(&st);
    return st.state == AUDIO_RECORDER_STATE_RECORDING;
}

bool audio_recorder_is_playing(void)
{
    audio_recorder_status_t st;
    audio_recorder_get_status(&st);
    return st.state == AUDIO_RECORDER_STATE_PLAYING || st.state == AUDIO_RECORDER_STATE_PAUSED;
}

esp_err_t audio_recorder_set_volume(int volume)
{
    if (volume < 0)
        volume = 0;
    if (volume > 100)
        volume = 100;
    s_current_volume = volume;
    if (s_spk_dev != nullptr) {
        return esp_codec_dev_set_out_vol(s_spk_dev, s_current_volume);
    }
    return ESP_OK;
}
