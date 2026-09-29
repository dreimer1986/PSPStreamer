# Isolated VAAPI Main/CABAC PSP test

This is a test generator, **not a server encoder setting**. Production stays on
libx264. No PSP rebuild, ESP flash, server restart or persistent setting change
is required. The previous Intel failure used Constrained Baseline/CAVLC; this
trial uses Main/CABAC, no B frames, one reference, GOP 64 and the former VAAPI
VBR/AUD/SEI policy. It retains the production MP3/FLV timeline and video filters.
CPU scaling/padding precedes NV12 upload. No experimental hardware decoding.

## Run on the Intel machine

Requires this repository checkout, Python 3, FFmpeg with `h264_vaapi`, `trace_headers`,
libx264 and libmp3lame, a working Intel VAAPI driver, and read/write access to
`/dev/dri/renderD128`. In a container, the render device and driver must be available
inside that container. The generator deliberately does not alter Docker/HA configuration.

From the repository root:

```sh
python3 tools/vaapi_psp_test.py '/path/to/episode.mkv' \
  --output /tmp/psp-vaapi-main-test --profile tv --seconds 60 --start 60
```

Use an isolated output directory, **not the server's offline queue**. The input
must be a locally readable file with audio; `--audio 1` selects the second audio
track. Subtitles are off for this first decoder test. `--profile lcd` creates
480x272 instead of 720x480. Default is paired software and VAAPI packages; use
`--encoder software` or `--encoder vaapi` only to repeat one side. The duration is
bounded to 1..300 seconds. Media paths are passed as subprocess arguments, not shell commands.

## Copy and test

The script prints two generated hexadecimal folder paths. Copy **both whole
folders** into `PSP/VIDEO/PSPStreamer/` on the Memory Stick. They contain the FLV,
empty subtitle overlay, seek index, manifest with SHA-256 digests and `ready` marker.
Select `Software-TV` and `VAAPI-TV` in Local Storage, using TV output for the TV
profile. Do not rename only the FLV or omit the companion files.

Play the control, then VAAPI: check start, picture for the full minute, sound/sync,
30-second seek and Stop. Keep the PSP recovery log, and report which file failed.
LCD is a separate comparison after TV works.

`command.txt` records the actual encoder invocation; `encoder.log`, `headers.txt`
and `probe.json` provide diagnostics. No `ready` marker is written unless the
package passes host checks: Main/level 3.0/yuv420p, expected resolution, no B
frames, increasing equal video PTS/DTS, MP3 audio, CABAC in PPS, one reference in
VAAPI SPS, and full host decoding. **Host checks do not prove PSP compatibility.**

## Current validation status

Command-isolation and software package/hash/index/host-decode tests pass locally.
No `/dev/dri` device is exposed in the development environment, and SSH access to
the NUC is blocked there. The VAAPI package has therefore **not yet been generated
or tested**. Actual Intel encoding and PSP playback remain external test steps.
