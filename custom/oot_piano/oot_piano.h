#ifndef OOT_PIANO_H
#define OOT_PIANO_H

#include <stddef.h>
#include <stdint.h>

/* Reset per-game playback positions and lazy-loaded audio files. */
void oot_piano_reset(void);

/* Free loaded external audio. */
void oot_piano_shutdown(void);

/* Mix external OoT piano music into 44.1 kHz stereo int16 output. */
void oot_piano_mix(int16_t *samples, size_t frames);

#endif
