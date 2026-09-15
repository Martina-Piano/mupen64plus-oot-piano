#define retro_set_audio_sample retro_set_audio_sample_v01
#define retro_set_audio_sample_batch retro_set_audio_sample_batch_v01
#define retro_deinit retro_deinit_v01
#define retro_get_system_info retro_get_system_info_v01
#define retro_reset retro_reset_v01
#define retro_run retro_run_v01
#define retro_load_game retro_load_game_v01
#define retro_load_game_special retro_load_game_special_v01
#define retro_unload_game retro_unload_game_v01
#define audio_sample_proxy audio_sample_proxy_v01
#define audio_batch_proxy audio_batch_proxy_v01
#define track_next_sample track_next_sample_v01
#define mix_external mix_external_v01
#define load_music_files load_music_files_v01
#define update_music_state update_music_state_v01
#include "oot_piano_dynamic_wrapper.c"
#undef retro_set_audio_sample
#undef retro_set_audio_sample_batch
#undef retro_deinit
#undef retro_get_system_info
#undef retro_reset
#undef retro_run
#undef retro_load_game
#undef retro_load_game_special
#undef retro_unload_game
#undef audio_sample_proxy
#undef audio_batch_proxy
#undef track_next_sample
#undef mix_external
#undef load_music_files
#undef update_music_state

/*
 * Dynamic prototype v0.3
 *
 * Adds the user's final Kokiri Forest, House and Battle WAV files and
 * musical loop points.  The supplied FL Studio positions are interpreted
 * on the 130 BPM project grid (4/4, 16 steps/bar, 96 PPQ):
 *
 *   Kokiri Forest  31:08:05 -> 56.2163461538 s
 *   House          16:09:08 -> 28.6538461538 s
 *   Battle         34:14:04 -> 62.4423076923 s
 *
 * Each file plays its intro once.  On reaching the physical end of the WAV,
 * playback jumps back to the corresponding loop-start position and continues
 * indefinitely.  OoT's own sequence-player volumes still control the
 * crossfade between area music and Battle.
 */

static struct wav_track g_house = {0};

static const char *KOKIRI_FILE_V03 = "07 Kokiri Forest (Young Link).wav";
static const char *HOUSE_FILE_V03 = "06 House.wav";
static const char *BATTLE_FILE_V03 = "09 Battle.wav";

static const double KOKIRI_LOOP_START_SEC = 56.21634615384615;
static const double HOUSE_LOOP_START_SEC  = 28.653846153846153;
static const double BATTLE_LOOP_START_SEC = 62.442307692307686;

static int load_named_track_v03(struct wav_track *track, const char *filename)
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

static void load_music_files_v03(void)
{
   int kokiri_ok = load_named_track_v03(&g_kokiri, KOKIRI_FILE_V03);
   int house_ok = load_named_track_v03(&g_house, HOUSE_FILE_V03);
   int battle_ok = load_named_track_v03(&g_battle, BATTLE_FILE_V03);

   if (kokiri_ok && house_ok && battle_ok)
      frontend_message("OoT Piano Dynamic v0.3: Kokiri + House + Battle WAV loaded", 300);
   else
   {
      char msg[240];
      snprintf(msg, sizeof(msg),
         "OoT Piano v0.3 load  Kok:%s House:%s Battle:%s",
         kokiri_ok ? "OK" : "MISS",
         house_ok ? "OK" : "MISS",
         battle_ok ? "OK" : "MISS");
      frontend_message(msg, 300);
   }
}

static void update_music_state_v03(bool show_diag)
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

   /* Area IDs select the timeline.  OoT's volume drives audibility. */
   set_track_state(&g_kokiri, p1.id == 0x3CU, p1.volume);
   set_track_state(&g_house,  p1.id == 0x1FU, p1.volume);
   set_track_state(&g_battle, p3.id == 0x1AU, p3.volume);

   if (show_diag)
   {
      char msg[300];
      snprintf(msg, sizeof(msg),
         "v0.3 P1:%02X v%.2f K:%s/%s H:%s/%s | P3:%02X v%.2f B:%s/%s",
         (unsigned)p1.id, (double)p1.volume,
         g_kokiri.loaded ? "L" : "M", g_kokiri.active ? "ON" : "off",
         g_house.loaded ? "L" : "M", g_house.active ? "ON" : "off",
         (unsigned)p3.id, (double)p3.volume,
         g_battle.loaded ? "L" : "M", g_battle.active ? "ON" : "off");
      frontend_message(msg, 20);
   }
}

static double track_loop_start_frame(const struct wav_track *track, double loop_start_sec)
{
   double start;
   if (!track || track->sample_rate == 0)
      return 0.0;

   start = loop_start_sec * (double)track->sample_rate;
   if (start < 0.0)
      start = 0.0;
   if (track->frames > 1 && start >= (double)(track->frames - 1))
      start = (double)(track->frames - 1);
   return start;
}

static void track_next_sample_v03(struct wav_track *track, double loop_start_sec,
      float *left, float *right)
{
   size_t current, next;
   double fraction, step, loop_start, loop_length;
   float l0, r0, l1, r1;

   *left = 0.0f;
   *right = 0.0f;

   if (!track || !track->loaded || !track->active ||
       !track->stereo || track->frames == 0 || g_output_sample_rate <= 1.0)
      return;

   loop_start = track_loop_start_frame(track, loop_start_sec);
   loop_length = (double)track->frames - loop_start;

   if (loop_length < 2.0)
      loop_start = 0.0, loop_length = (double)track->frames;

   while (track->position >= (double)track->frames)
      track->position = loop_start + (track->position - (double)track->frames);

   while (track->position >= (double)track->frames)
      track->position -= loop_length;

   current = (size_t)track->position;
   next = current + 1;
   if (next >= track->frames)
      next = (size_t)loop_start;

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

static void mix_external_v03(float *left, float *right)
{
   float kl, kr, hl, hr, bl, br;

   track_next_sample_v03(&g_kokiri, KOKIRI_LOOP_START_SEC, &kl, &kr);
   track_next_sample_v03(&g_house,  HOUSE_LOOP_START_SEC,  &hl, &hr);
   track_next_sample_v03(&g_battle, BATTLE_LOOP_START_SEC, &bl, &br);

   *left = (kl * g_kokiri.volume + hl * g_house.volume + bl * g_battle.volume) * g_master_gain;
   *right = (kr * g_kokiri.volume + hr * g_house.volume + br * g_battle.volume) * g_master_gain;
}

static void RETRO_CALLCONV audio_sample_proxy_v03(int16_t left, int16_t right)
{
   float ext_l, ext_r;
   int32_t out_l, out_r;

   if (!g_frontend_audio_sample)
      return;

   mix_external_v03(&ext_l, &ext_r);
   out_l = (int32_t)left + (int32_t)(ext_l * 32767.0f);
   out_r = (int32_t)right + (int32_t)(ext_r * 32767.0f);
   g_frontend_audio_sample(clamp_i16(out_l), clamp_i16(out_r));
}

static size_t RETRO_CALLCONV audio_batch_proxy_v03(const int16_t *data, size_t frames)
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

      mix_external_v03(&ext_l, &ext_r);
      out_l = (int32_t)data[i * 2 + 0] + (int32_t)(ext_l * 32767.0f);
      out_r = (int32_t)data[i * 2 + 1] + (int32_t)(ext_r * 32767.0f);
      g_mix_buffer[i * 2 + 0] = clamp_i16(out_l);
      g_mix_buffer[i * 2 + 1] = clamp_i16(out_r);
   }

   return g_frontend_audio_batch(g_mix_buffer, frames);
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb)
{
   g_frontend_audio_sample = cb;
   if (ensure_core_loaded()) core_retro_set_audio_sample(audio_sample_proxy_v03);
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   g_frontend_audio_batch = cb;
   if (ensure_core_loaded()) core_retro_set_audio_sample_batch(audio_batch_proxy_v03);
}

RETRO_API void retro_deinit(void)
{
   g_game_loaded = false;
   if (ensure_core_loaded()) core_retro_deinit();
   unload_track(&g_kokiri);
   unload_track(&g_house);
   unload_track(&g_battle);
   free(g_mix_buffer);
   g_mix_buffer = NULL;
   g_mix_buffer_frames = 0;
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
   if (!info) return;

   if (ensure_core_loaded())
   {
      core_retro_get_system_info(info);
      info->library_name = "Mupen64Plus-Next OoT Piano Dynamic";
      info->library_version = "Kokiri+House+Battle 0.3";
      return;
   }

   memset(info, 0, sizeof(*info));
   info->library_name = "Mupen64Plus-Next OoT Piano Dynamic";
   info->library_version = "0.3 (missing base core)";
   info->valid_extensions = "n64|v64|z64|ndd|bin|u1";
   info->need_fullpath = true;
}

RETRO_API void retro_reset(void)
{
   g_kokiri.position = 0.0;
   g_house.position = 0.0;
   g_battle.position = 0.0;
   g_kokiri.was_active = false;
   g_house.was_active = false;
   g_battle.was_active = false;
   if (ensure_core_loaded()) core_retro_reset();
}

RETRO_API void retro_run(void)
{
   if (!ensure_core_loaded())
      return;

   update_music_state_v03(false);
   mute_original_music();
   core_retro_run();
   mute_original_music();
   update_music_state_v03(false);

   g_diag_frame++;
   if ((g_diag_frame % 10U) == 0U)
      update_music_state_v03(true);
}

RETRO_API bool retro_load_game(const struct retro_game_info *game)
{
   struct retro_system_av_info av;
   bool ok;

   if (!ensure_core_loaded())
   {
      frontend_message("OoT Piano Dynamic v0.3: official Mupen64Plus-Next not found", 300);
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

   load_music_files_v03();
   update_music_state_v03(false);
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

   load_music_files_v03();
   update_music_state_v03(false);
   return true;
}

RETRO_API void retro_unload_game(void)
{
   g_game_loaded = false;
   unload_track(&g_kokiri);
   unload_track(&g_house);
   unload_track(&g_battle);
   if (ensure_core_loaded()) core_retro_unload_game();
}
