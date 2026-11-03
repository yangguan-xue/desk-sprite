#ifndef HAL_ST7789_H
#define HAL_ST7789_H

#include <stdint.h>
#include <stdbool.h>

// ST7789 Pinout (SPEC-003 第 1.2 节)
#define LCD_PIN_MOSI 11
#define LCD_PIN_SCLK 12
#define LCD_PIN_CS   10
#define LCD_PIN_DC   9
#define LCD_PIN_RST  14
#define LCD_PIN_BL   48

#define LCD_WIDTH    240
#define LCD_HEIGHT   240

void bsp_st7789_init(void);
void bsp_st7789_set_backlight(uint8_t brightness_pct);
void bsp_st7789_fill(uint16_t color);
void bsp_st7789_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data);

#endif // HAL_ST7789_H
