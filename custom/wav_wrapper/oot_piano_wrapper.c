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
 * OoT Piano libretro wrapper - prototype 0.1
 *
 * This DLL does NOT emulate N64 itself. It loads the official
 * mupen64plus_next_libretro.dll from the same RetroArch cores directory,
 * forwards the complete libretro API, and intercepts audio callbacks.
 *
 * Prototype behaviour:
 *   RetroArch/system/OoT-Piano/test.wav is looped and mixed with the
 *   official Mupen64Plus-Next audio output. The intended test ROM is the
 *   user's existing music-muted OoT ROM, so native SFX remain audible.
 */

static HMODULE g_self_module = NULL;
static HMODULE g_core_module = NULL;
static retro_environment_t g_frontend_environment = NULL;
static retro_audio_sample_t g_frontend_audio_sample = NULL;
static retro_audio_sample_batch_t g_frontend_audio_batch = NULL;
static retro_log_printf_t g_log = NULL;

static double g_output_sample_rate = 44100.0;
static int16_t *g_mix_buffer = NULL;
static size_t g_mix_buffer_frames = 0;

struct wav_state
{
   float *stereo;          /* interleaved float stereo */
   size_t frames;
   uint32_t sample_rate;
   double position;
   bool loaded;
};

static struct wav_state g_wav = {0};
static const float g_piano_gain = 0.75f;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
   (void)reserved;
   if (reason == DLL_PROCESS_ATTACH)
      g_self_module = (HMODULE)instance;
   return TRUE;
}

/* Function pointers for the official Mupen64Plus-Next core. */
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

static void frontend_message(const char *text)
{
   if (g_frontend_environment)
   {
      struct retro_message message;
      message.msg = text;
      message.frames = 240;
      g_frontend_environment(RETRO_ENVIRONMENT_SET_MESSAGE, &message);
   }
}

static void acquire_log_interface(void)
{
   if (g_frontend_environment)
   {
      struct retro_log_callback cb;
      memset(&cb, 0, sizeof(cb));
      if (g_frontend_environment(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &cb))
         g_log = cb.log;
   }
}

static int get_core_directory(char *buffer, size_t capacity)
{
   DWORD length;
   char *slash_back;
   char *slash_forward;
   char *slash;

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

static float clamp_float(float value)
{
   if (value > 1.0f) return 1.0f;
   if (value < -1.0f) return -1.0f;
   return value;
}

static float decode_pcm_sample(const unsigned char *p, uint16_t format, uint16_t bits)
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

static void unload_wav(void)
{
   free(g_wav.stereo);
   memset(&g_wav, 0, sizeof(g_wav));
}

static int load_wav_file(const char *path)
{
   FILE *file = NULL;
   unsigned char riff[12];
   unsigned char fmt[64];
   size_t fmt_size = 0;
   long data_offset = -1;
   uint32_t data_size = 0;
   uint16_t format = 0;
   uint16_t channels = 0;
   uint32_t sample_rate = 0;
   uint16_t block_align = 0;
   uint16_t bits = 0;
   unsigned char *raw = NULL;
   float *decoded = NULL;
   size_t frames = 0;
   size_t frame;

   unload_wav();

   file = fopen(path, "rb");
   if (!file)
      return 0;

   if (fread(riff, 1, sizeof(riff), file) != sizeof(riff) ||
       memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0)
      goto fail;

   for (;;)
   {
      unsigned char chunk_header[8];
      uint32_t chunk_size;
      long payload_offset;

      if (fread(chunk_header, 1, sizeof(chunk_header), file) != sizeof(chunk_header))
         break;

      chunk_size = read_u32_le(chunk_header + 4);
      payload_offset = ftell(file);
      if (payload_offset < 0)
         goto fail;

      if (memcmp(chunk_header, "fmt ", 4) == 0)
      {
         fmt_size = chunk_size < sizeof(fmt) ? chunk_size : sizeof(fmt);
         memset(fmt, 0, sizeof(fmt));
         if (fmt_size && fread(fmt, 1, fmt_size, file) != fmt_size)
            goto fail;
      }
      else if (memcmp(chunk_header, "data", 4) == 0)
      {
         data_offset = payload_offset;
         data_size = chunk_size;
      }

      if (fseek(file, payload_offset + (long)chunk_size + (long)(chunk_size & 1U), SEEK_SET) != 0)
         break;
   }

   if (fmt_size < 16 || data_offset < 0 || data_size == 0)
      goto fail;

   format = read_u16_le(fmt + 0);
   channels = read_u16_le(fmt + 2);
   sample_rate = read_u32_le(fmt + 4);
   block_align = read_u16_le(fmt + 12);
   bits = read_u16_le(fmt + 14);

   /* WAVE_FORMAT_EXTENSIBLE: SubFormat GUID starts at byte 24. */
   if (format == 0xFFFE && fmt_size >= 40)
   {
      uint32_t subformat = read_u32_le(fmt + 24);
      if (subformat == 1 || subformat == 3)
         format = (uint16_t)subformat;
   }

   if ((format != 1 && format != 3) || channels == 0 || sample_rate == 0 || block_align == 0)
      goto fail;
   if (format == 3 && bits != 32)
      goto fail;
   if (format == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32)
      goto fail;

   frames = (size_t)(data_size / block_align);
   if (frames == 0 || frames > (SIZE_MAX / (sizeof(float) * 2)))
      goto fail;

   raw = (unsigned char *)malloc(data_size);
   decoded = (float *)malloc(frames * 2 * sizeof(float));
   if (!raw || !decoded)
      goto fail;

   if (fseek(file, data_offset, SEEK_SET) != 0 || fread(raw, 1, data_size, file) != data_size)
      goto fail;

   for (frame = 0; frame < frames; ++frame)
   {
      const unsigned char *base = raw + frame * block_align;
      const unsigned bytes_per_sample = bits / 8;
      float left;
      float right;

      left = decode_pcm_sample(base, format, bits);
      if (channels == 1)
         right = left;
      else
         right = decode_pcm_sample(base + bytes_per_sample, format, bits);

      decoded[frame * 2 + 0] = left;
      decoded[frame * 2 + 1] = right;
   }

   free(raw);
   fclose(file);

   g_wav.stereo = decoded;
   g_wav.frames = frames;
   g_wav.sample_rate = sample_rate;
   g_wav.position = 0.0;
   g_wav.loaded = true;
   return 1;

fail:
   free(raw);
   free(decoded);
   if (file)
      fclose(file);
   unload_wav();
   return 0;
}

static void load_test_wav(void)
{
   const char *system_directory = NULL;
   char path[4096];

   if (!g_frontend_environment ||
       !g_frontend_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory) ||
       !system_directory)
   {
      frontend_message("OoT Piano: RetroArch system folder unavailable");
      return;
   }

   if (snprintf(path, sizeof(path), "%s\\OoT-Piano\\test.wav", system_directory) <= 0)
      return;

   if (load_wav_file(path))
   {
      if (g_log)
         g_log(RETRO_LOG_INFO, "[OoT Piano] Loaded WAV: %s (%u Hz, %zu frames)\n",
               path, (unsigned)g_wav.sample_rate, g_wav.frames);
      frontend_message("OoT Piano: test.wav loaded - WAV + game audio active");
   }
   else
   {
      if (g_log)
         g_log(RETRO_LOG_WARN, "[OoT Piano] Could not load WAV: %s\n", path);
      frontend_message("OoT Piano: test.wav not found/unsupported");
   }
}

static int16_t clamp_i16(int32_t value)
{
   if (value > 32767) return 32767;
   if (value < -32768) return -32768;
   return (int16_t)value;
}

static void wav_next_sample(float *left, float *right)
{
   size_t current;
   size_t next;
   double fraction;
   double step;
   float l0, r0, l1, r1;

   *left = 0.0f;
   *right = 0.0f;

   if (!g_wav.loaded || !g_wav.stereo || g_wav.frames == 0 || g_output_sample_rate <= 1.0)
      return;

   while (g_wav.position >= (double)g_wav.frames)
      g_wav.position -= (double)g_wav.frames;

   current = (size_t)g_wav.position;
   next = current + 1;
   if (next >= g_wav.frames)
      next = 0;

   fraction = g_wav.position - (double)current;
   l0 = g_wav.stereo[current * 2 + 0];
   r0 = g_wav.stereo[current * 2 + 1];
   l1 = g_wav.stereo[next * 2 + 0];
   r1 = g_wav.stereo[next * 2 + 1];

   *left = (float)(l0 + (l1 - l0) * fraction);
   *right = (float)(r0 + (r1 - r0) * fraction);

   step = (double)g_wav.sample_rate / g_output_sample_rate;
   g_wav.position += step;
}

static void RETRO_CALLCONV audio_sample_proxy(int16_t left, int16_t right)
{
   float piano_left, piano_right;
   int32_t mixed_left, mixed_right;

   if (!g_frontend_audio_sample)
      return;

   if (!g_wav.loaded)
   {
      g_frontend_audio_sample(left, right);
      return;
   }

   wav_next_sample(&piano_left, &piano_right);
   mixed_left = (int32_t)left + (int32_t)(piano_left * 32767.0f * g_piano_gain);
   mixed_right = (int32_t)right + (int32_t)(piano_right * 32767.0f * g_piano_gain);
   g_frontend_audio_sample(clamp_i16(mixed_left), clamp_i16(mixed_right));
}

static size_t RETRO_CALLCONV audio_batch_proxy(const int16_t *data, size_t frames)
{
   size_t i;

   if (!g_frontend_audio_batch)
      return frames;

   if (!g_wav.loaded || !data)
      return g_frontend_audio_batch(data, frames);

   if (frames > g_mix_buffer_frames)
   {
      int16_t *new_buffer = (int16_t *)realloc(g_mix_buffer, frames * 2 * sizeof(int16_t));
      if (!new_buffer)
         return g_frontend_audio_batch(data, frames);
      g_mix_buffer = new_buffer;
      g_mix_buffer_frames = frames;
   }

   for (i = 0; i < frames; ++i)
   {
      float piano_left, piano_right;
      int32_t mixed_left, mixed_right;
      wav_next_sample(&piano_left, &piano_right);

      mixed_left = (int32_t)data[i * 2 + 0] +
                   (int32_t)(piano_left * 32767.0f * g_piano_gain);
      mixed_right = (int32_t)data[i * 2 + 1] +
                    (int32_t)(piano_right * 32767.0f * g_piano_gain);

      g_mix_buffer[i * 2 + 0] = clamp_i16(mixed_left);
      g_mix_buffer[i * 2 + 1] = clamp_i16(mixed_right);
   }

   return g_frontend_audio_batch(g_mix_buffer, frames);
}

RETRO_API void retro_set_environment(retro_environment_t cb)
{
   g_frontend_environment = cb;
   acquire_log_interface();
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
   if (ensure_core_loaded()) core_retro_deinit();
   unload_wav();
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
      info->library_name = "Mupen64Plus-Next OoT Piano";
      info->library_version = "WAV Wrapper 0.1";
      return;
   }

   memset(info, 0, sizeof(*info));
   info->library_name = "Mupen64Plus-Next OoT Piano";
   info->library_version = "WAV Wrapper 0.1 (missing base core)";
   info->valid_extensions = "n64|v64|z64|ndd|bin|u1";
   info->need_fullpath = false;
   info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info)
{
   if (ensure_core_loaded()) core_retro_get_system_av_info(info);
}

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device)
{
   if (ensure_core_loaded()) core_retro_set_controller_port_device(port, device);
}

RETRO_API void retro_reset(void)
{
   g_wav.position = 0.0;
   if (ensure_core_loaded()) core_retro_reset();
}

RETRO_API void retro_run(void)
{
   if (ensure_core_loaded()) core_retro_run();
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
      frontend_message("OoT Piano: official Mupen64Plus-Next core not found");
      return false;
   }

   ok = core_retro_load_game(game);
   if (!ok)
      return false;

   memset(&av, 0, sizeof(av));
   core_retro_get_system_av_info(&av);
   if (av.timing.sample_rate > 1.0)
      g_output_sample_rate = av.timing.sample_rate;

   load_test_wav();
   return true;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
      const struct retro_game_info *info, size_t num_info)
{
   bool ok;
   struct retro_system_av_info av;

   if (!ensure_core_loaded()) return false;
   ok = core_retro_load_game_special(game_type, info, num_info);
   if (!ok) return false;

   memset(&av, 0, sizeof(av));
   core_retro_get_system_av_info(&av);
   if (av.timing.sample_rate > 1.0)
      g_output_sample_rate = av.timing.sample_rate;
   load_test_wav();
   return true;
}

RETRO_API void retro_unload_game(void)
{
   unload_wav();
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
