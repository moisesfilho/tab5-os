#include "music_player.h"
#include "audio_recorder.h"
#include "bsp/m5stack_tab5.h"
#include "esp_audio_simple_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "wifi_storage.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

/* Decode MP3 em tempo real: o build global usa -Og (debug), que deixaria o
 * minimp3 ~2.4x mais lento que o tempo real. Forcar -O2 neste arquivo faz o
 * minimp3 (incluido abaixo) acompanhar o I2S. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O2")
#endif

/* minimp3: decodificador MP3 header-only (public domain). O wrapper da lib
 * esp_audio_codec e lento no P4 (~55ms/frame); usando o minimp3 direto com
 * -O2 o decode fica bem abaixo do orcamento de tempo real. */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

static const char *TAG = "tab5_music_player";

#define IN_BUFFER_SIZE 4096
#define OUT_BUFFER_SIZE 8192
#define MUSIC_DIR "/sdcard/musica"

namespace {

SemaphoreHandle_t s_music_mutex = nullptr;
esp_codec_dev_handle_t s_spk_dev = nullptr;
TaskHandle_t s_task_handle = nullptr;
volatile bool s_stop_requested = false;
int s_current_volume = 80;
bool s_decoders_registered = false;

music_player_status_t s_status = {
    .state = MUSIC_PLAYER_STATE_IDLE,
    .current_time_sec = 0,
    .total_time_sec = 0,
    .sample_rate = 44100,
    .channels = 2,
    .bits_per_sample = 16,
    .current_filepath = {0},
};

void ensure_music_dir_exists(void)
{
    struct stat st;
    if (stat(MUSIC_DIR, &st) != 0) {
        wifi_storage_mount();
        mkdir(MUSIC_DIR, 0755);
    }
}

std::string get_file_extension(const char *filepath)
{
    if (filepath == nullptr) {
        return "";
    }
    const char *dot = strrchr(filepath, '.');
    if (!dot || *(dot + 1) == '\0') {
        return "";
    }
    std::string ext = dot + 1;
    for (char &c : ext) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

#pragma pack(push, 1)
typedef struct {
    char riff_id[4];        // "RIFF"
    uint32_t riff_size;     // Tamanho total do arquivo - 8
    char wave_id[4];        // "WAVE"
    char fmt_id[4];         // "fmt "
    uint32_t fmt_size;      // 16 para PCM linear
    uint16_t audio_format;  // 1 para PCM
    uint16_t num_channels;  // 1 (mono) ou 2 (stereo)
    uint32_t sample_rate;   // ex: 44100
    uint32_t byte_rate;     // sample_rate * num_channels * bits/8
    uint16_t block_align;   // num_channels * bits/8
    uint16_t bits_per_samp; // 16
    char data_id[4];        // "data"
    uint32_t data_size;     // Tamanho dos dados de audio PCM
} wav_header_t;
#pragma pack(pop)

void skip_id3v2(FILE *fp)
{
    uint8_t header[10];
    if (fread(header, 1, 10, fp) == 10) {
        if (header[0] == 'I' && header[1] == 'D' && header[2] == '3') {
            uint32_t tag_size = ((header[6] & 0x7F) << 21) | ((header[7] & 0x7F) << 14) | ((header[8] & 0x7F) << 7) |
                                (header[9] & 0x7F);
            uint32_t total_skip = 10 + tag_size;
            if (header[5] & 0x10) {
                total_skip += 10;
            }
            ESP_LOGI(TAG, "ID3v2 tag detectada (%u bytes), pulando para dados de audio", (unsigned)total_skip);
            fseek(fp, total_skip, SEEK_SET);
            return;
        }
    }
    fseek(fp, 0, SEEK_SET);
}

/* Estado do codec de saida (ES8388) durante a reproducao. */
typedef struct {
    esp_codec_dev_handle_t spk;
    bool opened;
    uint32_t bytes_per_sec;
    uint64_t pcm_played;
} play_codec_t;

void music_player_sync_audio_output(void)
{
    bool hp_connected = bsp_headphone_is_connected();
    /* Se o fone de ouvido estiver conectado, o amplificador do alto-falante embutido
     * e desligado para tocar exclusivamente no fone (sem tocar no speaker).
     * Se o fone for removido, o som volta automaticamente para o alto-falante. */
    bsp_feature_enable(BSP_FEATURE_SPEAKER, !hp_connected);
}

bool play_codec_open(play_codec_t *c, uint32_t sample_rate, uint8_t channels, uint8_t bits)
{
    if (c->opened) {
        return true;
    }
    /* ESP32-P4 I2S STD mode requer canais pares (minimo 2 / estereo) */
    uint8_t codec_channels = (channels == 1) ? 2 : channels;
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = bits,
        .channel = codec_channels,
        .channel_mask = 0,
        .sample_rate = sample_rate,
        .mclk_multiple = 0,
    };
    int ret = esp_codec_dev_open(c->spk, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Falha ao abrir codec de saida (ret=%d, sr=%u, ch=%u, bits=%u)", ret, (unsigned)sample_rate,
                 (unsigned)codec_channels, (unsigned)bits);
        return false;
    }
    esp_codec_dev_set_out_mute(c->spk, false);
    music_player_sync_audio_output();
    ESP_LOGI(TAG, "Codec aberto com sucesso: sr=%u, ch=%u, bits=%u (fone=%d)", (unsigned)sample_rate,
             (unsigned)codec_channels, (unsigned)bits, (int)bsp_headphone_is_connected());
    c->opened = true;
    c->bytes_per_sec = sample_rate * codec_channels * (bits / 8);
    if (c->bytes_per_sec == 0) {
        c->bytes_per_sec = 44100 * 2 * 2;
    }
    return true;
}

void play_codec_write(play_codec_t *c, uint8_t *buffer, uint32_t len)
{
    /* Checa periodicamente se o fone de ouvido foi conectado/desconectado durante a reproducao */
    static uint32_t s_last_hp_check_ms = 0;
    uint32_t now_ms = esp_log_timestamp();
    if (now_ms - s_last_hp_check_ms >= 250) {
        s_last_hp_check_ms = now_ms;
        music_player_sync_audio_output();
    }

    if (esp_codec_dev_write(c->spk, buffer, len) == ESP_CODEC_DEV_OK) {
        c->pcm_played += len;
        uint32_t cur_sec = (c->bytes_per_sec > 0) ? (uint32_t)(c->pcm_played / c->bytes_per_sec) : 0;
        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
        s_status.current_time_sec = cur_sec;
        if (s_status.total_time_sec < cur_sec) {
            s_status.total_time_sec = cur_sec;
        }
        xSemaphoreGive(s_music_mutex);
    } else {
        ESP_LOGW(TAG, "esp_codec_dev_write retornou erro");
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void play_task_exit_failed(void)
{
    if (s_spk_dev != nullptr) {
        esp_codec_dev_set_out_mute(s_spk_dev, true);
    }
    bsp_feature_enable(BSP_FEATURE_SPEAKER, false);
    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_status.state = MUSIC_PLAYER_STATE_IDLE;
    s_task_handle = nullptr;
    xSemaphoreGive(s_music_mutex);
    vTaskDelete(nullptr);
}

void music_play_task(void *param)
{
    char filepath[256];
    strncpy(filepath, (const char *)param, sizeof(filepath) - 1);
    filepath[sizeof(filepath) - 1] = '\0';
    free(param);

    ESP_LOGI(TAG, "music_play_task iniciada para: %s", filepath);

    FILE *fp = fopen(filepath, "rb");
    if (!fp) {
        wifi_storage_mount();
        fp = fopen(filepath, "rb");
    }

    if (!fp) {
        ESP_LOGE(TAG, "Falha ao abrir arquivo: %s (errno=%d)", filepath, errno);
        play_task_exit_failed();
        return;
    }

    /* Obtem tamanho total do arquivo para estimativa de duracao */
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    ESP_LOGI(TAG, "Arquivo aberto com sucesso (tamanho=%ld bytes)", file_size);

    std::string ext = get_file_extension(filepath);
    bool is_mp3 = (ext == "mp3");

    uint8_t *in_buf = (uint8_t *)heap_caps_malloc(IN_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!in_buf) {
        in_buf = (uint8_t *)malloc(IN_BUFFER_SIZE);
    }

    uint8_t *out_buf = (uint8_t *)heap_caps_malloc(OUT_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!out_buf) {
        out_buf = (uint8_t *)malloc(OUT_BUFFER_SIZE);
    }

    if (!in_buf || !out_buf) {
        ESP_LOGE(TAG, "Falha ao alocar buffers de audio");
        if (in_buf) {
            free(in_buf);
        }
        if (out_buf) {
            free(out_buf);
        }
        fclose(fp);
        play_task_exit_failed();
        return;
    }

    play_codec_t codec = {
        .spk = s_spk_dev,
        .opened = false,
        .bytes_per_sec = 44100 * 2 * 2,
        .pcm_played = 0,
    };
    esp_codec_dev_set_out_vol(s_spk_dev, s_current_volume);

    if (is_mp3) {
        skip_id3v2(fp);
        long audio_start_pos = ftell(fp);
        long audio_data_size = (file_size > audio_start_pos) ? (file_size - audio_start_pos) : file_size;
        uint64_t total_consumed_bytes = 0;

        mp3dec_t *mp3dec = (mp3dec_t *)heap_caps_malloc(sizeof(mp3dec_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (mp3dec == nullptr) {
            mp3dec = (mp3dec_t *)malloc(sizeof(mp3dec_t));
        }
        if (mp3dec == nullptr) {
            ESP_LOGE(TAG, "Falha ao alocar estado do decoder MP3");
            free(in_buf);
            free(out_buf);
            fclose(fp);
            play_task_exit_failed();
            return;
        }
        mp3dec_init(mp3dec);

        // cppcheck-suppress invalidPointerCast
        mp3d_sample_t *pcm_buf = reinterpret_cast<mp3d_sample_t *>(out_buf);
        size_t data_len = 0;
        bool eof = false;
        int64_t t_decode_us = 0;
        int64_t t_write_us = 0;
        uint32_t frame_count = 0;

        while (!s_stop_requested) {
            if (s_status.state == MUSIC_PLAYER_STATE_PAUSED) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            /* Le dados do SD para encher o buffer de entrada */
            if (!eof && data_len < IN_BUFFER_SIZE) {
                if (feof(fp)) {
                    eof = true;
                } else {
                    size_t rd = fread(in_buf + data_len, 1, IN_BUFFER_SIZE - data_len, fp);
                    data_len += rd;
                    if (rd == 0) {
                        eof = true;
                    }
                }
            }

            if (data_len == 0) {
                break; /* Fim do arquivo */
            }

            mp3dec_frame_info_t info;
            int64_t t_dec_start = esp_timer_get_time();
            int samples = mp3dec_decode_frame(mp3dec, in_buf, (int)data_len, pcm_buf, &info);
            t_decode_us += esp_timer_get_time() - t_dec_start;

            if (samples > 0) {
                uint32_t sr = (info.hz > 0) ? (uint32_t)info.hz : 44100;
                uint8_t ch = (info.channels > 0) ? (uint8_t)info.channels : 2;

                if (!codec.opened) {
                    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
                    s_status.sample_rate = sr;
                    s_status.channels = ch;
                    s_status.bits_per_sample = 16;
                    xSemaphoreGive(s_music_mutex);

                    if (!play_codec_open(&codec, sr, 2, 16)) {
                        break;
                    }

                    uint32_t est_total_sec = 0;
                    if (info.bitrate_kbps > 0 && audio_data_size > 0) {
                        est_total_sec =
                            (uint32_t)(((uint64_t)audio_data_size * 8ULL) / ((uint64_t)info.bitrate_kbps * 1000ULL));
                    }
                    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
                    s_status.total_time_sec = est_total_sec;
                    xSemaphoreGive(s_music_mutex);

                    ESP_LOGI(TAG, "MP3 Info: %u Hz, %u ch, %d kbps, duracao estimada=%u s (audio=%ld bytes)",
                             (unsigned)sr, (unsigned)ch, info.bitrate_kbps, (unsigned)est_total_sec, audio_data_size);
                }

                /* Se o MP3 for mono (1 canal), duplica amostras para estéreo */
                uint32_t out_bytes;
                if (ch == 1) {
                    for (int i = samples - 1; i >= 0; --i) {
                        pcm_buf[i * 2] = pcm_buf[i];
                        pcm_buf[i * 2 + 1] = pcm_buf[i];
                    }
                    out_bytes = (uint32_t)samples * 2 * sizeof(mp3d_sample_t);
                } else {
                    out_bytes = (uint32_t)samples * ch * sizeof(mp3d_sample_t);
                }

                int consumed =
                    (info.frame_bytes > 0 && info.frame_bytes <= (int)data_len) ? info.frame_bytes : (int)data_len;
                memmove(in_buf, in_buf + consumed, data_len - consumed);
                data_len -= consumed;
                total_consumed_bytes += consumed;

                int64_t t_write_start = esp_timer_get_time();
                play_codec_write(&codec, (uint8_t *)pcm_buf, out_bytes);
                t_write_us += esp_timer_get_time() - t_write_start;
                frame_count++;

                /* Refina dinamicamente a duracao total com base no consumo real do arquivo de audio */
                if ((frame_count % 25) == 0 && audio_data_size > 0 && total_consumed_bytes > 0) {
                    uint32_t cur_sec =
                        (codec.bytes_per_sec > 0) ? (uint32_t)(codec.pcm_played / codec.bytes_per_sec) : 0;
                    if (cur_sec >= 1) {
                        uint32_t dynamic_total =
                            (uint32_t)(((uint64_t)cur_sec * (uint64_t)audio_data_size) / total_consumed_bytes);
                        if (dynamic_total >= cur_sec) {
                            xSemaphoreTake(s_music_mutex, portMAX_DELAY);
                            s_status.total_time_sec = dynamic_total;
                            xSemaphoreGive(s_music_mutex);
                        }
                    }
                }

                if ((frame_count % 100) == 0) {
                    ESP_LOGI(TAG, "MP3 TIMING: frames=%u decode=%.2fms/fr write=%.2fms/fr total=%.2fms/fr",
                             (unsigned)frame_count, (double)t_decode_us / frame_count / 1000.0,
                             (double)t_write_us / frame_count / 1000.0,
                             (double)(t_decode_us + t_write_us) / frame_count / 1000.0);
                }
                continue;
            }

            /* samples == 0 */
            if (info.frame_bytes > 0) {
                int consumed = (info.frame_bytes <= (int)data_len) ? info.frame_bytes : (int)data_len;
                memmove(in_buf, in_buf + consumed, data_len - consumed);
                data_len -= consumed;
                total_consumed_bytes += consumed;
                continue;
            }

            /* info.frame_bytes == 0: precisa de mais dados do SD */
            if (!eof && data_len < IN_BUFFER_SIZE) {
                continue;
            }

            /* Buffer cheio ou EOF e nao conseguiu decodificar: descarta 1 byte para resync */
            if (data_len > 0) {
                memmove(in_buf, in_buf + 1, data_len - 1);
                data_len--;
                total_consumed_bytes += 1;
            }
        }

        uint32_t final_sec = (codec.bytes_per_sec > 0) ? (uint32_t)(codec.pcm_played / codec.bytes_per_sec) : 0;
        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
        s_status.current_time_sec = final_sec;
        s_status.total_time_sec = final_sec;
        xSemaphoreGive(s_music_mutex);

        free(mp3dec);
        ESP_LOGI(TAG, "Reproducao MP3 finalizada. Total PCM bytes tocados: %llu", (unsigned long long)codec.pcm_played);
    } else {
        /* WAV: Decodificação direta de PCM linear (RIFF WAVE) */
        wav_header_t wav_hdr;
        bool is_valid_wav = false;
        uint32_t sample_rate = 44100;
        uint16_t num_channels = 2;
        uint16_t bits_per_sample = 16;
        uint32_t data_bytes = 0;

        if (fread(&wav_hdr, 1, sizeof(wav_hdr), fp) == sizeof(wav_hdr)) {
            if (memcmp(wav_hdr.riff_id, "RIFF", 4) == 0 && memcmp(wav_hdr.wave_id, "WAVE", 4) == 0 &&
                memcmp(wav_hdr.fmt_id, "fmt ", 4) == 0) {
                is_valid_wav = true;
                sample_rate = wav_hdr.sample_rate;
                num_channels = wav_hdr.num_channels;
                bits_per_sample = wav_hdr.bits_per_samp;
                data_bytes = wav_hdr.data_size;
            }
        }

        if (!is_valid_wav) {
            /* Busca generica pelo chunk 'fmt ' e 'data' */
            fseek(fp, 0, SEEK_SET);
            char riff_sig[12];
            if (fread(riff_sig, 1, 12, fp) == 12 && memcmp(riff_sig, "RIFF", 4) == 0 &&
                memcmp(riff_sig + 8, "WAVE", 4) == 0) {
                char chunk_id[4];
                uint32_t chunk_size = 0;
                while (fread(chunk_id, 1, 4, fp) == 4 && fread(&chunk_size, 1, 4, fp) == 4) {
                    if (memcmp(chunk_id, "fmt ", 4) == 0) {
                        uint16_t fmt_code = 0;
                        fread(&fmt_code, 1, 2, fp);
                        fread(&num_channels, 1, 2, fp);
                        fread(&sample_rate, 1, 4, fp);
                        uint32_t byte_rate = 0;
                        fread(&byte_rate, 1, 4, fp);
                        uint16_t block_align = 0;
                        fread(&block_align, 1, 2, fp);
                        fread(&bits_per_sample, 1, 2, fp);
                        if (chunk_size > 16) {
                            fseek(fp, chunk_size - 16, SEEK_CUR);
                        }
                    } else if (memcmp(chunk_id, "data", 4) == 0) {
                        data_bytes = chunk_size;
                        is_valid_wav = true;
                        break;
                    } else {
                        fseek(fp, chunk_size, SEEK_CUR);
                    }
                }
            }
        }

        if (!is_valid_wav) {
            ESP_LOGW(TAG, "Header WAV nao reconhecido em %s", filepath);
            free(in_buf);
            free(out_buf);
            fclose(fp);
            play_task_exit_failed();
            return;
        }

        if (sample_rate == 0)
            sample_rate = 44100;
        if (num_channels == 0)
            num_channels = 2;
        if (bits_per_sample == 0)
            bits_per_sample = 16;

        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
        s_status.sample_rate = sample_rate;
        s_status.channels = num_channels;
        s_status.bits_per_sample = bits_per_sample;
        xSemaphoreGive(s_music_mutex);

        if (!play_codec_open(&codec, sample_rate, 2, bits_per_sample)) {
            ESP_LOGE(TAG, "Falha ao abrir codec para WAV");
            free(in_buf);
            free(out_buf);
            fclose(fp);
            play_task_exit_failed();
            return;
        }

        uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
        uint32_t est_total_sec = (byte_rate > 0 && data_bytes > 0) ? (data_bytes / byte_rate) : 0;
        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
        s_status.total_time_sec = est_total_sec;
        xSemaphoreGive(s_music_mutex);

        ESP_LOGI(TAG, "WAV Info: %u Hz, %u ch, %u bits, duracao=%u s", (unsigned)sample_rate, (unsigned)num_channels,
                 (unsigned)bits_per_sample, (unsigned)est_total_sec);

        uint32_t bytes_played = 0;
        while (!s_stop_requested && (data_bytes == 0 || bytes_played < data_bytes)) {
            if (s_status.state == MUSIC_PLAYER_STATE_PAUSED) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

            size_t to_read = IN_BUFFER_SIZE;
            if (num_channels == 1) {
                to_read = IN_BUFFER_SIZE / 2;
            }
            if (data_bytes > 0 && (data_bytes - bytes_played) < to_read) {
                to_read = data_bytes - bytes_played;
            }

            if (feof(fp) || ferror(fp)) {
                break;
            }

            size_t rd = fread(in_buf, 1, to_read, fp);
            if (rd == 0 || ferror(fp)) {
                break;
            }

            bytes_played += rd;

            if (num_channels == 1 && bits_per_sample == 16) {
                int16_t *src = (int16_t *)in_buf;
                int16_t *dst = (int16_t *)out_buf;
                size_t num_samples = rd / 2;
                for (size_t i = 0; i < num_samples; ++i) {
                    dst[i * 2] = src[i];
                    dst[i * 2 + 1] = src[i];
                }
                play_codec_write(&codec, out_buf, num_samples * 4);
            } else {
                play_codec_write(&codec, in_buf, rd);
            }
        }

        ESP_LOGI(TAG, "Reproducao WAV finalizada. Total PCM bytes tocados: %llu", (unsigned long long)codec.pcm_played);
    }

    if (codec.opened) {
        esp_codec_dev_set_out_mute(s_spk_dev, true);
        esp_codec_dev_close(s_spk_dev);
    }
    bsp_feature_enable(BSP_FEATURE_SPEAKER, false);
    free(in_buf);
    free(out_buf);
    fclose(fp);

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    s_status.state = MUSIC_PLAYER_STATE_IDLE;
    s_status.current_time_sec = 0;
    s_task_handle = nullptr;
    xSemaphoreGive(s_music_mutex);

    vTaskDelete(nullptr);
}

} // namespace

esp_err_t music_player_init(void)
{
    if (s_music_mutex == nullptr) {
        s_music_mutex = xSemaphoreCreateMutex();
        if (!s_music_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    wifi_storage_mount();
    bsp_feature_enable(BSP_FEATURE_SPEAKER, false);

    if (!s_decoders_registered) {
        esp_audio_err_t ret = esp_audio_simple_dec_register_default();
        if (ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGW(TAG, "esp_audio_simple_dec_register_default retornou %d", (int)ret);
        } else {
            s_decoders_registered = true;
            ESP_LOGI(TAG, "Decoders padrao registrados com sucesso");
        }
    }

    if (s_spk_dev == nullptr) {
        s_spk_dev = bsp_audio_codec_speaker_init();
        if (s_spk_dev) {
            esp_codec_dev_set_out_mute(s_spk_dev, true);
            esp_codec_dev_set_out_vol(s_spk_dev, s_current_volume);
            ESP_LOGI(TAG, "Codec de speaker ES8388 inicializado para player de musica");
        } else {
            ESP_LOGW(TAG, "Nao foi possivel inicializar codec de speaker");
        }
    }
    bsp_feature_enable(BSP_FEATURE_SPEAKER, false);

    ensure_music_dir_exists();
    ESP_LOGI(TAG, "HEAP_DIAG music_init: internal=%zu dma=%zu dma_largest=%zu",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_free_size(MALLOC_CAP_DMA),
             heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    return ESP_OK;
}

esp_err_t music_player_start(const char *filepath)
{
    if (filepath == nullptr || strlen(filepath) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "music_player_start requisitado para: %s", filepath);

    if (music_player_init() != ESP_OK || s_spk_dev == nullptr) {
        ESP_LOGE(TAG, "Speaker nao disponivel para reproducao (s_spk_dev=%p)", s_spk_dev);
        return ESP_ERR_NOT_SUPPORTED;
    }

    /* Coexistencia com gravador de voz */
    if (audio_recorder_is_recording()) {
        audio_recorder_stop_recording();
    }
    if (audio_recorder_is_playing()) {
        audio_recorder_stop_playback();
    }

    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    if (s_status.state != MUSIC_PLAYER_STATE_IDLE) {
        xSemaphoreGive(s_music_mutex);
        music_player_stop();
        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    }

    strncpy(s_status.current_filepath, filepath, sizeof(s_status.current_filepath) - 1);
    s_status.current_filepath[sizeof(s_status.current_filepath) - 1] = '\0';
    s_status.state = MUSIC_PLAYER_STATE_PLAYING;
    s_status.current_time_sec = 0;
    s_status.total_time_sec = 0;
    s_stop_requested = false;

    char *task_arg = strdup(filepath);
    BaseType_t res = xTaskCreatePinnedToCoreWithCaps(music_play_task, "music_play_task", 65536, task_arg, 5,
                                                     &s_task_handle, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (res != pdPASS) {
        s_status.state = MUSIC_PLAYER_STATE_IDLE;
        free(task_arg);
        xSemaphoreGive(s_music_mutex);
        ESP_LOGE(TAG, "Falha ao criar task de reproducao de musica em PSRAM");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Task music_play_task criada com sucesso (64KB em PSRAM)");

    xSemaphoreGive(s_music_mutex);
    return ESP_OK;
}

esp_err_t music_player_pause(void)
{
    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    if (s_status.state == MUSIC_PLAYER_STATE_PLAYING) {
        s_status.state = MUSIC_PLAYER_STATE_PAUSED;
    }
    xSemaphoreGive(s_music_mutex);
    return ESP_OK;
}

esp_err_t music_player_resume(void)
{
    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    if (s_status.state == MUSIC_PLAYER_STATE_PAUSED) {
        s_status.state = MUSIC_PLAYER_STATE_PLAYING;
    }
    xSemaphoreGive(s_music_mutex);
    return ESP_OK;
}

esp_err_t music_player_stop(void)
{
    xSemaphoreTake(s_music_mutex, portMAX_DELAY);
    if (s_status.state != MUSIC_PLAYER_STATE_PLAYING && s_status.state != MUSIC_PLAYER_STATE_PAUSED) {
        xSemaphoreGive(s_music_mutex);
        return ESP_OK;
    }

    s_stop_requested = true;
    xSemaphoreGive(s_music_mutex);

    while (s_task_handle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (s_spk_dev != nullptr) {
        esp_codec_dev_set_out_mute(s_spk_dev, true);
    }
    bsp_feature_enable(BSP_FEATURE_SPEAKER, false);

    return ESP_OK;
}

void music_player_get_status(music_player_status_t *status)
{
    if (status == nullptr) {
        return;
    }
    if (s_music_mutex != nullptr) {
        xSemaphoreTake(s_music_mutex, portMAX_DELAY);
        *status = s_status;
        xSemaphoreGive(s_music_mutex);
    } else {
        *status = s_status;
    }
}

bool music_player_is_playing(void)
{
    music_player_status_t st;
    music_player_get_status(&st);
    return st.state == MUSIC_PLAYER_STATE_PLAYING || st.state == MUSIC_PLAYER_STATE_PAUSED;
}

esp_err_t music_player_set_volume(int volume)
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

int music_player_get_volume(void)
{
    return s_current_volume;
}
