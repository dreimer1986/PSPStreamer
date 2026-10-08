# Onju V3 optical audio (first hardware build)

## Runtime diagnostics (firmware 0.3.24)

Serial monitoring over the Onju USB connector cannot run while that connector
is hosting the PSP. No UART wiring is required for the new diagnostic path.

- With the updated Consolizer, `audio_mirror=1` and `report=1` enable runtime
  snapshots every five seconds. They are queued to the normal report writer,
  not written from the Sony mixer hook. Look in `last.log` / `last.log.previous`.
- With the updated PSPStreamer and its debug option enabled, normal network
  diagnostic snapshots include ESP counters and the last 16 events, every
  30 seconds during network activity. Older firmware remains usable.
- The first explicit runtime request enables a small HTTP server at
  `http://<StreamMaster-WLAN-IP>:8080/diagnostics.json`. Use the Onju IP shown
  in StreamMaster settings, not the media-server or PSP internal-WLAN IP.
- After a USB/audio failure, keep Onju powered and download that URL from a
  device on the same reachable LAN. One or two manual downloads are enough;
  do not continuously poll during the performance test. Wi-Fi must still work.
- This read-only endpoint has **no authentication**. It exposes firmware,
  numeric diagnostics and event codes only, no SSIDs/passwords/media paths.
  Use a trusted test LAN and do not expose port 8080 through a reverse proxy.
  It stays available after USB loss and closes on ESP restart or protocol
  action 2. A power cycle clears the RAM trace; nothing is written to flash.

Protocol opcode 56 accepts a uint32 action: 0 snapshot, 1 enable HTTP + snapshot,
2 disable HTTP + snapshot. The reply is the versioned `SmTrace` structure.
`http_error=0` after action 1 means HTTP initialization succeeded; a nonzero
value means HTTP failed or was refused for low memory. USB snapshots still work.
HTTP starts only with at least 48 KiB free internal memory and a 12 KiB largest
block, and is stopped again if less than 24 KiB remains after initialization.
It allows one client, one route, bounded two-second socket waits and a 4 KiB
task stack. This is not a guarantee against unrelated memory exhaustion.

`audio_flags`: bit 0 paused, bit 1 disconnected, bit 2 has played samples.
`max_dma_gap_us` measures callback spacing, not receiver lock or optical quality.
`last_command_ms` records entry into the command handler; a growing age with
unchanged counters helps identify a stalled command. Diagnostic reads themselves
do not overwrite it. `reset_reason` is the ESP reset reason, not a PSP crash code.
Events: 1 USB gone, 2 receive failure, 3 transmit failure, 4 command failure,
5 audio open, 6 audio close, 7 Bluetooth state. Events retain their ESP uptime.

These measurements do not yet establish why the PSP occasionally shuts down.
No new PSP kernel thread, audio ring or USB driver is allocated for diagnostics.
The existing 144 KiB download-pool saving in games remains in place.
The compiler stack audit measured 8 bytes for the mixer hook, 696 bytes for
the PCM worker's own frame and 2,984 bytes for the controller service's own
frame, against 4 KiB/8 KiB worker stacks. These exclude nested call frames:
the new live stack-headroom report is needed to check real worst-case use.


Requires the matching PSP application, server **0.1.79** and StreamMaster
**0.3.18**. Built, but not hardware-verified. No automatic test tones.
This is a PSPStreamer output, not a system-wide PSP audio plugin.

## Wiring

For the **Everlight PLT133/T8** transmitter, use its datasheet pin numbering,
not an assumed mirrored left/right order:

| Transmitter | Onju Voice V3 |
| --- | --- |
| Pin 1, Vin | GPIO12 / SDO side of R14 |
| Pin 2, Vcc | 3.3 V, IC2 regulator output tab |
| Pin 3, GND | GND side of R14 |
| Pins 4/5 | Not connected |

Place 100 nF directly between Vcc and GND at the transmitter. On the user's
photographed board, the upper R14 pad was measured as GND and the lower pad
as the signal. Verify the net on other boards; do not assume orientation.
GPIO21 holds the onboard speaker amplifier shut down. GPIO12 is repurposed
as a complete S/PDIF signal, not raw I2S. Generic firmware variants do not
advertise optical output because their pin assignments are unspecified.

## Selection

PSP library → SELECT → **Audio output**, then save settings:

| CFG `audio_output` | Behaviour |
| --- | --- |
| `psp` | Existing PSP DAC output; unchanged default |
| `spdif_pcm` | Stereo 16-bit PCM, 48 kHz |
| `spdif_auto_pcm` | Copy AC-3 / DTS core; otherwise stereo PCM |
| `spdif_auto_ac3` | Copy AC-3 / DTS core; otherwise encode multichannel sources as AC-3, up to 5.1; stereo sources remain PCM |

The same choice is under **PSP audio output** in the web media controls.
**Use PSP setting** leaves the device's selection alone. A web override applies
to playback and subsequent files until changed; save in the PSP settings to
retain it over a restart. It does not change Xbox or browser playback.
The firmware is configured by the application for each playback session;
there is no independent firmware setting that could override this selection.

The receiver must support the chosen compressed codec. Start at a moderate
receiver volume. During passthrough the PSP volume slider has no effect:
scaling compressed words would corrupt them. PCM volume remains adjustable.
TrueHD, E-AC-3 and uncompressed multichannel are not sent as ordinary TOSLINK
bitstreams. DTS-HD uses its existing core; no lossless-HD claim is made.
Conversion fallback is explicit, never labelled as passthrough.

## Current scope

- Network movies and music files from the server, including its media providers.
- Optical PCM output and compressed carriers run at 32/44.1/48 kHz; converted
  PCM is 48 kHz. Unusual incompatible DTS core rates/burst periods fail visibly.
- Radio and existing local MP3/FLV cache files are not routed to optical output
  in this version. Select **PSP** for these. They fail explicitly rather than
  silently changing the selected output.
- PCM feeds visualizations; compressed passthrough does not provide decoded
  samples for beat detection/VU meters. Choose PCM for music visualizations.
- USB may carry Wi-Fi and audio together, or audio with the internal PSP WLAN.
- No playback signal at boot; Stop closes the output. No amplifier/I2S speaker
  output, system-wide sound capture, HDMI or Atmos transport is provided.

## Timing and transport

FFmpeg produces AVC and PCM/AC-3/DTS on one Matroska timestamp timeline.
The server repackages video into the existing AVC/FLV framing and optical
audio into private `F0 SMA1` tags. This private stream is **not** a normal FLV
download format; offline conversion remains unchanged. Onju renders IEC60958
with I2S DMA; non-PCM data carries IEC61937 bursts, non-audio channel status
and an invalid-for-PCM validity bit. The onboard amplifier remains disabled.

USB commands 50–54 open/write/status/pause/close a bounded audio session.
Capability bit 128 prevents using old or incompatible firmware. Sequence and
session checks reject stale writes. The ring holds 16384 carrier frames in
PSRAM; six internal DMA buffers hold 64 frames each (6 KiB total, 8 ms at
48 kHz). The 192-frame channel-status sequence spans three DMA buffers.
Only an explicit audio session allocates these buffers. After the first
successful setup they remain reserved across stops/track changes, avoiding
fragmentation-sensitive DMA reallocation. Stop disables I2S; the next open
uses a new session, resets counters/queued audio and preloads valid silence.
No Wi-Fi buffer tuning is changed.
Status reports media PTS and samples **completed by DMA**, not merely received
over USB. The PSP video scheduler follows that clock. Pause/underflow emits
silence/stuffing without advancing the media clock; samples are not replayed
to correct synchronization. USB detach disables further consumption.

With debug enabled, the PSP log records the selected carrier rate, PCM versus
compressed mode, accepted/completed samples, underrun blocks and last PTS.
Errors `-1801` (local/radio), `-1802` (firmware capability), `-1804` (framing),
`-1805` (output open), `-1806` (format change), `-1807`–`-1809` (USB control/data),
`-1810` (no output progress) and `-1811` (empty stream) are explicit failures.

References: [Onju schematic](https://github.com/justLV/onju-voice/blob/master/images/Schematic.pdf),
[transmitter datasheet](https://www.mouser.com/catalog/specsheets/everlight_ever-s-a0008486188-1.pdf),
[FFmpeg IEC61937 muxer](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/spdifenc.c),
[Espressif I2S driver](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/i2s.html).
