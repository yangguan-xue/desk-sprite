#include "st7789.h"
#include <stdio.h>

void bsp_st7789_init(void) {
    // 物理硬件环境初始化 SPI 屏幕与引脚
    printf("[BSP ST7789] Initialized SPI LCD (MOSI=%d, SCLK=%d, CS=%d, DC=%d, RST=%d, BL=%d)\n",
           LCD_PIN_MOSI, LCD_PIN_SCLK, LCD_PIN_CS, LCD_PIN_DC, LCD_PIN_RST, LCD_PIN_BL);
}

void bsp_st7789_set_backlight(uint8_t brightness_pct) {
    printf("[BSP ST7789] Set backlight to %d%%\n", brightness_pct);
}
