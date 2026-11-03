#ifndef HAL_TTP223_H
#define HAL_TTP223_H

#include <stdbool.h>
#include <stdint.h>

// 触摸按键 Pinout (实测硬件 S3 原生触摸直连 GPIO5 / TOUCH_PAD_NUM5)
#define TOUCH_PIN_KEY 5

void bsp_touch_init(void);
bool bsp_touch_read(void);
uint32_t bsp_touch_get_raw(void);

// 兼容旧接口别名 (SPEC-007 / R4-05)
#define bsp_ttp223_init bsp_touch_init
#define bsp_ttp223_read bsp_touch_read

#endif // HAL_TTP223_H
