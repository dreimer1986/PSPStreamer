# Playlist and PSP comparison build — 2026-09-29

Server / HA app: **0.1.64**. The canonical server and Home Assistant payload
are mirrored. Firmware and OC plugin are unchanged.

## Features

- Next-entry preview uses the same queue successor resolver as the PSP; merely
  looking at the preview never consumes Play Next.
- Total duration counts one full pass and explicitly marks unknown entries.
  Duration lookup runs sequentially only while the playlist page is open;
  playback/queue polling performs no new media probes.
- Drag handles reorder the stored list, with revision protection and button
  fallback. Active dragging/select editing is not replaced by the refresh timer.
- Audio quality and video FPS are persisted per entry. Explicit inheritance is
  scoped to the pre-queue PSP defaults; it never inherits a preceding override.
  Web starts, native starts and automatic continuation use the same fields.
  Music hides FPS. No LCD/TV resolution or clock changes.

## Optimizations and limits

- PSP TLS reads ahead at most 2 KiB for tiny record-header requests. Large reads
  bypass the extra copy. EOF, partial data and retry/error return codes retain
  their semantics. Only PSP-native HTTPS uses this path, not ESP-side TLS.
- No TLS session resumption: every new connection still receives and records the
  peer certificate. Existing opportunistic auto-accept/change notice is unchanged;
  it is not CA validation and does not authenticate a first-use certificate.
- SHA-256 input uses alias-safe, unaligned-safe word loads and byte swapping.
  PSP disassembly confirms `lwl`/`lwr` + `wsbw`; the rolling schedule and full
  card read-back verification remain. Existing async disk I/O is retained rather
  than growing buffers without a measured bottleneck.
- O3 remains the normal build. LTO is a separate optional comparison and does not
  rebuild kernel plugins, SDK libraries or ESP firmware. It is not a speed claim.
  An explicit shape-side range guard keeps the existing 3–100/invalid behavior
  clear to the LTO optimizer without zero-initializing every vertex array.

| Build | EBOOT bytes | ELF text + read-only data | Initialized data | BSS |
|---|---:|---:|---:|---:|
| O3 | 4,815,324 | 4,083,476 | 127,600 | 6,387,780 |
| O3 + LTO | 4,859,468 | 4,114,596 | 127,600 | 5,817,344 |

LTO is about 43 KiB larger on disk but has about 557 KiB less BSS. Neither value
predicts playback/visualization speed or proves hardware stability.

## Focused verification

Builds were created before running affected tests. Checks cover queue persistence,
revision conflicts, quality validation, actual HTTP handler dispatch without
network sockets, successor overrides, duration fallbacks, PSP scoped inheritance,
TLS buffer ordering/partial reads/EOF/errors, shape closure, and web drag/focus/
audio-only/duration behavior. No unrelated full test suite was run.

SHA validation compares 98 boundary cases, four unaligned mixed-chunk streams and
counter carry against Mbed TLS. Read-ahead tests cover cancellation, fallback,
read failures and digest mismatch. ASan/UBSan remain enabled; LeakSanitizer had to
be disabled because the sandbox's tracing prevents it from running.

## Hardware comparison

1. Update server/HA app and both normal PSP files. In the web playlist, reorder
   a mixed list and choose different quality/FPS settings plus PSP-default on
   successive entries. Check web start, native start and automatic continuation.
2. Check next preview with Play Next, shuffle and Repeat One. Unknown durations
   must not prevent start; retry them explicitly if their source was unavailable.
3. For LTO, copy its **EBOOT.PBP and PSPStreamer.prx together** into the existing
   PSPStreamer app folder; keep CFG, plugins and assets. Compare the same preset,
   media, LCD/TV mode and clock. Check start, track change, Stop and exit too.
   Restore the normal pair if there is a regression.
4. Optional HTTPS comparison needs native WLAN; StreamMaster does its own TLS.
   Offline verification can be compared over either transport with the same file
   and card: inspect `download hash timing`, not just the displayed download rate.

No new PSP hardware results or throughput gains are claimed yet.
