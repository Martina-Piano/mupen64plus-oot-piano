#define retro_run diag_original_retro_run
#include "oot_music_diag_wrapper.c"
#undef retro_run

/*
 * Diagnostic variant for use with a normal OoT 1.0 USA ROM.
 * It preserves the game's sequence-player state/IDs, but continuously
 * invalidates the original music sequence table so the N64 BGM stays muted.
 * This mirrors the approach used by hylian-modding/MusicReplacementMod.
 */

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

RETRO_API void retro_run(void)
{
   if (!ensure_core_loaded())
      return;

   /* Keep the game's music decision logic alive, but prevent the original
    * N64 sequences from producing audible BGM. */
   mute_original_music();
   core_retro_run();
   mute_original_music();

   g_diag_frame++;
   if ((g_diag_frame % 10U) == 0U)
      show_music_diagnostics();
}
