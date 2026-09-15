# OoT Piano test core

This fork builds a separate libretro core named `oot_piano_libretro.dll` on
Windows. It loads a normal external ROM. The repository contains no ROM or
recordings.

For the two-track test, place these three files together:

```
Ocarina of Time No Music 1 (USA).z64
piano/house.pcm
piano/kokiri.pcm
```

`*.pcm` is uncompressed **44,100 Hz stereo signed 16-bit little-endian** audio.
Convert FLAC with `ffmpeg -i source.flac -ar 44100 -ac 2 -f s16le house.pcm`
and likewise for Kokiri. The test defaults to an approximate loop start of
29 seconds for House and 56 seconds for Kokiri, and loops at the end of each
recording. Precise musical loop points still require listening in-game.

The mixer adds piano to the game's audio output when the OoT USA 1.0 music
identifier at RAM `0x8011B9DE` reads `0x1F` (House) or `0x3C` (Kokiri).
Other games are unchanged, and the ROM's sound effects are passed through.
This is an initial two-track test: the silence patch may set the identifier
to `0x01` instead of leaving the original song ID. In that case no piano will
start; this trigger must be confirmed against the actual patched ROM in
RetroArch before treating the core as complete.

Select the core with RetroArch's **Load Core → Install or Restore a Core**,
then load the `.z64` ROM. Keep the core alongside your existing N64 core.
