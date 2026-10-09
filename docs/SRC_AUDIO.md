# Consolizer SRC/Output2 optical audio

GTA Vice City Stories ULES00502 changed from normal mixer output to SRC after
its intro. The previous mirror deliberately stopped at gate 4 rather than
reading the wrong DMA buffer.

The 6.61 driver dumps from the normal-game and VSH probes match the uOFW audio
reference. The internal SRC submission call at text+0x2170 calls text+0x225c;
the original delay slot is retained. A separate relocation-normalized
fingerprint covers 0x20ec..0x2454 and verifies state references. Unknown code
is not patched. Tests mutate every covered instruction and reject changes.

The hook calls Sony first and mirrors only accepted buffers. It copies, never
retains, the game pointer. No allocation, file I/O, USB, or additional waits
occur in the hook. SRC volume is applied with signed saturation. Original
return values, waiting, interrupts and DAC timing are unchanged.

The worker lazily allocates a 32 KiB raw ring for SRC and frees it on returning
to normal output. Original PCM/POPS ring and stack sizes do not grow. Native
32/44.1/48 kHz goes to optical unchanged; lower supported SRC rates use integer
linear interpolation to 48 kHz with phase retained between packets. This is
not a bit-exact emulation of Sony's resampling filter. Concurrent SRC and
normal-channel mixing is not implemented: SRC takes precedence while reserved.
Full rings drop a submitted block with a counter; they never block the game.

GTA gameplay audio was confirmed by the user. The follow-up optimization sends
native-rate SRC samples directly from the raw ring into USB packets, bypassing
the intermediate PCM ring. Unity-volume capture and packet assembly use two
bounded bulk copies at most, including wraparound. Lower-rate resampling keeps
the existing worker path. No new buffer or priority change is introduced.
Ring-copy tests compare exact samples at both boundaries and counter rollover.
Improvement in GTA frame/audio smoothness needs hardware confirmation: the
latest mounted `last.log`/`.previous` were VSH and PSPStreamer, not GTA.

## Subsequent cold-start report

The next GTA log contains zero captured blocks through 26 seconds. FuSa's
corresponding log has a null source framebuffer and never activates zoom.
USB service/replies continue. This locates the failure before actual AV output;
it does not identify the blocked game thread or prove which plugin causes it.
The successful restart is not in the current Consolizer log pair. No speculative
clock reset, display reset, driver reinitialization or extra startup delay was
added. A report-only per-title inventory captures two thread-wait/module
snapshots if there is still no output after 20 seconds of service. See the
Consolizer README for `startup-TITLEID.log` and the hardware test.

Drained optical STATUS polling is reduced from 10 to 100 ms. New PCM WRITE
requests bypass that deadline, preserving output latency. This avoids repeated
empty USB queries but is not claimed as a cold-start or cutscene fix.

## Hardware validation pending

- GTA VCS: intro -> actual gameplay, pause/menu, exit, restart.
- Check `PCM mirror SRC/Output2 verified JAL`, opened rate, dropped/underruns.
- Normal-channel game and POPS smoke check after the change.
- No ESP firmware change is required.

## Separate PSPStreamer playback report

The latest mounted recovery logs contain successful HTTP 200 metadata fetches,
but no recorded player start/failure. Mounted app matches the prior release;
config selects `audio_output=psp`, `play_mode=stream`. No playback root cause is
established from these logs. Added opt-in entries record options accept/cancel,
player entry/return (including early module errors) and AVC runtime setup.
The follow-up logs show successful AVC initialization and playback with PSP,
auto/PCM and auto/AC-3 output, and no decoder failure. The user confirms playback
works again. This does not establish the cause of the earlier failure.

MP3 quality is hidden for optical playback, not offline MP3 conversion.
Passthrough preserves source AC-3/DTS; AC-3 fallback uses fixed 640 kbit/s;
stereo PCM has no MP3 compression. Server/HA addon 0.1.82 also respects the
output mode reported by the PSP when the web selection is "Use PSP setting".
