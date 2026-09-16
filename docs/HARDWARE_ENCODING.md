# Optional server hardware encoding

Software (`libx264`) remains the default. Hardware encoding is optional and
requires no new PSP build. Current FLV video streams and the legacy raw H.264
endpoint use this setting; MP3-only music, MJPEG and legacy MP4/MPEG-TS paths
remain unchanged. The CPU still decodes, scales and burns subtitles where
needed. Only the final video encoding runs on the GPU. Local PSP subtitle
overlays and audio encoding/timestamps are unaffected.

## Modes

* `software`: the existing FFmpeg command, unchanged.
* `auto`: try VA-API on the selected render device, then NVIDIA NVENC, then
  software if neither passes the actual encoding test.
* `vaapi`: Intel/AMD VA-API; fail visibly if unavailable, rather than silently
  claiming hardware acceleration while using the CPU.
* `nvenc`: NVIDIA NVENC; also fail visibly if unavailable.

Before using hardware, the server encodes eight test frames and checks the
result with ffprobe: H.264 Baseline, level 3.0, 4:2:0, correct dimensions,
no B-frames and matching, increasing PTS/DTS. Results are cached for five
minutes per backend/device/output size/frame rate. A first hardware request
can therefore take longer. This is a format check, not a substitute for
testing actual playback and quality on the PSP.

**Hardware-test finding:** the initial VA-API version passed host decoding but
failed on the PSP's TV decoder with `80628002` after one picture. Version
0.1.28 removes optional H.264 SEI metadata from VA-API output as a targeted
compatibility candidate. The NUC generated buffering-period/picture-timing
SEI unlike the working x264 stream. The filter preserves coded slices,
SPS/PPS and container timestamps; it does not re-encode or alter audio.
This difference is a suspect, not a proven root cause. Until confirmed on
the PSP, keep `software` selected for reliable playback; `auto` cannot detect
a PSP firmware failure remotely and therefore cannot guarantee recovery from
this error. NVIDIA is not changed by this compatibility filter.

The diagnostic comparison on the NUC also found differences in frame-number
bit width, explicit reference-picture marking, the PPS deblocking-control
flag and advertised motion-vector bounds. Those are not changed speculatively
by 0.1.28. If removing SEI does not fix the actual PSP failure, these remain
separate investigation points; a successful FFmpeg decode cannot distinguish
which syntax the Sony firmware rejects. A regression test verifies the SEI
filter preserves every non-SEI NAL payload and identical decoded frame hashes.

If a real hardware process produces no output, `auto` can restart in software
before HTTP media headers/data are sent. After output starts the server never
splices a different encoder into that stream. Ordinary network recovery stays
unchanged. An encoder failure after media output begins still ends the stream.

The browser's **Server settings** displays the selected mode and last video
encoder/probe diagnostic; **Refresh encoder status** updates it. The stream
response also contains `X-Video-Encoder`. This is video encoding acceleration,
not a claim that the entire pipeline or every filter runs on the GPU.

## Home Assistant app (0.1.27+)

The app includes Intel and Mesa VA-API drivers and requests GPU devices with
`video: true`. Update/rebuild the app, then set its options:

```yaml
acceleration: auto
vaapi_device: /dev/dri/renderD128
```

Start a video, then inspect Server settings in the web UI. For the Intel NUC,
`vaapi` should appear as the last video encoder. Select `software` to revert.
Restart the app after changing HA options. The HA web UI reports these options
but does not override the Supervisor configuration.

NVIDIA additionally needs compatible host drivers and runtime/library access.
`video: true` alone does not install these. Do not assume stock Home Assistant
OS supports the NVIDIA setup merely because `nvenc` is offered. Use normal
Docker on a properly configured NVIDIA host if those requirements are missing.

## Normal Docker

Default `compose.yaml` requires no GPU. For Intel/AMD:

```sh
docker compose -f compose.yaml -f compose.vaapi.yaml up -d --build
```

This maps `/dev/dri` and defaults to `auto`. On multi-GPU machines select the
correct render node. The device list in Server settings shows available nodes.
For NVIDIA, first install/configure the host NVIDIA driver and NVIDIA Container
Toolkit, then:

```sh
docker compose -f compose.yaml -f compose.nvidia.yaml up -d --build
```

The NVIDIA override requests one GPU and the `compute,video,utility` driver
capabilities. The FFmpeg/driver combination must support NVENC; probing reports
missing libraries or incompatible drivers. NVIDIA support is implemented but
requires testing on actual NVIDIA hardware.

Both images ship the same Python server and browser assets; regression tests
enforce parity. Their distributions and device setup necessarily differ.
The Debian Docker image includes `intel-media-va-driver-non-free` from the
container's Debian non-free repository: the free-only variant offered only
CQP, not bitrate-controlled VBR, on the tested Kaby Lake NUC. The complete
driver includes Intel media kernels with their own license terms. The Alpine
HA image uses `intel-media-driver`. No host repository settings are modified.

Environment settings are `PSP_STREAMER_ACCELERATION` and
`PSP_STREAMER_VAAPI_DEVICE`. In ordinary Docker, **Server settings** can save
them to `/data/acceleration.json`; saved settings take precedence over the
environment defaults and apply to the next stream. The existing settings
volume preserves them across updates. Changing settings does not terminate
current playback. Outside Docker, enable persistence with
`PSP_STREAMER_SETTINGS_DIR`; without it settings are environment-managed.

Hardware encoding can reduce CPU load, but its quality at PSP WLAN bitrates
can differ from x264. Compare LCD/TV, 20/23.976 fps, subtitles, seeking and A/V
sync before choosing it permanently. No custom driver installation on the
host or automatic changes to the running HA app are performed by this feature.

For a repeatable synthetic A/V integration check, run
`PYTHONPATH=. python3 tools/check_hardware_encoding.py` on the host (or feed
that script to `python3 -` inside a built container with GPU access). It checks
both output sizes and frame rates, with and without a text subtitle, MP3 audio,
ordered video timestamps and software decoding of the resulting FLV. It uses
temporary generated media only. `--device` chooses a VA-API node;
`--backend nvenc` selects NVIDIA for testing on suitable hardware.

Validation for this implementation: both container images were built and
passed all eight synthetic A/V cases on the Intel NUC server (LCD/TV ×
20/23.976 fps × text subtitles off/on). The host AMD VA-API path also passed
all eight cases. NVIDIA command construction is unit-tested, but no NVIDIA
hardware was available. Long playback and perceived image quality still need
the PSP hardware test; these checks do not claim that validation is complete.
