#ifndef OOT_PIANO_H
#define OOT_PIANO_H

#include <stddef.h>
#include <stdint.h>

void oot_piano_open(const char *rom_path, const void *rom, size_t rom_size);
void oot_piano_close(void);
void oot_piano_mix(int16_t *stereo_samples, size_t frames);

#endif
