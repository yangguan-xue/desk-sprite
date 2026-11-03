#include "es8311.h"
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HAL_ES8311";

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(I2C_NUM_0, ES8311_I2C_ADDR, data, 2, pdMS_TO_TICKS(100));
}

static esp_err_t es8311_read_reg(uint8_t reg, uint8_t *value) {
    return i2c_master_write_read_device(I2C_NUM_0, ES8311_I2C_ADDR, &reg, 1, value, 1, pdMS_TO_TICKS(100));
}

void bsp_es8311_init(void) {
    // 1. 配置功放使能引脚 PA_EN (GPIO 8)
    gpio_config_t pa_conf = {
        .pin_bit_mask = (1ULL << AUDIO_PIN_PA_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&pa_conf));
    gpio_set_level(AUDIO_PIN_PA_EN, 0); // 默认静音避免上电破音

    // 2. 初始化 I2C 总线 (SDA=40, SCL=39, 100kHz)
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = AUDIO_PIN_SDA,
        .scl_io_num = AUDIO_PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));

    // 3. 校验 ES8311 芯片 ID (Reg 0xFD==0x83, Reg 0xFE==0x11)
    uint8_t id_h = 0, id_l = 0;
    if (es8311_read_reg(0xFD, &id_h) == ESP_OK && es8311_read_reg(0xFE, &id_l) == ESP_OK) {
        ESP_LOGI(TAG, "ES8311 Chip ID detected: 0x%02X 0x%02X", id_h, id_l);
    } else {
        ESP_LOGW(TAG, "ES8311 identity read failed, proceeding with register writes...");
    }

    // 4. 发送 Codec 初始化配置时序 (板级核验 QuotaClock/mono_audio 序列)
    es8311_write_reg(0x00, 0x1F); // Reset
    vTaskDelay(pdMS_TO_TICKS(20));

    const uint8_t regs[][2] = {
        {0x00, 0x00}, {0x00, 0x80}, {0x01, 0x3F}, {0x02, 0x00}, {0x03, 0x10},
        {0x04, 0x10}, {0x05, 0x00}, {0x06, 0x03}, {0x07, 0x00}, {0x08, 0xFF},
        {0x09, 0x0C}, {0x0A, 0x0C}, {0x0D, 0x01}, {0x0E, 0x02}, {0x12, 0x00},
        {0x13, 0x10}, {0x1C, 0x6A}, {0x37, 0x08}, {0x32, 0xBF}, {0x31, 0x60}
    };
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        es8311_write_reg(regs[i][0], regs[i][1]);
    }
    es8311_write_reg(0x31, 0x00); // 开启 DAC 电源

    // 5. 初始化 I2S 接口 (16kHz, 16bit, TX Master, MCLK=4096000)
    i2s_config_t i2s_cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 4096000,
    };
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &i2s_cfg, 0, NULL));

    i2s_pin_config_t pin_config = {
        .mck_io_num = AUDIO_PIN_MCLK,
        .bck_io_num = AUDIO_PIN_BCLK,
        .ws_io_num = AUDIO_PIN_LRCK,
        .data_out_num = AUDIO_PIN_SDOUT,
        .data_in_num = I2S_PIN_NO_CHANGE,
    };
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pin_config));

    ESP_LOGI(TAG, "ES8311 Codec & I2S initialized successfully (MCLK=%d, BCLK=%d, WS=%d, DOUT=%d, PA_EN=%d)",
             AUDIO_PIN_MCLK, AUDIO_PIN_BCLK, AUDIO_PIN_LRCK, AUDIO_PIN_SDOUT, AUDIO_PIN_PA_EN);
}

void bsp_es8311_set_mute(bool mute) {
    // 硬件 PA_EN 控制与 Codec 寄存器软静音
    gpio_set_level(AUDIO_PIN_PA_EN, mute ? 0 : 1);
    es8311_write_reg(0x31, mute ? 0x60 : 0x00);
    ESP_LOGI(TAG, "Audio mute set to %s (PA_EN=%d)", mute ? "MUTED" : "UNMUTED", AUDIO_PIN_PA_EN);
}

void bsp_es8311_set_volume(uint8_t volume_pct) {
    if (volume_pct > 100) volume_pct = 100;
    uint8_t reg_val = (uint8_t)((volume_pct * 255) / 100);
    es8311_write_reg(0x32, reg_val);
    ESP_LOGI(TAG, "ES8311 DAC Volume set to %d%% (reg 0x32=0x%02X)", volume_pct, reg_val);
}

void bsp_es8311_play_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz == 0 || duration_ms == 0) return;

    uint32_t total_samples = (16000 * duration_ms) / 1000;
    int16_t block[256 * 2]; // 左右声道样本

    // 打开功放
    gpio_set_level(AUDIO_PIN_PA_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    for (uint32_t start = 0; start < total_samples; start += 256) {
        uint32_t count = (total_samples - start) < 256 ? (total_samples - start) : 256;
        for (uint32_t n = 0; n < count; ++n) {
            uint32_t p = start + n;
            // 简单的淡入淡出包络，避免首尾爆音
            float env = 1.0f;
            if (p < 240) env = (float)p / 240.0f;
            else if (total_samples - p < 240) env = (float)(total_samples - p) / 240.0f;

            int16_t val = (int16_t)(sinf(2.0f * 3.14159265f * freq_hz * p / 16000.0f) * env * 2500);
            block[n * 2] = val;     // 左声道
            block[n * 2 + 1] = val; // 右声道
        }
        size_t written = 0;
        i2s_write(I2S_NUM_0, block, count * 4, &written, pdMS_TO_TICKS(100));
    }

    // 关闭功放
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(AUDIO_PIN_PA_EN, 0);
    i2s_zero_dma_buffer(I2S_NUM_0);
}
