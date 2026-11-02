#ifndef SIM_AUDIO_H
#define SIM_AUDIO_H

#include "sprite_types.h"

#ifdef __cplusplus
extern "C" {
#endif

void sim_audio_init(void);
void sim_audio_play(sound_t sound, bool force_mute);

#ifdef __cplusplus
}
#endif

#endif // SIM_AUDIO_H
