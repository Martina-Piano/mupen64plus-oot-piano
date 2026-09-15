OoT Piano WAV Wrapper - Prototype 0.1

Purpose
-------
This wrapper uses the official RetroArch Mupen64Plus-Next core that is already installed.
It does not replace or rebuild that core.

Files expected in RetroArch\cores\
----------------------------------
mupen64plus_next_libretro.dll          (official working RetroArch core - leave unchanged)
mupen64plus_oot_piano_libretro.dll     (this wrapper)

Optional core info file in RetroArch\info\
-------------------------------------------
mupen64plus_oot_piano_libretro.info

Test WAV location
-----------------
Create this folder:
RetroArch\system\OoT-Piano\

Put one WAV there with this exact name:
test.wav

Supported by this prototype:
- PCM WAV: 8/16/24/32 bit
- IEEE float WAV: 32 bit
- mono or stereo (stereo recommended)
- common sample rates such as 44.1 / 48 / 96 kHz

Test
----
Load the core named:
Nintendo - Nintendo 64 (Mupen64Plus-Next OoT Piano WAV)

Then load the music-muted OoT ROM.
The test WAV loops continuously and is mixed with normal game audio/SFX.
This first prototype intentionally does not switch tracks yet.

Expected on-screen message:
OoT Piano: test.wav loaded - WAV + game audio active

If the WAV cannot be opened or is unsupported:
OoT Piano: test.wav not found/unsupported

The next development step after this works is detecting OoT's current music ID and selecting the matching WAV automatically.
