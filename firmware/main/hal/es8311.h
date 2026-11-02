#ifndef HAL_ES8311_H
#define HAL_ES8311_H

#include <stdint.h>
#include <stdbool.h>

// ES8311 Pinout (SPEC-003 第 1.2 节)
#define AUDIO_PIN_BCLK  15
#define AUDIO_PIN_LRCK  16
#define AUDIO_PIN_SDOUT 17
#define AUDIO_PIN_SDA   18
#define AUDIO_PIN_SCL   8
#define AUDIO_PIN_PA_EN 47

void bsp_es8311_init(void);
void bsp_es8311_set_mute(bool mute);
void bsp_es8311_set_volume(uint8_t volume_pct);

#endif // HAL_ES8311_H
