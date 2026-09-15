#define track_next_sample_v03 track_next_sample_v03_raw
#define mix_external_v03 mix_external_v03_raw
#define audio_sample_proxy_v03 audio_sample_proxy_v03_raw
#define audio_batch_proxy_v03 audio_batch_proxy_v03_raw
#include "oot_piano_dynamic_wrapper_v03.c"
#undef track_next_sample_v03
#undef mix_external_v03
#undef audio_sample_proxy_v03
#undef audio_batch_proxy_v03

/*
 * Dynamic prototype v0.31
 *
 * Same mapping and loop points as v0.3, but adds a 20 ms linear crossfade
 * at every musical loop seam. During the last 20 ms of the WAV, the tail
 * fades out while the first 20 ms starting at the user's loop point fades in.
 * After the seam, playback continues after that already-heard 20 ms region.
 * This removes clicks without creating an audible long transition.
 */

static const double LOOP_CROSSFADE_SEC = 0.020;

static void sample_at_position_v031(const struct wav_track *track, double position,
      float *left, float *right)
{
   size_t current, next;
   double fraction;
   float l0, r0, l1, r1;

   *left = 0.0f;
   *right = 0.0f;

   if (!track || !track->stereo || track->frames == 0)
      return;

   if (position < 0.0)
      position = 0.0;
   if (position >= (double)track->frames)
      position = (double)(track->frames - 1);

   current = (size_t)position;
   next = current + 1;
   if (next >= track->frames)
      next = current;

   fraction = position - (double)current;
   l0 = track->stereo[current * 2 + 0];
   r0 = track->stereo[current * 2 + 1];
   l1 = track->stereo[next * 2 + 0];
   r1 = track->stereo[next * 2 + 1];

   *left = (float)(l0 + (l1 - l0) * fraction);
   *right = (float)(r0 + (r1 - r0) * fraction);
}

static void track_next_sample_v031(struct wav_track *track, double loop_start_sec,
      float *left, float *right)
{
   double step;
   double loop_start;
   double fade_frames;
   double fade_start;
   double loop_after_fade;

   *left = 0.0f;
   *right = 0.0f;

   if (!track || !track->loaded || !track->active ||
       !track->stereo || track->frames == 0 || g_output_sample_rate <= 1.0)
      return;

   loop_start = track_loop_start_frame(track, loop_start_sec);
   fade_frames = LOOP_CROSSFADE_SEC * (double)track->sample_rate;

   if (fade_frames < 1.0)
      fade_frames = 1.0;
   if (fade_frames > ((double)track->frames - loop_start) * 0.25)
      fade_frames = ((double)track->frames - loop_start) * 0.25;
   if (fade_frames < 1.0)
      fade_frames = 1.0;

   fade_start = (double)track->frames - fade_frames;
   loop_after_fade = loop_start + fade_frames;

   while (track->position >= (double)track->frames)
      track->position = loop_after_fade +
                        (track->position - (double)track->frames);

   while (track->position >= (double)track->frames)
      track->position = loop_start +
                        (track->position - (double)track->frames);

   if (track->position >= fade_start)
   {
      float tail_l, tail_r, head_l, head_r;
      double offset = track->position - fade_start;
      double alpha = offset / fade_frames;
      double head_position = loop_start + offset;

      if (alpha < 0.0) alpha = 0.0;
      if (alpha > 1.0) alpha = 1.0;

      sample_at_position_v031(track, track->position, &tail_l, &tail_r);
      sample_at_position_v031(track, head_position, &head_l, &head_r);

      *left = (float)(tail_l * (1.0 - alpha) + head_l * alpha);
      *right = (float)(tail_r * (1.0 - alpha) + head_r * alpha);
   }
   else
   {
      sample_at_position_v031(track, track->position, left, right);
   }

   step = (double)track->sample_rate / g_output_sample_rate;
   track->position += step;
}

static void mix_external_v031(float *left, float *right)
{
   float kl, kr, hl, hr, bl, br;

   track_next_sample_v031(&g_kokiri, KOKIRI_LOOP_START_SEC, &kl, &kr);
   track_next_sample_v031(&g_house,  HOUSE_LOOP_START_SEC,  &hl, &hr);
   track_next_sample_v031(&g_battle, BATTLE_LOOP_START_SEC, &bl, &br);

   *left = (kl * g_kokiri.volume + hl * g_house.volume + bl * g_battle.volume) * g_master_gain;
   *right = (kr * g_kokiri.volume + hr * g_house.volume + br * g_battle.volume) * g_master_gain;
}

static void RETRO_CALLCONV audio_sample_proxy_v031(int16_t left, int16_t right)
{
   float ext_l, ext_r;
   int32_t out_l, out_r;

   if (!g_frontend_audio_sample)
      return;

   mix_external_v031(&ext_l, &ext_r);
   out_l = (int32_t)left + (int32_t)(ext_l * 32767.0f);
   out_r = (int32_t)right + (int32_t)(ext_r * 32767.0f);
   g_frontend_audio_sample(clamp_i16(out_l), clamp_i16(out_r));
}

static size_t RETRO_CALLCONV audio_batch_proxy_v031(const int16_t *data, size_t frames)
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

      mix_external_v031(&ext_l, &ext_r);
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
   if (ensure_core_loaded()) core_retro_set_audio_sample(audio_sample_proxy_v031);
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   g_frontend_audio_batch = cb;
   if (ensure_core_loaded()) core_retro_set_audio_sample_batch(audio_batch_proxy_v031);
}

RETRO_API void retro_get_system_info(struct retro_system_info *info)
{
   if (!info) return;

   if (ensure_core_loaded())
   {
      core_retro_get_system_info(info);
      info->library_name = "Mupen64Plus-Next OoT Piano Dynamic";
      info->library_version = "Kokiri+House+Battle 0.31 SmoothLoop";
      return;
   }

   memset(info, 0, sizeof(*info));
   info->library_name = "Mupen64Plus-Next OoT Piano Dynamic";
   info->library_version = "0.31 SmoothLoop (missing base core)";
   info->valid_extensions = "n64|v64|z64|ndd|bin|u1";
   info->need_fullpath = true;
}
