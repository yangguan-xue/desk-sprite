#include "st7789.h"
#include "font_utf8.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "HAL_ST7789";

static esp_lcd_panel_io_handle_t s_io_handle = NULL;
static esp_lcd_panel_handle_t s_panel_handle = NULL;
static SemaphoreHandle_t s_trans_done_sem = NULL;
static SemaphoreHandle_t s_lcd_mutex = NULL;

static bool IRAM_ATTR lcd_color_trans_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    (void)panel_io;
    (void)edata;
    (void)user_ctx;
    BaseType_t high_task_awoken = pdFALSE;
    if (s_trans_done_sem) {
        xSemaphoreGiveFromISR(s_trans_done_sem, &high_task_awoken);
    }
    return high_task_awoken == pdTRUE;
}

void bsp_st7789_init(void) {
    ESP_LOGI(TAG, "Initializing ST7789 LCD (MOSI=%d, SCLK=%d, CS=%d, DC=%d, RST=%d, BL=%d)",
             LCD_PIN_MOSI, LCD_PIN_SCLK, LCD_PIN_CS, LCD_PIN_DC, LCD_PIN_RST, LCD_PIN_BL);

    // 1. 初始化背光 LEDC PWM
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_8_BIT,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LCD_PIN_BL,
        .duty           = 255, // 初始置 255 关断 AO3401 (低电平导通)，避免开机花屏
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // 2. 初始化 SPI 总线
    spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_PIN_SCLK,
        .mosi_io_num = LCD_PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * 40 * sizeof(uint16_t),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 3. 安装 LCD 驱动芯片 IO 与完成中断回调 (R5-03: 同步生命周期守护)
    if (!s_trans_done_sem) {
        s_trans_done_sem = xSemaphoreCreateBinary();
    }
    if (!s_lcd_mutex) {
        s_lcd_mutex = xSemaphoreCreateMutex();
    }

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 27 * 1000 * 1000, // 27MHz 实测稳定频率 (board-profile.json)
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 3, // ST7789 SPI Mode 3
        .trans_queue_depth = 10,
        .on_color_trans_done = lcd_color_trans_done_cb,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &s_io_handle));

    // 4. 安装 ST7789 驱动 (SPEC-005 第 3.0 节 & board-profile.json: 格式 RGB565, 顺序 RGB)
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_io_handle, &panel_config, &s_panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    // 5. 初始填充亮红色 (0xF800) 并 100% 满亮度点亮背光
    bsp_st7789_fill(0xF800); // 纯红
    bsp_st7789_set_backlight(100);
    ESP_LOGI(TAG, "ST7789 initialized and screen filled with RED (0xF800)");
}

void bsp_st7789_set_backlight(uint8_t brightness_pct) {
    if (brightness_pct > 100) brightness_pct = 100;
    // AO3401 P-MOSFET 高边开关低电平导通，PWM 占空比反向映射 (SPEC-005 第 3.0 节)
    uint32_t duty = 255 - ((brightness_pct * 255) / 100);
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void bsp_st7789_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data) {
    if (!s_panel_handle || !color_data) return;
    if (s_lcd_mutex) {
        xSemaphoreTake(s_lcd_mutex, portMAX_DELAY);
    }
    esp_lcd_panel_draw_bitmap(s_panel_handle, x_start, y_start, x_end, y_end, color_data);
    // 同步等待后台 DMA 发送完成，确保调用方缓冲区在返回前保持存活且未被覆盖 (R5-03)
    if (s_trans_done_sem) {
        if (xSemaphoreTake(s_trans_done_sem, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE(TAG, "LCD color transmission timed out");
        }
    }
    if (s_lcd_mutex) {
        xSemaphoreGive(s_lcd_mutex);
    }
}

// ESP32-S3 是小端序 (Little-Endian)，SPI 发送 16-bit 像素数据时逐字节发送。
// ST7789 控制器接收大端序 (Big-Endian，高字节在前)。写入缓冲区前进行字节交换。
static inline uint16_t color_to_be(uint16_t c) {
    return (uint16_t)((c >> 8) | (c << 8));
}

void bsp_st7789_fill(uint16_t color) {
    if (!s_panel_handle) return;
    uint16_t be_color = color_to_be(color);
    static uint16_t __attribute__((aligned(4))) line_buf[LCD_WIDTH * 20];
    for (int i = 0; i < LCD_WIDTH * 20; i++) {
        line_buf[i] = be_color;
    }
    for (int y = 0; y < LCD_HEIGHT; y += 20) {
        bsp_st7789_draw_bitmap(0, y, LCD_WIDTH, y + 20, line_buf);
    }
}

void bsp_st7789_fill_rect(int x, int y, int w, int h, uint16_t color) {
    if (!s_panel_handle) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    if (y + h > LCD_HEIGHT) h = LCD_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    uint16_t be_color = color_to_be(color);
    static uint16_t __attribute__((aligned(4))) s_chunk_buf[LCD_WIDTH * 16];
    int max_pixels = LCD_WIDTH * 16;
    for (int i = 0; i < max_pixels; i++) {
        s_chunk_buf[i] = be_color;
    }

    int cur_y = y;
    int remaining_h = h;
    while (remaining_h > 0) {
        int lines = remaining_h > 16 ? 16 : remaining_h;
        bsp_st7789_draw_bitmap(x, cur_y, x + w, cur_y + lines, s_chunk_buf);
        cur_y += lines;
        remaining_h -= lines;
    }
}

void bsp_st7789_draw_glyph(int x, int y, uint32_t codepoint, uint16_t fg_color, uint16_t bg_color) {
    if (!s_panel_handle) return;
    if (x < 0 || x + 8 > LCD_WIDTH || y < 0 || y + 8 > LCD_HEIGHT) return;

    const uint8_t *bitmap = font_get_glyph_8x8(codepoint);
    uint16_t be_fg = color_to_be(fg_color);
    uint16_t be_bg = color_to_be(bg_color);
    uint16_t __attribute__((aligned(4))) buf[64];
    for (int r = 0; r < 8; r++) {
        uint8_t row = bitmap[r];
        for (int c = 0; c < 8; c++) {
            bool set = (row & (1 << c)) != 0;
            buf[r * 8 + c] = set ? be_fg : be_bg;
        }
    }
    bsp_st7789_draw_bitmap(x, y, x + 8, y + 8, buf);
}

void bsp_st7789_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg) {
    if (!str || !s_panel_handle) return;
    int cur_x = x;
    const char *ptr = str;
    while (*ptr && cur_x + 8 <= LCD_WIDTH) {
        if (*ptr == '\n') break;
        uint32_t cp = utf8_next_codepoint(&ptr);
        if (cp == 0) break;
        bsp_st7789_draw_glyph(cur_x, y, cp, fg, bg);
        cur_x += 8;
    }
}

void bsp_st7789_draw_string_wrapped(int x, int y, int max_w, int max_h, const char *str, uint16_t fg, uint16_t bg) {
    if (!str || !s_panel_handle) return;
    int cur_x = x;
    int cur_y = y;
    const char *ptr = str;
    while (*ptr) {
        if (*ptr == '\n') {
            cur_x = x;
            cur_y += 10;
            ptr++;
            if (cur_y + 8 > y + max_h) break;
            continue;
        }

        if (cur_x + 8 > x + max_w) {
            cur_x = x;
            cur_y += 10;
            if (cur_y + 8 > y + max_h) break;
        }

        uint32_t cp = utf8_next_codepoint(&ptr);
        if (cp == 0) break;

        bsp_st7789_draw_glyph(cur_x, cur_y, cp, fg, bg);
        cur_x += 8;
    }
}
