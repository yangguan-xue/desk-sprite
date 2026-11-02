#include "ttp223.h"
#include <stdio.h>

void bsp_ttp223_init(void) {
    printf("[BSP TTP223] Configured touch button input on GPIO %d\n", TOUCH_PIN_KEY);
}

bool bsp_ttp223_read(void) {
    // 物理硬件环境读取 GPIO 电平
    return false;
}
