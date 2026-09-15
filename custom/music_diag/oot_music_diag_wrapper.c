#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "libretro.h"

/*
 * OoT Music Diagnostics libretro wrapper
 *
 * Loads the user's existing official mupen64plus_next_libretro.dll and
 * forwards the libretro API unchanged. After each emulated frame it reads
 * OoT's three sequence-player structures from RDRAM and shows their music
 * IDs, play flags and effective volumes in RetroArch's on-screen message.
 *
 * This is intentionally a diagnostic core only. It does not replace the
 * already-working OoT Piano WAV wrapper.
 */

static HMODULE g_self_module = NULL;
static HMODULE g_core_module = NULL;
static retro_environment_t g_frontend_environment = NULL;
static unsigned g_diag_frame = 0;
static bool g_game_loaded = false;

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

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
   (void)reserved;
   if (reason == DLL_PROCESS_ATTACH)
      g_self_module = (HMODULE)instance;
   return TRUE;
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

static uint32_t rdram_offset(uint32_t virtual_address)
{
   return virtual_address & 0x007fffffU;
}

/* Mupen64Plus stores each N64 32-bit word in host byte order. On Windows x64
 * a byte read therefore uses the N64 byte offset XOR 3. */
static uint8_t rdram_read8(const uint8_t *ram, size_t size, uint32_t virtual_address)
{
   uint32_t off = rdram_offset(virtual_address);
   uint32_t physical = off ^ 3U;
   if (!ram || physical >= size)
      return 0;
   return ram[physical];
}

static float rdram_read_f32(const uint8_t *ram, size_t size, uint32_t virtual_address)
{
   uint32_t off = rdram_offset(virtual_address);
   uint32_t word = 0;
   float value = 0.0f;

   if (!ram || (off & 3U) != 0 || off + 4U > size)
      return 0.0f;

   memcpy(&word, ram + off, sizeof(word));
   memcpy(&value, &word, sizeof(value));

   /* Reject NaN, infinities, negatives and obviously bogus diagnostic data. */
   if (!(value >= 0.0f && value < 100.0f))
      return 0.0f;
   return value;
}

struct oot_player_diag
{
   uint8_t flags;
   uint8_t id;
   float volume;
};

static struct oot_player_diag read_player(const uint8_t *ram, size_t size, uint32_t base)
{
   struct oot_player_diag p;
   float v1;
   float v2;

   p.flags = rdram_read8(ram, size, base + 0x00U);
   p.id = rdram_read8(ram, size, base + 0x04U);
   v1 = rdram_read_f32(ram, size, base + 0x1cU);
   v2 = rdram_read_f32(ram, size, base + 0x2cU);
   p.volume = v1 * v2;
   if (!(p.volume >= 0.0f && p.volume < 100.0f))
      p.volume = 0.0f;
   return p;
}

static void show_music_diagnostics(void)
{
   const uint8_t *ram;
   size_t size;
   struct oot_player_diag p1;
   struct oot_player_diag p2;
   struct oot_player_diag p3;
   char text[256];

   if (!g_game_loaded || !core_retro_get_memory_data || !core_retro_get_memory_size)
      return;

   ram = (const uint8_t *)core_retro_get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
   size = core_retro_get_memory_size(RETRO_MEMORY_SYSTEM_RAM);

   if (!ram || size < 0x200000U)
   {
      frontend_message("OoT Music Diag: RDRAM unavailable", 30);
      return;
   }

   /* Addresses used by the existing OoT MusicReplacementMod for the USA game. */
   p1 = read_player(ram, size, 0x80128B60U);
   p2 = read_player(ram, size, 0x80128CC0U);
   p3 = read_player(ram, size, 0x80128F80U);

   snprintf(text, sizeof(text),
      "OoT Music  P1:%02X %c v%.2f | P2:%02X %c v%.2f | P3:%02X %c v%.2f",
      (unsigned)p1.id, (p1.flags & 1U) ? '*' : '-', (double)p1.volume,
      (unsigned)p2.id, (p2.flags & 1U) ? '*' : '-', (double)p2.volume,
      (unsigned)p3.id, (p3.flags & 1U) ? '*' : '-', (double)p3.volume);

   frontend_message(text, 20);
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
   if (ensure_core_loaded()) core_retro_set_audio_sample(cb);
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   if (ensure_core_loaded()) core_retro_set_audio_sample_batch(cb);
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
      info->library_name = "Mupen64Plus-Next OoT Music Diag";
      info->library_version = "0.1";
      return;
   }

   memset(info, 0, sizeof(*info));
   info->library_name = "Mupen64Plus-Next OoT Music Diag";
   info->library_version = "0.1 (missing base core)";
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
   g_diag_frame = 0;
   if (ensure_core_loaded()) core_retro_reset();
}

RETRO_API void retro_run(void)
{
   if (!ensure_core_loaded())
      return;

   core_retro_run();

   /* Refresh ~6 times per second on a 60 Hz game. */
   g_diag_frame++;
   if ((g_diag_frame % 10U) == 0U)
      show_music_diagnostics();
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
   bool ok;
   if (!ensure_core_loaded()) return false;
   ok = core_retro_load_game(game);
   g_game_loaded = ok;
   g_diag_frame = 0;
   if (ok)
      frontend_message("OoT Music Diag active - watch P1/P2/P3 IDs and volumes", 240);
   return ok;
}

RETRO_API bool retro_load_game_special(unsigned game_type,
      const struct retro_game_info *info, size_t num_info)
{
   bool ok;
   if (!ensure_core_loaded()) return false;
   ok = core_retro_load_game_special(game_type, info, num_info);
   g_game_loaded = ok;
   g_diag_frame = 0;
   return ok;
}

RETRO_API void retro_unload_game(void)
{
   g_game_loaded = false;
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
