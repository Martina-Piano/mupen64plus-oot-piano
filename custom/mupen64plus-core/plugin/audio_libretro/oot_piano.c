/* Two-track OoT USA 1.0 piano playback. The ROM and tracks stay outside the core. */
#include "oot_piano.h"
#include "../../../../mupen64plus-core/src/device/device.h"
#include "../../../../mupen64plus-core/src/osal/preproc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 44100
#define MUSIC_ID_OFFSET 0x0011B9DE
#define HOUSE_ID 0x1f
#define KOKIRI_ID 0x3c

extern struct device g_dev;

struct piano_track {
   int16_t *pcm;
   size_t frames;
   size_t loop_start;
};

static struct piano_track tracks[2];
static int active = -1;
static size_t position;
static int oot_enabled;

void oot_piano_close(void)
{
   unsigned i;
   for (i = 0; i < 2; ++i) {
      free(tracks[i].pcm);
      memset(&tracks[i], 0, sizeof(tracks[i]));
   }
   active = -1;
   position = 0;
   oot_enabled = 0;
}

static void load_track(struct piano_track *track, const char *rom_path,
                       const char *basename, size_t loop_start)
{
   char *path;
   const char *separator;
   size_t dir_len, file_size;
   FILE *file;
   long length;

   if (!rom_path)
      return;
   separator = strrchr(rom_path, '/');
#ifdef _WIN32
   {
      const char *backslash = strrchr(rom_path, '\\');
      if (!separator || (backslash && backslash > separator))
         separator = backslash;
   }
#endif
   dir_len = separator ? (size_t)(separator - rom_path + 1) : 0;
   path = malloc(dir_len + strlen("piano/") + strlen(basename) + 1);
   if (!path)
      return;
   if (dir_len)
      memcpy(path, rom_path, dir_len);
   strcpy(path + dir_len, "piano/");
   strcat(path, basename);
   file = fopen(path, "rb");
   free(path);
   if (!file)
      return;
   if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) <= 0 ||
       (size_t)length % 4 != 0 || fseek(file, 0, SEEK_SET) != 0) {
      fclose(file);
      return;
   }
   file_size = (size_t)length;
   track->pcm = malloc(file_size);
   if (!track->pcm || fread(track->pcm, 1, file_size, file) != file_size) {
      free(track->pcm);
      track->pcm = NULL;
      fclose(file);
      return;
   }
   fclose(file);
   track->frames = file_size / 4;
   track->loop_start = loop_start < track->frames ? loop_start : 0;
}

void oot_piano_open(const char *rom_path, const void *rom, size_t rom_size)
{
   const uint8_t *bytes = rom;
   oot_piano_close();
   /* CZLE, USA, revision 0; do not affect another game's audio. */
   if (!bytes || rom_size < 0x40 ||
       memcmp(bytes + 0x3b, "CZLE", 4) || bytes[0x3f] != 0)
      return;
   oot_enabled = 1;
   load_track(&tracks[0], rom_path, "house.pcm", 29 * SAMPLE_RATE);
   load_track(&tracks[1], rom_path, "kokiri.pcm", 56 * SAMPLE_RATE);
}

static int16_t clamp_sample(int32_t value)
{
   if (value > 32767) return 32767;
   if (value < -32768) return -32768;
   return (int16_t)value;
}

void oot_piano_mix(int16_t *stereo_samples, size_t frames)
{
   const uint8_t *rdram;
   uint8_t seq_id;
   int next;
   size_t i;
   struct piano_track *track;

   if (!oot_enabled || !stereo_samples || !g_dev.rdram.dram)
      return;
   rdram = (const uint8_t *)g_dev.rdram.dram;
   /* Mupen stores N64 bytes in native-endian words. */
   seq_id = rdram[MUSIC_ID_OFFSET ^ S8];
   next = seq_id == HOUSE_ID ? 0 : (seq_id == KOKIRI_ID ? 1 : -1);
   if (next != active) {
      active = next;
      position = 0;
   }
   if (active < 0)
      return;
   track = &tracks[active];
   if (!track->pcm || !track->frames)
      return;
   for (i = 0; i < frames; ++i) {
      size_t index;
      if (position >= track->frames)
         position = track->loop_start;
      index = position++ * 2;
      stereo_samples[2 * i] = clamp_sample((int32_t)stereo_samples[2 * i] +
                           ((int32_t)track->pcm[index] * 3 / 4));
      stereo_samples[2 * i + 1] = clamp_sample((int32_t)stereo_samples[2 * i + 1] +
                           ((int32_t)track->pcm[index + 1] * 3 / 4));
   }
}
