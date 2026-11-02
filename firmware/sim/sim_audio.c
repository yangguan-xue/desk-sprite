#include "sim_audio.h"
#include <stdio.h>
#include <stdlib.h>

void sim_audio_init(void) {
    // 初始化音频桩
}

void sim_audio_play(sound_t sound, bool force_mute) {
    if (force_mute) {
        printf("[Sim Audio] Muted (PA_EN=0), skipping audio output.\n");
        return;
    }

    const char* sound_name = "none";
    const char* sys_sound_path = NULL;

    switch (sound) {
        case SOUND_DING:
            sound_name = "ding";
            sys_sound_path = "/System/Library/Sounds/Ping.aiff";
            break;
        case SOUND_CHIME:
            sound_name = "chime";
            sys_sound_path = "/System/Library/Sounds/Hero.aiff";
            break;
        case SOUND_CHEER:
            sound_name = "cheer";
            sys_sound_path = "/System/Library/Sounds/Glass.aiff";
            break;
        case SOUND_SIGH:
            sound_name = "sigh";
            sys_sound_path = "/System/Library/Sounds/Basso.aiff";
            break;
        case SOUND_ALERT_BEEP:
            sound_name = "alert_beep";
            sys_sound_path = "/System/Library/Sounds/Sosumi.aiff";
            break;
        case SOUND_NONE:
        default:
            return;
    }

    printf("[Sim Audio] Play sound: %s\n", sound_name);

#ifdef __APPLE__
    if (sys_sound_path) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "/usr/bin/afplay '%s' >/dev/null 2>&1 &", sys_sound_path);
        system(cmd);
    }
#endif
}
