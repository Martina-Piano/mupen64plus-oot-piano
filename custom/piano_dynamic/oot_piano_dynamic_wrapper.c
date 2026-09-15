#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libretro.h"

/*
 * OoT Piano dynamic prototype
 *
 * Thin libretro wrapper around the user's installed official
 * mupen64plus_next_libretro.dll.
 *
 * Prototype mapping:
 *   OoT P1 sequence 0x3C -> Kokiri Forest WAV
 *   OoT P3 sequence 0x1A -> Battle WAV
 *
 * The original N64 music table is muted in RDRAM, while native SFX remain.
 * Both external WAV playheads keep advancing while their corresponding
 * OoT sequence player is active, even when OoT fades its volume to zero.
 * This preserves musical position across dynamic battle crossfades.
 *
 * Looping for this prototype is whole-file. Exact musical loop points will
 * be added after the user supplies them.
 */

static HMODULE g_self_module = NULL;
static HMODULE g_core_module = NULL;
static retro_environment_t g_frontend_environment = NULL;
static retro_audio_sample_t g_frontend_audio_sample = NULL;
static retro_audio_sample_batch_t g_frontend_audio_batch = NULL;

static double g_output_sample_rate = 44100.0;
static int16_t *g_mix_buffer = NULL;
static size_t g_mix_buffer_frames = 0;
static bool g_game_loaded = false;
static unsigned g_diag_frame = 0;

struct wav_track
{
   float *stereo;
   size_t frames;
   uint32_t sample_rate;
   double position;
   bool loaded;
   bool active;
   bool was_active;
   float volume;
};

static struct wav_track g_kokiri = {0};
static struct wav_track g_battle = {0};
static const float g_master_gain = 0.75f;

static const char *KOKIRI_FILE =
   "Zelda 05 ~ Ocarina of Time ~ 02 Kokiri Forest.wav";
static const char *BATTLE_FILE =
   "Zelda 05 ~ Ocarina of Time ~ Battle.wav";

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
   (void)reserved;
   if (reason == DLL_PROCESS_ATTACH)
      g_self_module = (HMODULE)instance;
   return TRUE;
}

/* Official Mupen64Plus-Next function pointers. */
typedef void (RETRO_CALLCONV *fn_retro_set_environment)(retro_environment_t);
typedef void (RETRO_CALLCONV *fn_retro_set_video_refresh)(retro_video_refresh_t);
typedef void (RETRO_CALLCONV *fn_retro_set_audio_sample)(retro_audio_sample_t);
typedef void (RETRO_CALLCONV *fn_retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
typedef void (RETRO_CALLCONV *fn_retro_set_input_poll)(retro_input_poll_t);
typedef void (RETRO_CALLCONV *fn_retro_set_input_state)(retro_input_state_t);
typedef void (RETRO_CALLCONV *fn_retro_init)(void);
typedef void (RETRO_CALLCONV *fn_retro_deinit)(void);
typedef unsigned (RETRO_CALLCONV *fn_retro_api_version)(void);
typedef void (RETRO_CALLCONV *fn_retro_get_system_info)(struct retro_system_info *);
typedef void (RETRO_CALLCONV *fn_retro_get_system_av_info)(struct retro_system_av_info *);
typedef void (RETRO_CALLCONV *fn_retro_set_controller_port_device)(unsigned, unsigned);
typedef void (RETRO_CALLCONV *fn_retro_reset)(void);
typedef void (RETRO_CALLCONV *fn_retro_run)(void);
typedef size_t (RETRO_CALLCONV *fn_retro_serialize_size)(void);
typedef bool (RETRO_CALLCONV *fn_retro_serialize)(void *, size_t);
typedef bool (RETRO_CALLCONV *fn_retro_unserialize)(const void *, size_t);
typedef void (RETRO_CALLCONV *fn_retro_cheat_reset)(void);
typedef void (RETRO_CALLCONV *fn_retro_cheat_set)(unsigned, bool, const char *);
typedef bool (RETRO_CALLCONV *fn_retro_load_game)(const struct retro_game_info *);
typedef bool (RETRO_CALLCONV *fn_retro_load_game_special)(unsigned, const struct retro_game_info *, size_t);
typedef void (RETRO_CALLCONV *fn_retro_unload_game)(void);
typedef unsigned (RETRO_CALLCONV *fn_retro_get_region)(void);
typedef void *(RETRO_CALLCONV *fn_retro_get_memory_data)(unsigned);
typedef size_t (RETRO_CALLCONV *fn_retro_get_memory_size)(unsigned);

static fn_retro_set_environment core_retro_set_environment = NULL;
static fn_retro_set_video_refresh core_retro_set_video_refresh = NULL;
static fn_retro_set_audio_sample core_retro_set_audio_sample = NULL;
static fn_retro_set_audio_sample_batch core_retro_set_audio_sample_batch = NULL;
static fn_retro_set_input_poll core_retro_set_input_poll = NULL;
static fn_retro_set_input_state core_retro_set_input_state = NULL;
static fn_retro_init core_retro_init = NULL;
static fn_retro_deinit core_retro_deinit = NULL;
static fn_retro_api_version core_retro_api_version = NULL;
static fn_retro_get_system_info core_retro_get_system_info = NULL;
static fn_retro_get_system_av_info core_retro_get_system_av_info = NULL;
static fn_retro_set_controller_port_device core_retro_set_controller_port_device = NULL;
static fn_retro_reset core_retro_reset = NULL;
static fn_retro_run core_retro_run = NULL;
static fn_retro_serialize_size core_retro_serialize_size = NULL;
static fn_retro_serialize core_retro_serialize = NULL;
static fn_retro_unserialize core_retro_unserialize = NULL;
static fn_retro_cheat_reset core_retro_cheat_reset = NULL;
static fn_retro_cheat_set core_retro_cheat_set = NULL;
static fn_retro_load_game core_retro_load_game = NULL;
static fn_retro_load_game_special core_retro_load_game_special = NULL;
static fn_retro_unload_game core_retro_unload_game = NULL;
static fn_retro_get_region core_retro_get_region = NULL;
static fn_retro_get_memory_data core_retro_get_memory_data = NULL;
static fn_retro_get_memory_size core_retro_get_memory_size = NULL;

static void frontend_message(const char *text, unsigned frames)
{
   if (g_frontend_environment)
   {
      struct retro_message message;
      message.msg = text;
      message.frames = frames;
      g_frontend_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
   }
}

static int get_core_directory(char *buffer, size_t capacity)
{
   DWORD length;
   char *slash_back, *slash_forward, *slash;

   if (!buffer || capacity < 8 || !g_self_module)
      return 0;

   length = GetModuleFileNameA(g_self_module, buffer, (DWORD)capacity);
   if (length == 0 || length >= capacity)
      return 0;

   slash_back = strrchr(buffer, '\\');
   slash_forward = strrchr(buffer, '/');
   slash = slash_back;
   if (!slash || (slash_forward && slash_forward > slash))
      slash = slash_forward;

   if (slash)
      slash[1] = '\0';
   else
      buffer[0] = '\0';

   return 1;
}

static int load_symbol(FARPROC *target, const char *name)
{
   FARPROC symbol = GetProcAddress(g_core_module, name);
   if (!symbol)
      return 0;
   *target = symbol;
   return 1;
}

static int ensure_core_loaded(void)
{
   char directory[4096];
   char path[4096];

   if (g_core_module)
      return 1;

   if (!get_core_directory(directory, sizeof(directory)))
      return 0;

   if (snprintf(path, sizeof(path), "%smupen64plus_next_libretro.dll", directory) <= 0)
      return 0;

   g_core_module = LoadLibraryA(path);
   if (!g_core_module)
      return 0;

#define LOAD_REQUIRED(name) \
   do { if (!load_symbol((FARPROC *)&core_##name, #name)) return 0; } while (0)

   LOAD_REQUIRED(retro_set_environment);
   LOAD_REQUIRED(retro_set_video_refresh);
   LOAD_REQUIRED(retro_set_audio_sample);
   LOAD_REQUIRED(retro_set_audio_sample_batch);
   LOAD_REQUIRED(retro_set_input_poll);
   LOAD_REQUIRED(retro_set_input_state);
   LOAD_REQUIRED(retro_init);
   LOAD_REQUIRED(retro_deinit);
   LOAD_REQUIRED(retro_api_version);
   LOAD_REQUIRED(retro_get_system_info);
   LOAD_REQUIRED(retro_get_system_av_info);
   LOAD_REQUIRED(retro_set_controller_port_device);
   LOAD_REQUIRED(retro_reset);
   LOAD_REQUIRED(retro_run);
   LOAD_REQUIRED(retro_serialize_size);
   LOAD_REQUIRED(retro_serialize);
   LOAD_REQUIRED(retro_unserialize);
   LOAD_REQUIRED(retro_cheat_reset);
   LOAD_REQUIRED(retro_cheat_set);
   LOAD_REQUIRED(retro_load_game);
   LOAD_REQUIRED(retro_load_game_special);
   LOAD_REQUIRED(retro_unload_game);
   LOAD_REQUIRED(retro_get_region);
   LOAD_REQUIRED(retro_get_memory_data);
   LOAD_REQUIRED(retro_get_memory_size);

#undef LOAD_REQUIRED
   return 1;
}

static uint16_t read_u16_le(const unsigned char *p)
{
   return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const unsigned char *p)
{
   return (uint32_t)p[0] |
          ((uint32_t)p[1] << 8) |
          ((uint32_t)p[2] << 16) |
          ((uint32_t)p[3] << 24);
}

static float clamp_float(float v)
{
   if (v > 1.0f) return 1.0f;
   if (v < -1.0f) return -1.0f;
   return v;
}

static int16_t clamp_i16(int32_t v)
{
   if (v > 32767) return 32767;
   if (v < -32768) return -32768;
   return (int16_t)v;
}

static void unload_track(struct wav_track *track)
{
   if (!track) return;
   free(track->stereo);
   memset(track, 0, sizeof(*track));
}

/* Prototype loader: PCM WAV 8/16/24/32-bit or 32-bit IEEE float. */
static float decode_sample(const unsigned char *p, uint16_t format, uint16_t bits)
{
   if (format == 3 && bits == 32)
   {
      float value;
      memcpy(&value, p, sizeof(value));
      return clamp_float(value);
   }

   if (format != 1)
      return 0.0f;

   switch (bits)
   {
      case 8:
         return ((float)((int)p[0] - 128)) / 128.0f;
      case 16:
      {
         int16_t value = (int16_t)read_u16_le(p);
         return (float)value / 32768.0f;
      }
      case 24:
      {
         int32_t value = (int32_t)((uint32_t)p[0] |
                                   ((uint32_t)p[1] << 8) |
                                   ((uint32_t)p[2] << 16));
         if (value & 0x00800000)
            value |= (int32_t)0xFF000000;
         return (float)value / 8388608.0f;
      }
      case 32:
      {
         int32_t value = (int32_t)read_u32_le(p);
         return (float)((double)value / 2147483648.0);
      }
      default:
         return 0.0f;
   }
}

static int load_wav_file(struct wav_track *track, const char *path)
{
   FILE *file = NULL;
   unsigned char riff[12];
   unsigned char fmt[64];
   size_t fmt_size = 0;
   long data_offset = -1;
   uint32_t data_size = 0;
   uint16_t format = 0, channels = 0, block_align = 0, bits = 0;
   uint32_t sample_rate = 0;
   unsigned char *raw = NULL;
   float *decoded = NULL;
   size_t frames = 0, frame;

   unload_track(track);

   file = fopen(path, "rb");
   if (!file)
      return 0;

   if (fread(riff, 1, sizeof(riff), file) != sizeof(riff) ||
       memcmp(riff, "RIFF", 4) != 0 ||
       memcmp(riff + 8, "WAVE", 4) != 0)
      goto fail;

   for (;;)
   {
      unsigned char header[8];
      uint32_t chunk_size;
      long payload;

      if (fread(header, 1, sizeof(header), file) != sizeof(header))
         break;

      chunk_size = read_u32_le(header + 4);
      payload = ftell(file);
      if (payload < 0)
         goto fail;

      if (memcmp(header, "fmt ", 4) == 0)
      {
         fmt_size = chunk_size < sizeof(fmt) ? chunk_size : sizeof(fmt);
         memset(fmt, 0, sizeof(fmt));
         if (fmt_size && fread(fmt, 1, fmt_size, file) != fmt_size)
            goto fail;
      }
      else if (memcmp(header, "data", 4) == 0)
      {
         data_offset = payload;
         data_size = chunk_size;
      }

      if (fseek(file, payload + (long)chunk_size + (long)(chunk_size & 1U), SEEK_SET) != 0)
         break;
   }

   if (fmt_size < 16 || data_offset < 0 || data_size == 0)
      goto fail;

   format = read_u16_le(fmt + 0);
   channels = read_u16_le(fmt + 2);
   sample_rate = read_u32_le(fmt + 4);
   block_align = read_u16_le(fmt + 12);
   bits = read_u16_le(fmt + 14);

   if (format == 0xFFFE && fmt_size >= 40)
   {
      uint32_t subformat = read_u32_le(fmt + 24);
      if (subformat == 1 || subformat == 3)
         format = (uint16_t)subformat;
   }

   if ((format != 1 && format != 3) || channels == 0 ||
       sample_rate == 0 || block_align == 0)
      goto fail;
   if (format == 3 && bits != 32)
      goto fail;
   if (format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
      goto fail;

   frames = (size_t)(data_size / block_align);
   if (!frames || frames > (SIZE_MAX / (sizeof(float) * 2)))
      goto fail;

   raw = (unsigned char *)malloc(data_size);
   decoded = (float *)malloc(frames * 2 * sizeof(float));
   if (!raw || !decoded)
      goto fail;

   if (fseek(file, data_offset, SEEK_SET) != 0 ||
       fread(raw, 1, data_size, file) != data_size)
      goto fail;

   for (frame = 0; frame < frames; ++frame)
   {
      const unsigned char *base = raw + frame * block_align;
      unsigned bytes_per_sample = bits / 8;
      float left = decode_sample(base, format, bits);
      float right = channels == 1
         ? left
         : decode_sample(base + bytes_per_sample, format, bits);

      decoded[frame * 2 + 0] = left;
      decoded[frame * 2 + 1] = right;
   }

   free(raw);
   fclose(file);

   track->stereo = decoded;
   track->frames = frames;
   track->sample_rate = sample_rate;
   track->position = 0.0;
   track->loaded = true;
   track->active = false;
   track->was_active = false;
   track->volume = 0.0f;
   return 1;

fail:
   free(raw);
   free(decoded);
   if (file) fclose(file);
   unload_track(track);
   return 0;
}

static int load_named_track(struct wav_track *track, const char *filename)
{
   const char *system_directory = NULL;
   char path[4096];

   if (!g_frontend_environment ||
       !g_frontend_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory) ||
       !system_directory)
      return 0;

   if (snprintf(path, sizeof(path), "%s\\OoT-Piano\\%s",
                system_directory, filename) <= 0)
      return 0;

   return load_wav_file(track, path);
}

static void load_music_files(void)
{
   int kokiri_ok = load_named_track(&g_kokiri, KOKIRI_FILE);
   int battle_ok = load_named_track(&g_battle, BATTLE_FILE);

   if (kokiri_ok && battle_ok)
      frontend_message("OoT Piano Dynamic: Kokiri + Battle WAV loaded", 240);
   else if (!kokiri_ok && !battle_ok)
      frontend_message("OoT Piano Dynamic: both WAV files missing/unsupported", 240);
   else if (!kokiri_ok)
      frontend_message("OoT Piano Dynamic: Kokiri WAV missing/unsupported", 240);
   else
      frontend_message("OoT Piano Dynamic: Battle WAV missing/unsupported", 240);
}

static uint32_t rdram_offset(uint32_t virtual_address)
{
   return virtual_address & 0x007fffffU;
}

static uint8_t rdram_read8(const uint8_t *ram, size_t size, uint32_t address)
{
   uint32_t off = rdram_offset(address);
   uint32_t physical = off ^ 3U;
   if (!ram || physical >= size)
      return 0;
   return ram[physical];
}

static float rdram_read_f32(const uint8_t *ram, size_t size, uint32_t address)
{
   uint32_t off = rdram_offset(address);
   uint32_t word = 0;
   float value = 0.0f;

   if (!ram || (off & 3U) != 0 || off + 4U > size)
      return 0.0f;

   memcpy(&word, ram + off, sizeof(word));
   memcpy(&value, &word, sizeof(value));

   if (!(value >= 0.0f && value < 100.0f))
      return 0.0f;
   return value;
}

struct player_state
{
   uint8_t flags;
   uint8_t id;
   float volume;
};

static struct player_state read_player(const uint8_t *ram, size_t size, uint32_t base)
{
   struct player_state p;
   float a, b;

   p.flags = rdram_read8(ram, size, base + 0x00U);
   p.id = rdram_read8(ram, size, base + 0x04U);
   a = rdram_read_f32(ram, size, base + 0x1cU);
   b = rdram_read_f32(ram, size, base + 0x2cU);
   p.volume = a * b;
   if (!(p.volume >= 0.0f && p.volume <= 1.5f))
      p.volume = 0.0f;
   return p;
}

static void set_track_state(struct wav_track *track, bool active, float volume)
{
   if (!track)
      return;

   track->active = active && track->loaded;
   track->volume = volume;
   if (track->volume < 0.0f) track->volume = 0.0f;
   if (track->volume > 1.0f) track->volume = 1.0f;

   if (track->active && !track->was_active)
      track->position = 0.0;

   track->was_active = track->active;
}

static void update_music_state(bool show_diag)
{
   const uint8_t *ram;
   size_t size;
   struct player_state p1, p3;

   if (!g_game_loaded || !core_retro_get_memory_data || !core_retro_get_memory_size)
      return;

   ram = (const uint8_t *)core_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
   size = core_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
   if (!ram || size < 0x200000U)
      return;

   p1 = read_player(ram, size, 0x80128B60U);
   p3 = read_player(ram, size, 0x80128F80U);

   set_track_state(&g_kokiri,
      ((p1.flags & 1U) != 0U) && p1.id == 0x3CU, p1.volume);
   set_track_state(&g_battle,
      ((p3.flags & 1U) != 0U) && p3.id == 0x1AU, p3.volume);

   if (show_diag)
   {
      char msg[220];
      snprintf(msg, sizeof(msg),
         "Dynamic WAV  P1:%02X v%.2f Kok:%s | P3:%02X v%.2f Battle:%s",
         (unsigned)p1.id, (double)p1.volume,
         g_kokiri.active ? "ON" : "off",
         (unsigned)p3.id, (double)p3.volume,
         g_battle.active ? "ON" : "off");
      frontend_message(msg, 20);
   }
}

static void mute_original_music(void)
{
   uint8_t *ram;
   size_t size;
   unsigned i;

   if (!g_game_loaded || !core_retro_get_memory_data || !core_retro_get_memory_size)
      return;

   ram = (uint8_t *)core_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
   size = core_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
   if (!ram || size < 0x200000U)
      return;

   for (i = 0x03U; i < 0x26U; ++i)
   {
      uint32_t address = 0x80113750U + (i * 0x10U);
      uint32_t off = rdram_offset(address);
      uint32_t value = 0xFFFFFFFFU;
      if (off + sizeof(value) <= size)
         memcpy(ram + off, &value, sizeof(value));
   }
}

/* Whole-file looping for the first dynamic test. */
static void track_next_sample(struct wav_track *track, float *left, float *right)
{
   size_t current, next;
   double fraction, step;
   float l0, r0, l1, r1;

   *left = 0.0f;
   *right = 0.0f;

   if (!track || !track->loaded || !track->active ||
       !track->stereo || track->frames == 0 || g_output_sample_rate <= 1.0)
      return;

   while (track->position >= (double)track->frames)
      track->position -= (double)track->frames;

   current = (size_t)track->position;
   next = current + 1;
   if (next >= track->frames)
      next = 0;

   fraction = track->position - (double)current;
   l0 = track->stereo[current * 2 + 0];
   r0 = track->stereo[current * 2 + 1];
   l1 = track->stereo[next * 2 + 0];
   r1 = track->stereo[next * 2 + 1];

   *left = (float)(l0 + (l1 - l0) * fraction);
   *right = (float)(r0 + (r1 - r0) * fraction);

   step = (double)track->sample_rate / g_output_sample_rate;
   track->position += step;
}

static void mix_external(float *left, float *right)
{
   float kl, kr, bl, br;

   track_next_sample(&g_kokiri, &kl, &kr);
   track_next_sample(&g_battle, &bl, &br);

   *left = (kl * g_kokiri.volume + bl * g_battle.volume) * g_master_gain;
   *right = (kr * g_kokiri.volume + br * g_battle.volume) * g_master_gain;
}

static void RETRO_CALLCONV audio_sample_proxy(int16_t left, int16_t right)
{
   float ext_l, ext_r;
   int32_t out_l, out_r;

   if (!g_frontend_audio_sample)
      return;

   mix_external(&ext_l, &ext_r);
   out_l = (int32_t)left + (int32_t)(ext_l * 32767.0f);
   out_r = (int32_t)right + (int32_t)(ext_r * 32767.0f);
   g_frontend_audio_sample(clamp_i16(out_l), clamp_i16(out_r));
}

static size_t RETRO_CALLCONV audio_batch_proxy(const int16_t *data, size_t frames)
{
   size_t i;

   if (!g_frontend_audio_batch)
      return frames;
   if (!data)
      return g_frontend_audio_batch(data, frames);

   if (frames > g_mix_buffer_frames)
   {
      int16_t *new_buffer = (int16_t *)realloc(
         g_mix_buffer, frames * 2 * sizeof(int16_t));
      if (!new_buffer)
         return g_frontend_audio_batch(data, frames);
      g_mix_buffer = new_buffer;
      g_mix_buffer_frames = frames;
   }

   for (i = 0; i < frames; ++i)
   {
      float ext_l, ext_r;
      int32_t out_l, out_r;

      mix_external(&ext_l, &ext_r);
      out_l = (int32_t)data[i * 2 + 0] + (int32_t)(ext_l * 32767.0f);
      out_r = (int32_t)data[i * 2 + 1] + (int32_t)(ext_r * 32767.0f);
      g_mix_buffer[i * 2 + 0] = clamp_i16(out_l);
      g_mix_buffer[i * 2 + 1] = clamp_i16(out_r);
   }

   return g_frontend_audio_batch(g_mix_buffer, frames);
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
   g_frontend_environment = cb;
   if (ensure_core_loaded())
      core_retro_set_environment(cb);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb)
{
   if (ensure_core_loaded()) core_retro_set_video_refresh(cb);
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
   g_frontend_audio_sample = cb;
   if (ensure_core_loaded()) core_retro_set_audio_sample(audio_sample_proxy);
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   g_frontend_audio_batch = cb;
   if (ensure_core_loaded()) core_retro_set_audio_sample_batch(audio_batch_proxy);
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb)
{
   if (ensure_core_loaded()) core_retro_set_input_poll(cb);
}

RETRO_API void retro_set_input_state(retro_input_state_t cb)
{
   if (ensure_core_loaded()) core_retro_set_input_state(cb);
}

RETRO_API void retro_init(void)
{
   if (ensure_core_loaded()) core_retro_init();
}

RETRO_API void retro_deinit(void)
{
   g_game_loaded = false;
   if (ensure_core_loaded()) core_retro_deinit();
   unload_track(&g_kokiri);
   unload_track(&g_battle);
   free(g_mix_buffer);
   g_mix_buffer = NULL;
   g_mix_buffer_frames = 0;
}

RETRO_API unsigned retro_api_version(void)
{
   if (ensure_core_loaded()) return core_retro_api_version();
   return RETRO_API_VERSION;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
   if (!info) return;

   if (ensure_core_loaded())
   {
      core_retro_get_system_info(info);
      info->library_name = "Mupen64Plus-Next OoT Piano Dynamic";
      info->library_version = "Kokiri+Battle 0.1";
      return;
   }

   memset(info, 0, sizeof(*info));
   info->library_name = "Mupen64Plus-Next OoT Piano Dynamic";
   info->library_version = "Kokiri+Battle 0.1 (missing base core)";
   info->valid_extensions = "n64|v64|z64|ndd|bin|u1";
   info->need_fullpath = true;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
   if (ensure_core_loaded()) core_retro_get_system_av_info(info);
   else if (info) memset(info, 0, sizeof(*info));
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if (ensure_core_loaded()) core_retro_set_controller_port_device(port, device);
}

RETRO_API void retro_reset(void)
{
   g_kokiri.position = 0.0;
   g_battle.position = 0.0;
   g_kokiri.was_active = false;
   g_battle.was_active = false;
   if (ensure_core_loaded()) core_retro_reset();
}

RETRO_API void retro_run(void)
{
   if (!ensure_core_loaded())
      return;

   /* Use last known state for the audio generated during this frame. */
   update_music_state(false);
   mute_original_music();

   core_retro_run();

   mute_original_music();
   update_music_state(false);

   g_diag_frame++;
   if ((g_diag_frame % 10U) == 0U)
      update_music_state(true);
}

RETRO_API size_t retro_serialize_size(void)
{
   if (ensure_core_loaded()) return core_retro_serialize_size();
   return 0;
}

RETRO_API bool retro_serialize(void *data, size_t size)
{
   if (ensure_core_loaded()) return core_retro_serialize(data, size);
   return false;
}

RETRO_API bool retro_unserialize(const void *data, size_t size)
{
   if (ensure_core_loaded()) return core_retro_unserialize(data, size);
   return false;
}

RETRO_API void retro_cheat_reset(void)
{
   if (ensure_core_loaded()) core_retro_cheat_reset();
}

RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   if (ensure_core_loaded()) core_retro_cheat_set(index, enabled, code);
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
   struct retro_system_av_info av;
   bool ok;

   if (!ensure_core_loaded())
   {
      frontend_message("OoT Piano Dynamic: official Mupen64Plus-Next not found", 240);
      return false;
   }

   ok = core_retro_load_game(game);
   if (!ok)
      return false;

   g_game_loaded = true;
   g_diag_frame = 0;

   memset(&av, 0, sizeof(av));
   core_retro_get_system_av_info(&av);
   if (av.timing.sample_rate > 1.0)
      g_output_sample_rate = av.timing.sample_rate;

   load_music_files();
   update_music_state(false);
   return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
      const struct retro_game_info *info, size_t num_info)
{
   struct retro_system_av_info av;
   bool ok;

   if (!ensure_core_loaded()) return false;
   ok = core_retro_load_game_special(game_type, info, num_info);
   if (!ok) return false;

   g_game_loaded = true;
   g_diag_frame = 0;

   memset(&av, 0, sizeof(av));
   core_retro_get_system_av_info(&av);
   if (av.timing.sample_rate > 1.0)
      g_output_sample_rate = av.timing.sample_rate;

   load_music_files();
   update_music_state(false);
   return true;
}

RETRO_API void retro_unload_game(void)
{
   g_game_loaded = false;
   unload_track(&g_kokiri);
   unload_track(&g_battle);
   if (ensure_core_loaded()) core_retro_unload_game();
}

RETRO_API unsigned retro_get_region(void)
{
   if (ensure_core_loaded()) return core_retro_get_region();
   return RETRO_REGION_NTSC;
}

RETRO_API void *retro_get_memory_data(unsigned id)
{
   if (ensure_core_loaded()) return core_retro_get_memory_data(id);
   return NULL;
}

RETRO_API size_t retro_get_memory_size(unsigned id)
{
   if (ensure_core_loaded()) return core_retro_get_memory_size(id);
   return 0;
}
