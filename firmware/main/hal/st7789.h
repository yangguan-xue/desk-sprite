#ifndef HAL_ST7789_H
#define HAL_ST7789_H

#include <stdint.h>
#include <stdbool.h>

// ST7789 Pinout (实测 USB 小电视 S3 硬件板型 board-profile.json)
#define LCD_PIN_MOSI 1
#define LCD_PIN_SCLK 18
#define LCD_PIN_CS   21
#define LCD_PIN_DC   17
#define LCD_PIN_RST  6
#define LCD_PIN_BL   2

#define LCD_WIDTH    240
#define LCD_HEIGHT   240

void bsp_st7789_init(void);
void bsp_st7789_set_backlight(uint8_t brightness_pct);
void bsp_st7789_fill(uint16_t color);
void bsp_st7789_draw_bitmap(int x_start, int y_start, int x_end, int y_end, const void *color_data);

#endif // HAL_ST7789_H
