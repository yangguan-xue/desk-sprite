#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/stream_buffer.h"
#include "driver/i2s.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "avi_player.h"
#include "jpeg_decoder.h"
#include "st7789.h"
#include "es8311.h"

#define WIDTH 240
#define HEIGHT 136
#define FRAME_BYTES (WIDTH * HEIGHT * 2)
#define PLAY_DONE BIT0
#define AUDIO_DONE BIT1

extern const uint8_t clip_start[] asm("_binary_clip_avi_start");
extern const uint8_t clip_end[] asm("_binary_clip_avi_end");
static const char *TAG = "VIDEO_PROBE";

typedef struct {
    EventGroupHandle_t events;
    StreamBufferHandle_t audio;
    uint8_t *pixels;
    uint8_t *stripe;
    bool clock_valid;
    volatile bool stop_audio;
    uint32_t frames, video_errors, audio_errors, i2s_errors;
    size_t audio_queued, audio_written;
    int64_t decode_us, decode_max_us, draw_us, draw_max_us;
} probe_t;

static void audio_output(void *arg) {
    probe_t *p = arg;
    uint8_t block[4096];
    for (;;) {
        size_t n = xStreamBufferReceive(p->audio, block, sizeof(block), pdMS_TO_TICKS(50));
        if (n) {
            size_t written = 0;
            esp_err_t err = i2s_write(I2S_NUM_0, block, n, &written, pdMS_TO_TICKS(500));
            if (err != ESP_OK || written != n) p->i2s_errors++;
            p->audio_written += written;
        }
        if (p->stop_audio && xStreamBufferIsEmpty(p->audio)) break;
    }
    // Legacy I2S has four 256-sample stereo DMA buffers (64ms total).
    vTaskDelay(pdMS_TO_TICKS(80));
    xEventGroupSetBits(p->events, AUDIO_DONE);
    vTaskDelete(NULL);
}

static void audio_clock(uint32_t rate, uint32_t bits, uint32_t channels, void *arg) {
    probe_t *p = arg;
    p->clock_valid = rate == 16000 && bits == 16 && channels == 2;
    ESP_LOGI(TAG, "audio_format rate=%" PRIu32 " bits=%" PRIu32 " channels=%" PRIu32 " valid=%d",
             rate, bits, channels, p->clock_valid);
}

static void audio_frame(frame_data_t *data, void *arg) {
    probe_t *p = arg;
    if (!p->clock_valid || data->type != FRAME_TYPE_AUDIO || data->audio_info.format != FORMAT_PCM ||
        !data->data_bytes || data->data_bytes % 4 || data->data_bytes > 65536) {
        p->audio_errors++;
        return;
    }
    size_t sent = xStreamBufferSend(p->audio, data->data, data->data_bytes, pdMS_TO_TICKS(500));
    p->audio_queued += sent;
    if (sent != data->data_bytes) p->audio_errors++;
}

static void video_frame(frame_data_t *data, void *arg) {
    probe_t *p = arg;
    if (data->type != FRAME_TYPE_VIDEO || data->video_info.frame_format != FORMAT_MJEPG ||
        data->video_info.width != WIDTH || data->video_info.height != HEIGHT ||
        !data->data_bytes || data->data_bytes > 65536) {
        p->video_errors++;
        return;
    }
    esp_jpeg_image_cfg_t cfg = {
        .indata = data->data, .indata_size = data->data_bytes,
        .outbuf = p->pixels, .outbuf_size = FRAME_BYTES,
        .out_format = JPEG_IMAGE_FORMAT_RGB565, .out_scale = JPEG_IMAGE_SCALE_0,
        .flags.swap_color_bytes = 1,
    };
    esp_jpeg_image_output_t image;
    int64_t start = esp_timer_get_time();
    esp_err_t err = esp_jpeg_decode(&cfg, &image);
    int64_t decoded = esp_timer_get_time();
    if (err != ESP_OK || image.width != WIDTH || image.height != HEIGHT || image.output_len != FRAME_BYTES) {
        p->video_errors++;
        return;
    }
    for (int y = 0; y < HEIGHT; y += 16) {
        int h = HEIGHT - y < 16 ? HEIGHT - y : 16;
        memcpy(p->stripe, p->pixels + y * WIDTH * 2, h * WIDTH * 2);
        bsp_st7789_draw_bitmap(0, 52 + y, WIDTH, 52 + y + h, p->stripe);
    }
    int64_t drawn = esp_timer_get_time();
    int64_t decode = decoded - start, draw = drawn - decoded;
    p->decode_us += decode;
    p->draw_us += draw;
    if (decode > p->decode_max_us) p->decode_max_us = decode;
    if (draw > p->draw_max_us) p->draw_max_us = draw;
    p->frames++;
}

static void play_end(void *arg) {
    probe_t *p = arg;
    xEventGroupSetBits(p->events, PLAY_DONE);
}

static bool play_round(unsigned round) {
    probe_t p = {0};
    p.pixels = heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    p.stripe = heap_caps_malloc(WIDTH * 16 * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    p.events = xEventGroupCreate();
    p.audio = xStreamBufferCreate(64000, 4);
    ESP_ERROR_CHECK(p.pixels && p.stripe && p.events && p.audio ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(audio_output, "video_pcm", 6144, &p, 6, NULL, 1) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    avi_player_handle_t handle = NULL;
    avi_player_config_t config = {
        .buffer_size = 65536, .video_cb = video_frame, .audio_cb = audio_frame,
        .audio_set_clock_cb = audio_clock, .avi_play_end_cb = play_end,
        .stack_size = 8192, .priority = 5, .coreID = 0, .user_data = &p,
        .stack_in_psram = false,
    };
    ESP_ERROR_CHECK(avi_player_init(config, &handle));
    bsp_es8311_set_mute(false);
    int64_t started = esp_timer_get_time();
    ESP_ERROR_CHECK(avi_player_play_from_memory(handle, (uint8_t *)clip_start, clip_end - clip_start));
    bool ended = (xEventGroupWaitBits(p.events, PLAY_DONE, pdFALSE, pdTRUE, pdMS_TO_TICKS(15000)) & PLAY_DONE) != 0;
    if (!ended) {
        ESP_LOGE(TAG, "round=%u playback timeout", round);
        avi_player_play_stop(handle);
        xEventGroupWaitBits(p.events, PLAY_DONE, pdFALSE, pdTRUE, pdMS_TO_TICKS(2000));
    }
    ESP_ERROR_CHECK(avi_player_deinit(handle));
    p.stop_audio = true;
    ESP_ERROR_CHECK((xEventGroupWaitBits(p.events, AUDIO_DONE, pdFALSE, pdTRUE, pdMS_TO_TICKS(3000)) & AUDIO_DONE)
                    ? ESP_OK : ESP_ERR_TIMEOUT);
    int64_t elapsed = esp_timer_get_time() - started;
    bsp_es8311_set_mute(true);
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_NUM_0));
    bool ok = ended && p.clock_valid && p.frames == 96 && p.audio_written == 512000 &&
              p.audio_queued == p.audio_written && !p.video_errors && !p.audio_errors && !p.i2s_errors;
    ESP_LOGI(TAG, "result round=%u ok=%d frames=%" PRIu32 " audio_bytes=%u elapsed_ms=%" PRId64
             " fps=%.2f video_errors=%" PRIu32 " audio_errors=%" PRIu32 " i2s_errors=%" PRIu32,
             round, ok, p.frames, (unsigned)p.audio_written, elapsed / 1000,
             p.frames * 1000000.0 / elapsed, p.video_errors, p.audio_errors, p.i2s_errors);
    ESP_LOGI(TAG, "timings round=%u decode_avg_ms=%.2f decode_max_ms=%.2f draw_avg_ms=%.2f draw_max_ms=%.2f",
             round, p.frames ? p.decode_us / 1000.0 / p.frames : 0, p.decode_max_us / 1000.0,
             p.frames ? p.draw_us / 1000.0 / p.frames : 0, p.draw_max_us / 1000.0);
    vStreamBufferDelete(p.audio);
    vEventGroupDelete(p.events);
    free(p.stripe);
    free(p.pixels);
    return ok;
}

void app_main(void) {
    bsp_st7789_init();
    bsp_es8311_init();
    bsp_es8311_set_mute(true);
    // Keep the verified HAL DAC setting (0xBF); bound loudness in PCM (peak 1200).
    // The HAL percent maps register codes, not perceived loudness.
    bsp_st7789_set_backlight(65);
    bsp_st7789_fill(COLOR_RGB565_BLACK);
    bsp_st7789_draw_string(24, 20, "VIDEO + AUDIO TEST", COLOR_RGB565_WHITE, COLOR_RGB565_BLACK);
    bsp_st7789_draw_string(24, 210, "240x136 / 12 FPS", COLOR_RGB565_WHITE, COLOR_RGB565_BLACK);
    ESP_LOGI(TAG, "ready clip_bytes=%u starting_in=5s", (unsigned)(clip_end - clip_start));
    vTaskDelay(pdMS_TO_TICKS(5000));
    bool ok = true;
    for (unsigned round = 1; round <= 3; round++) {
        bool passed = play_round(round);
        ok = ok && passed;
        if (!passed) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    bsp_st7789_draw_string(24, 210, ok ? "TEST DONE / RESTORING" : "TEST FAILED / RESTORE",
                          COLOR_RGB565_WHITE, COLOR_RGB565_BLACK);
    ESP_LOGI(TAG, "complete ok=%d", ok);
}
