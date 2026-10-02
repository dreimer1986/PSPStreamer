# FuSa / ARK 6.61 investigation — diagnostic build, NOT fullscreen

The original FuSa SD source was inspected at
https://github.com/andy-man/psp-fusa-sd/commit/3f0a89797c2b1ccd554cdf720d4fae5527163d8e .
This independent MIT-licensed probe contains no copied FuSa implementation.
FuSa itself is GPL-3.0; any actual derivative port must retain its licensing.

## Why this is not a blind recompile

- `main.c:getfunctions` subtracts `0xDC8` from a Hibari export and adds `0x248`
  to a display export to obtain internal function addresses. Neither offset
  is established for 6.61; export/NID resolution alone cannot fix this.
- Timer NID tables end at firmware 5.x; unresolved functions cause an endless
  lookup loop without diagnostics.
- `hook.c` overwrites the first two instructions of display/GE functions and
  builds simple trampolines without verifying instruction relocation safety.
- `firstprepar` changes EDRAM exposure to 4 MiB while returning 2 MiB to games.
  The output uses fixed addresses around `0x04300000` and model-specific layout.
  These areas must not overlap the game or other plugins' output buffers.
- Fullscreen routines assume specific strides, formats and interlaced field
  layouts. They cannot be dropped into the working progressive TV path unchanged.
- In this published snapshot, most SpeedBooster work is commented out; toggling
  it instead invokes `saveinfo`, dumping kernel memory to the Stick. This is not
  included in the probe, and the published source must not be assumed identical
  to a historical release binary.
- Old build scripts and 2 KiB worker stacks also need review.

## Hardware check needed before a scaling port

Copy `SEPLUGINS/FuSaProbe` to the Memory Stick. Add this **temporarily**, preferably
for one test game, to ARK's plugin configuration (the example ships disabled):

```text
game, ms0:/SEPLUGINS/FuSaProbe/FuSaProbe.prx, on
```

Do not enable old FuSa at the same time. Existing Consolizer/OC can remain as
configured. Start a game, wait 15 seconds, then use the normal Sony screen-button
hold to switch to TV, if not already switched by Consolizer. Leave gameplay
visible for 10 seconds, switch back to LCD and exit normally. If Consolizer
already enabled TV, one TV-to-LCD transition suffices. No need to force a crash.

Return `ms0:/SEPLUGINS/FuSaProbe/probe.log`. It contains module-export availability,
firmware/model, framebuffer address/stride/format and output mode, NOT a memory
dump. It appends at most 120 samples per launch after a 10-second startup delay.
Disable the plugin after the test; repeated launches append to the same file.

No hooks, clock changes, EDRAM-size changes, display-mode changes, thread
suspension of games or framebuffer writes are performed. The only persistent
write is the log. This does **not** establish a working fullscreen port yet.

Next decision: resolve missing exports from the 6.61 reference, establish a
non-overlapping output buffer and safe presentation interception, then attempt
a reversible, manually activated scaler for GAME / PSP-3000 / component 480p.
Legacy SpeedBooster, composite/interlaced modes and POPS are out of that test.
