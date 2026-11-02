#include "es8311.h"
#include <stdio.h>

void bsp_es8311_init(void) {
    printf("[BSP ES8311] Initialized I2S & I2C Codec (BCLK=%d, LRCK=%d, SDOUT=%d, PA_EN=%d)\n",
           AUDIO_PIN_BCLK, AUDIO_PIN_LRCK, AUDIO_PIN_SDOUT, AUDIO_PIN_PA_EN);
}

void bsp_es8311_set_mute(bool mute) {
    printf("[BSP ES8311] PA_EN set to %s (PA_EN pin %d)\n",
           mute ? "LOW (Muted)" : "HIGH (Enabled)", AUDIO_PIN_PA_EN);
}

void bsp_es8311_set_volume(uint8_t volume_pct) {
    printf("[BSP ES8311] Volume set to %d%%\n", volume_pct);
}
