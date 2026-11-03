#ifndef HAL_TTP223_H
#define HAL_TTP223_H

#include <stdbool.h>

// 触摸按键 Pinout (实测硬件 S3 原生触摸直连 GPIO5)
#define TOUCH_PIN_KEY 5

void bsp_ttp223_init(void);
bool bsp_ttp223_read(void);

#endif // HAL_TTP223_H
