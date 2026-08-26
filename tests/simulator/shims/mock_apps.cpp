/* Mocks deterministicos dos backends dos aplicativos (audio, rede, camera).
 * Estado fixo: tudo parado/idle. */

#include "music_player.h"
#include "ai_client.h"
#include "ssh_client.h"
#include "audio_recorder.h"
#include "http_file_server.h"
#include "camera_mgr.h"

#include <cstdio>
#include <cstring>

/* ------------------------------------------------------------------ */
/* Music player                                                        */
/* ------------------------------------------------------------------ */

static music_player_status_t s_music = {};
static int s_music_volume = 60;

esp_err_t music_player_init(void)
{
    memset(&s_music, 0, sizeof(s_music));
    return ESP_OK;
}

esp_err_t music_player_start(const char *filepath)
{
    (void)filepath;
    s_music.state = MUSIC_PLAYER_STATE_PLAYING;
    s_music.current_time_sec = 0;
    s_music.total_time_sec = 180;
    snprintf(s_music.current_filepath, sizeof(s_music.current_filepath), "%s", "/sdcard/musica_fake.mp3");
    return ESP_OK;
}

esp_err_t music_player_pause(void)
{
    if (s_music.state == MUSIC_PLAYER_STATE_PLAYING) {
        s_music.state = MUSIC_PLAYER_STATE_PAUSED;
    }
    return ESP_OK;
}

esp_err_t music_player_resume(void)
{
    if (s_music.state == MUSIC_PLAYER_STATE_PAUSED) {
        s_music.state = MUSIC_PLAYER_STATE_PLAYING;
    }
    return ESP_OK;
}

esp_err_t music_player_stop(void)
{
    s_music.state = MUSIC_PLAYER_STATE_IDLE;
    s_music.current_time_sec = 0;
    return ESP_OK;
}

void music_player_get_status(music_player_status_t *status)
{
    if (status != nullptr) {
        *status = s_music;
        status->sample_rate = 44100;
        status->channels = 2;
        status->bits_per_sample = 16;
    }
}

bool music_player_is_playing(void)
{
    return s_music.state == MUSIC_PLAYER_STATE_PLAYING;
}

esp_err_t music_player_set_volume(int volume)
{
    s_music_volume = volume;
    return ESP_OK;
}

int music_player_get_volume(void)
{
    return s_music_volume;
}

/* ------------------------------------------------------------------ */
/* Cliente de IA: sempre idle                                          */
/* ------------------------------------------------------------------ */

void ai_client_init(void) {}

esp_err_t ai_client_send(const ai_cfg_t *cfg, const std::vector<ai_msg_t> &messages, ai_response_cb_t on_response,
                         ai_state_cb_t on_state, void *user_data)
{
    (void)cfg;
    (void)messages;
    (void)on_response;
    (void)on_state;
    (void)user_data;
    return ESP_FAIL;
}

void ai_client_cancel(void) {}

ai_state_t ai_client_get_state(void)
{
    return AI_STATE_IDLE;
}

bool ai_client_is_busy(void)
{
    return false;
}

/* ------------------------------------------------------------------ */
/* SSH                                                                 */
/* ------------------------------------------------------------------ */

esp_err_t ssh_client_connect(const char *user, const char *host, int port, ssh_rx_cb_t rx_cb, ssh_state_cb_t state_cb)
{
    (void)user;
    (void)host;
    (void)port;
    (void)rx_cb;
    if (state_cb != nullptr) {
        state_cb(SSH_CLIENT_ERROR, "sim: ssh nao disponivel");
    }
    return ESP_FAIL;
}

esp_err_t ssh_client_send_password(const char *password)
{
    (void)password;
    return ESP_FAIL;
}

esp_err_t ssh_client_send_data(const char *data, size_t len)
{
    (void)data;
    (void)len;
    return ESP_FAIL;
}

void ssh_client_disconnect(void) {}

bool ssh_client_is_active(void)
{
    return false;
}

ssh_client_state_t ssh_client_get_state(void)
{
    return SSH_CLIENT_DISCONNECTED;
}

/* ------------------------------------------------------------------ */
/* Gravador de audio                                                   */
/* ------------------------------------------------------------------ */

esp_err_t audio_recorder_init(void)
{
    return ESP_OK;
}

esp_err_t audio_recorder_start_recording(char *out_filepath, size_t out_len)
{
    (void)out_filepath;
    (void)out_len;
    return ESP_FAIL;
}

esp_err_t audio_recorder_stop_recording(void)
{
    return ESP_OK;
}

esp_err_t audio_recorder_start_playback(const char *filepath)
{
    (void)filepath;
    return ESP_FAIL;
}

esp_err_t audio_recorder_pause_playback(void)
{
    return ESP_OK;
}

esp_err_t audio_recorder_resume_playback(void)
{
    return ESP_OK;
}

esp_err_t audio_recorder_stop_playback(void)
{
    return ESP_OK;
}

void audio_recorder_get_status(audio_recorder_status_t *status)
{
    if (status != nullptr) {
        memset(status, 0, sizeof(*status));
        status->state = AUDIO_RECORDER_STATE_IDLE;
    }
}

bool audio_recorder_is_recording(void)
{
    return false;
}

bool audio_recorder_is_playing(void)
{
    return false;
}

esp_err_t audio_recorder_set_volume(int volume)
{
    (void)volume;
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Fileserver HTTP                                                     */
/* ------------------------------------------------------------------ */

esp_err_t http_file_server_start(void)
{
    return ESP_OK;
}

esp_err_t http_file_server_stop(void)
{
    return ESP_OK;
}

bool http_file_server_is_running(void)
{
    return false;
}

uint16_t http_file_server_get_port(void)
{
    return 8080;
}

/* ------------------------------------------------------------------ */
/* Camera                                                              */
/* ------------------------------------------------------------------ */

esp_err_t camera_mgr_init(void)
{
    return ESP_OK;
}

camera_state_t camera_mgr_get_state(void)
{
    return CAMERA_STATE_IDLE;
}

esp_err_t camera_mgr_start_preview(camera_frame_cb_t frame_cb, void *user_data)
{
    (void)frame_cb;
    (void)user_data;
    return ESP_FAIL;
}

esp_err_t camera_mgr_stop_preview(void)
{
    return ESP_OK;
}

void camera_mgr_rotate_rgb565_90(const uint16_t *src, int w, int h, uint16_t *dst)
{
    (void)src;
    (void)w;
    (void)h;
    (void)dst;
}

void camera_mgr_rotate_rgb565_180(const uint16_t *src, int w, int h, uint16_t *dst)
{
    (void)src;
    (void)w;
    (void)h;
    (void)dst;
}

void camera_mgr_rotate_rgb565_270(const uint16_t *src, int w, int h, uint16_t *dst)
{
    (void)src;
    (void)w;
    (void)h;
    (void)dst;
}

esp_err_t camera_mgr_capture_photo_with_rotation_async(char *out_filepath, size_t max_len, int rotation,
                                                       camera_capture_done_cb_t done_cb, void *user_data)
{
    (void)out_filepath;
    (void)max_len;
    (void)rotation;
    if (done_cb != nullptr) {
        done_cb(ESP_FAIL, nullptr, user_data);
    }
    return ESP_FAIL;
}

esp_err_t camera_mgr_capture_photo_async(char *out_filepath, size_t max_len, camera_capture_done_cb_t done_cb,
                                         void *user_data)
{
    (void)out_filepath;
    (void)max_len;
    if (done_cb != nullptr) {
        done_cb(ESP_FAIL, nullptr, user_data);
    }
    return ESP_FAIL;
}

esp_err_t camera_mgr_capture_photo(char *out_filepath, size_t max_len)
{
    (void)out_filepath;
    (void)max_len;
    return ESP_FAIL;
}

bool camera_mgr_is_saving(void)
{
    return false;
}

esp_err_t camera_mgr_wait_save_done(uint32_t timeout_ms)
{
    (void)timeout_ms;
    return ESP_OK;
}

void camera_mgr_deinit(void) {}
