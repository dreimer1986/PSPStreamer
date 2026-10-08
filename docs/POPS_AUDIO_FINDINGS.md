# POPS audio investigation — 2026-10-08

## Runtime evidence

Probe v2 on 6.61, SLUS01013: four inventories over 120 seconds contained
52 modules and no `sceAudio_Driver` or `SceAudioMixer` thread. `pops` was at
0x08B93000, text size 1048036; `scePops_Manager` at 0x88181B00, size 18832.
The emulator and serial-pad/rumble path remained active. This is not merely
a delayed normal-mixer load. The normal GAME/VSH capture cannot attach here.

## Static evidence from user-provided 6.60 modules

Source: `Downloads/Popsloader_v4i/seplugins/popsloader/modules/660`.
These are reference binaries, not proof of a matching 6.61 runtime. No Sony
binary is distributed with this project. Offsets below are module-relative,
before relocation. ELF LOAD file offsets are 0xA0 (manager) and 0xC0 (03g).

- `popsman.prx` exports library `sceAudio`, but nine of its ten functions point
  to 0x36F4, a bare return. The remaining function at 0x36D8 delays 10 ms.
  Hooking these as a normal PCM submission API would not find the real audio.
- `sceMeAudio_DE630CD2` at 0x3490 records argument a0 as a callback and uses a1
  to configure the Media Engine bootstrap stack. The bootstrap at 0x2F28 is
  copied to 0xBFC00040 by 0x35D8; the code then starts the ME.
- The ME-side routine at 0x2F88 loads the recorded callback into s0. It sets
  up the audio hardware at 0xBE000000 and runs a hardware-paced output loop.
- At 0x3124 it calls s0 (`jalr s0`). The delay slot at 0x3128 writes the
  **previous** packed sample in v0 to 0xBE000070. After the callback returns,
  0x3134/0x3138 extract signed high/low 16-bit halves from the new result.
  There is also a fade/control path writing a repacked sample at 0x3194.
- `pops_03g.prx` calls the DE630CD2 import at 0x1A2B0. Its a0 load is subject
  to PRX relocation: the raw zero immediate is NOT a null callback at runtime.
- `sceMeAudio_C93C56F8` at 0x3590 writes a shifted parameter to 0xBFC007F4;
  `sceMeAudio_68C55F4C` at 0x3514 controls/waits on ME state at 0xBFC007F0/7F8.

The local uOFW audio.c independently uses 0xBE000070 as an audio DMA destination.
The packed halves strongly identify stereo PCM; channel order, exact sample
rate and cache/coherency requirements still need validation before playback.

## Implementation consequence

This is a different CPU execution context, not a missing PSP SRC hook. Do not
call the existing kernel capture function from this ME loop: its normal CPU
assumptions, stack and services are inappropriate there. A possible next design
is a tiny ME-safe producer writing a bounded shared ring, with the existing main
CPU worker forwarding blocks. It must preserve registers, original hardware
writes, pacing, fade behavior, and safely coordinate ME stop/restart and cache
visibility. No such patch is enabled yet.

Probe v3 captures only the validated kernel TEXT of `scePops_Manager` when
the ordinary driver is absent in POPS. It records module/segment metadata,
GP, import/export table locations and selected export addresses, without calling
them. Existing <=128 KiB segment validation remains unchanged; the expected
manager dump is 18832 bytes. No ME hardware reads/writes or user RAM dumps.
This gives an exact relocated runtime reference for the next signature check.

## Next hardware step

Use `audio_probe=1`, `audio_mirror=0`, `report=1`. Start one PS1 title for about
30 seconds, return to XMB, provide its `audio-probe-<title>.log` and
`audio-probe-<title>-text.bin`. In v3 the latter is the POPS manager, explicitly
identified in the log. GAME/VSH output and ESP firmware remain untouched.
