#ifndef HAL_ST7789_H
#define HAL_ST7789_H

#include <stdint.h>
#include <stdbool.h>

// ST7789 Pinout (实测 USB 小电视 S3 硬件板型 SPEC-005 第 3.0 节 & board-profile.json)
#define LCD_PIN_MOSI 1
#define LCD_PIN_SCLK 18
#define LCD_PIN_CS   -1
#define LCD_PIN_DC   17
#define LCD_PIN_RST  6
#define LCD_PIN_BL   2

#define LCD_WIDTH    240
#define LCD_HEIGHT   240

// 常用 16-bit RGB565 颜色常量
#define COLOR_RGB565_BLACK      0x0000
#define COLOR_RGB565_WHITE      0xFFFF
#define COLOR_RGB565_GRAY       0x8410
#define COLOR_RGB565_DARKGRAY   0x2104
#define COLOR_RGB565_RED        0xF800
#define COLOR_RGB565_DARKRED    0x9800
#define COLOR_RGB565_ORANGE     0xFD20
#define COLOR_RGB565_DARKORANGE 0xBA00
#define COLOR_RGB565_YELLOW     0xFFE0
#define COLOR_RGB565_GREEN      0x07E0
#define COLOR_RGB565_BLUE       0x001F
#define COLOR_RGB565_PURPLE     0x8010
#define COLOR_RGB565_BG_DARK    0x10A2

void bsp_st7789_init(void);
void bsp_st7789_set_backlight(uint8_t brightness_pct);
void bsp_st7789_fill(uint16_t color);
void bsp_st7789_fill_rect(int x, int y, int w, int h, uint16_t color);
void bsp_st7789_draw_glyph(int x, int y, uint32_t codepoint, uint16_t fg_color, uint16_t bg_color);
void bsp_st7789_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg);
void bsp_st7789_draw_string_wrapped(int x, int y, int max_w, int max_h, const char *str, uint16_t fg, uint16_t bg);
void bsp_st7789_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data);

#endif // HAL_ST7789_H
