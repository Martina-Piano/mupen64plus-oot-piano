#define retro_run retro_run_v01
#define retro_load_game retro_load_game_v01
#define retro_load_game_special retro_load_game_special_v01
#define retro_get_system_info retro_get_system_info_v01
#include "oot_piano_dynamic_wrapper.c"
#undef retro_run
#undef retro_load_game
#undef retro_load_game_special
#undef retro_get_system_info

/*
 * Dynamic prototype v0.2
 *
 * Fixes two issues from v0.1:
 * 1) Do not gate external WAV playback on OoT's sequence-player playing flag.
 *    Our RDRAM mute method can make that flag unreliable even though the
 *    sequence ID and volume remain correct. ID selects the track; OoT volume
 *    controls audibility.
 * 2) Search configured System/BIOS first, then RetroArch\BIOS and
 *    RetroArch\system relative to the cores directory.
 */

static int load_named_track_v02(struct wav_track *track, const char *filename)
{
   const char *system_directory = NULL;
   char path[4096];
   char core_dir[4096];
   char root[4096];
   char *slash;
   size_t len;

   if (g_frontend_environment &&
       g_frontend_environment(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &system_directory) &&
       system_directory)
   {
      if (snprintf(path, sizeof(path), "%s\\OoT-Piano\\%s", system_directory, filename) > 0 &&
          load_wav_file(track, path))
         return 1;
   }

   if (!get_core_directory(core_dir, sizeof(core_dir)))
      return 0;

   strncpy(root, core_dir, sizeof(root) - 1);
   root[sizeof(root) - 1] = '\0';
   len = strlen(root);
   if (len && (root[len - 1] == '\\' || root[len - 1] == '/'))
      root[len - 1] = '\0';

   slash = strrchr(root, '\\');
   if (!slash)
      slash = strrchr(root, '/');
   if (slash)
      *slash = '\0';

   if (snprintf(path, sizeof(path), "%s\\BIOS\\OoT-Piano\\%s", root, filename) > 0 &&
       load_wav_file(track, path))
      return 1;

   if (snprintf(path, sizeof(path), "%s\\system\\OoT-Piano\\%s", root, filename) > 0 &&
       load_wav_file(track, path))
      return 1;

   return 0;
}

static void load_music_files_v02(void)
{
   int kokiri_ok = load_named_track_v02(&g_kokiri, KOKIRI_FILE);
   int battle_ok = load_named_track_v02(&g_battle, BATTLE_FILE);

   if (kokiri_ok && battle_ok)
      frontend_message("OoT Piano Dynamic v0.2: Kokiri + Battle WAV loaded", 300);
   else if (!kokiri_ok && !battle_ok)
      frontend_message("OoT Piano Dynamic v0.2: BOTH WAV FILES NOT LOADED", 300);
   else if (!kokiri_ok)
      frontend_message("OoT Piano Dynamic v0.2: KOKIRI WAV NOT LOADED", 300);
   else
      frontend_message("OoT Piano Dynamic v0.2: BATTLE WAV NOT LOADED", 300);
}

static void update_music_state_v02(bool show_diag)
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

   /* ID determines which external timeline is alive. Volume alone determines
    * whether it is audible. This lets Kokiri keep advancing at volume 0 while
    * Battle is faded in, then resume at the correct later position. */
   set_track_state(&g_kokiri, p1.id == 0x3CU, p1.volume);
   set_track_state(&g_battle, p3.id == 0x1AU, p3.volume);

   if (show_diag)
   {
      char msg[260];
      snprintf(msg, sizeof(msg),
         "Dyn v0.2 P1:%02X v%.2f Kok:%s/%s | P3:%02X v%.2f Bat:%s/%s",
         (unsigned)p1.id, (double)p1.volume,
         g_kokiri.loaded ? "LOAD" : "MISS",
         g_kokiri.active ? "ON" : "off",
         (unsigned)p3.id, (double)p3.volume,
         g_battle.loaded ? "LOAD" : "MISS",
         g_battle.active ? "ON" : "off");
      frontend_message(msg, 20);
   }
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
   if (!info) return;

   if (ensure_core_loaded())
   {
      core_retro_get_system_info(info);
      info->library_name = "Mupen64Plus-Next OoT Piano Dynamic";
      info->library_version = "Kokiri+Battle 0.2";
      return;
   }

   memset(info, 0, sizeof(*info));
   info->library_name = "Mupen64Plus-Next OoT Piano Dynamic";
   info->library_version = "Kokiri+Battle 0.2 (missing base core)";
   info->valid_extensions = "n64|v64|z64|ndd|bin|u1";
   info->need_fullpath = true;
}

RETRO_API void retro_run(void)
{
   if (!ensure_core_loaded())
      return;

   update_music_state_v02(false);
   mute_original_music();
   core_retro_run();
   mute_original_music();
   update_music_state_v02(false);

   g_diag_frame++;
   if ((g_diag_frame % 10U) == 0U)
      update_music_state_v02(true);
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
   struct retro_system_av_info av;
   bool ok;

   if (!ensure_core_loaded())
   {
      frontend_message("OoT Piano Dynamic v0.2: official Mupen64Plus-Next not found", 300);
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

   load_music_files_v02();
   update_music_state_v02(false);
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

   load_music_files_v02();
   update_music_state_v02(false);
   return true;
}
