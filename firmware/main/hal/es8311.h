#ifndef HAL_ES8311_H
#define HAL_ES8311_H

#include <stdint.h>
#include <stdbool.h>

// ES8311 Pinout (实测 USB 小电视 S3 硬件板型 board-profile.json)
#define AUDIO_PIN_SDA    40
#define AUDIO_PIN_SCL    39
#define AUDIO_PIN_MCLK   38
#define AUDIO_PIN_BCLK   16
#define AUDIO_PIN_LRCK   7
#define AUDIO_PIN_SDOUT  4
#define AUDIO_PIN_PA_EN  8
#define ES8311_I2C_ADDR  0x18

void bsp_es8311_init(void);
void bsp_es8311_set_mute(bool mute);
void bsp_es8311_set_volume(uint8_t volume_pct);
void bsp_es8311_play_tone(uint32_t freq_hz, uint32_t duration_ms);

#endif // HAL_ES8311_H
