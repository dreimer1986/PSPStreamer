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
- `sceMeAudio_C93C56F8` at 0x3590 writes a shifted volume parameter to 0xBFC007F4;
  `sceMeAudio_68C55F4C` at 0x3514 controls/waits on ME state at 0xBFC007F0/7F8.

The local uOFW audio.c independently uses 0xBE000070 as an audio DMA destination.
The packed halves strongly identify stereo PCM. Setup at 0x3478 calls the
clock-generation import with 44100 (delay slot 0x347C). This is distinct from
the volume parameter above. Channel order and cross-CPU operation still need
hardware verification.

## Implementation consequence

This is a different CPU execution context, not a missing PSP SRC hook. Do not
call the existing kernel capture function from this ME loop: its normal CPU
assumptions, stack and services are inappropriate there. A possible next design
is a tiny ME-safe producer writing a bounded shared ring, with the existing main
CPU worker forwarding blocks. It must preserve registers, original hardware
writes, pacing, fade behavior, and safely coordinate ME stop/restart and cache
visibility.

Probe v3 captures only the validated kernel TEXT of `scePops_Manager` when
the ordinary driver is absent in POPS. It records module/segment metadata,
GP, import/export table locations and selected export addresses, without calling
them. Existing <=128 KiB segment validation remains unchanged; the expected
manager dump is 18832 bytes. No ME hardware reads/writes or user RAM dumps.
This gives an exact relocated runtime reference for the next signature check.

## First capture build — 2026-10-09

The runtime manager dump matches the reference output loop after explicitly
accounting for relocated addresses. A first opt-in path now intercepts the
main-CPU callback setup, **only before its callback slot has been populated**.
It substitutes a self-contained user-memory producer that calls the original
callback, preserves its return value and places one stereo word into a 4096-word
uncached single-producer/single-consumer ring. No ME kernel calls, USB, logging,
semaphores or heap operations. The existing PCM worker collects and forwards it.

Runtime validation checks the output loop and setup hashes, exact memory layout,
callback-slot references and the 44100 clock setup. Different or already active
layouts remain untouched. A late install reports `POPS ME already configured`
(-22); it does not attempt to halt/reset the ME or patch its live instruction
cache. Whether the current loader is early enough requires a hardware test.

The producer and ring occupy 16640 bytes in user partition 2. Once published,
they remain resident until POPS process teardown, even if our kernel plugin is
stopped: the ME might retain the callback across a pause/resume. Unload disables
capture and restores the main-CPU setup entry, never freeing published callback
code underneath the ME. The resident producer has no kernel-plugin references.
Failed, unpublished allocations are released. No ESP firmware change.

Build precedes tests. Host instruction-path checks cover disable, one sample,
full ring, counter wrap, stack/return preservation and the runtime hashes;
they do not emulate actual ME cache behavior or certify playback stability.
Hardware test: `audio_mirror=1`, `audio_probe=0`, `report=1`, `pops=1`.
Fully exit/restart the PS1 title, play, open/close HOME and exit. Check
`last.log`/`last.log.previous` for setup armed, callback samples and PCM progress.

## Early-loader correction

Hardware reported `POPS ME already configured` at 4.721929 seconds: the bridge
worker necessarily arrived too late. Capture installation is now owned by
PSPConsolizer's small loader, before its two-second USB delay. It first checks
already loaded modules synchronously and otherwise chains the existing HEN
pre-start callback. No USB imports or transport initialization occur in that
callback. Global/context/path/title exclusions are checked before arming.

The bridge acquires a versioned `ConsolizerAudio` service reference (NID
0xB05501FB) and consumes the existing shared ring, rather than reinstalling the
hook. Acquire/release prevents loader unload while its interface is referenced.
Handler removal preserves a later owner's chain; if another hook could still
call into the loader, unload is refused rather than freeing reachable code.
Both PRX files are required. The ME producer and its allocation size are unchanged.

Builds completed before targeted host tests. Producer paths and consumer
handoff/release/error handling pass ASan/UBSan; exports were inspected in the
built loader. Early-config function stack: 1072 bytes; ME-setup hook: 168 bytes.
The PSP was no longer mounted for a repeat binary comparison; previous verified
runtime hashes and signature checks remain unchanged. Actual early startup and
POPS sound require a new hardware run.

## Next hardware step

Use `audio_probe=1`, `audio_mirror=0`, `report=1`. Start one PS1 title for about
30 seconds, return to XMB, provide its `audio-probe-<title>.log` and
`audio-probe-<title>-text.bin`. In v3 the latter is the POPS manager, explicitly
identified in the log. GAME/VSH output and ESP firmware remain untouched.
