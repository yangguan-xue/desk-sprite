#ifndef HAL_TTP223_H
#define HAL_TTP223_H

#include <stdbool.h>

// TTP223 Pinout (SPEC-003 第 1.2 节)
#define TOUCH_PIN_KEY 4

void bsp_ttp223_init(void);
bool bsp_ttp223_read(void);

#endif // HAL_TTP223_H
