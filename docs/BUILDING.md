# Building and testing

For release installation, use [Setup](SETUP.md).

## Build the PSP client

A PSPDEV/PSPSDK toolchain is required. The active video path uses firmware AVC, not OpenH264:

```bash
cd psp-client
make
# Install EBOOT.PBP AND PSPStreamer.prx together into your app directory.
```

Keep the firmware bridge and TV-out PRX files from the working installation alongside the new EBOOT. The Makefile preserves the MPEG import-library order and embeds the receiver artwork.

The default optimization is `-O3 -G0` with LTO (`LTO=1`), following the successful PSP comparison.
A conservative comparison build uses `make clean && make OPT_LEVEL=-O2`;
clean first because make does not track compiler-flag changes. No fast-math
options are enabled. Return to O3 with `make clean && make`.
Copy the matching EBOOT/PRX pair, keep your config,
presets and firmware modules, and compare the same presets on LCD and TV.
The reported improvement is subjective, not a measured speedup for every preset.
See [the O2/O3 comparison and test checklist](../docs/OPTIMIZATION_COMPARISON.md).

## Tests

For a focused change, run the directly affected tests first. A documentation or
asset change does not require the entire playback/integration suite below.

See [music rendering ownership and performance](../docs/MUSIC_RENDERING.md)
for the shared LCD/TV helpers, hardware-validated baseline, rendering
invariants and boundaries for a future visualization engine.

LCD and TV music views update only dynamic regions, at most 20 times per
second, and give the existing audio-output worker priority over GUI work.
No extra LCD framebuffer is needed. The initial scene is drawn before audio
starts; switching fullscreen redraws once. Video timestamps and audio sample
timing are unchanged. Native renderer tests compare incremental updates with
full redraws, including volume, spectrum, VU decay and both layouts.

Host integration tests require `cc`, FFmpeg and FFprobe. They compile the same FLV parsing/sync helpers used by the PSP with undefined-behavior checks, compare every parsed PTS with FFprobe (including a five-minute stream), decode the extracted H.264/MP3, and exercise seek, video-only HTTP output and millisecond subtitle cues.

Client regression tests also exercise the first-picture/DAC startup barrier, late decisions after a 600-ms preparation stall, and the actual audio output worker with a simulated asynchronous DAC and immediate producer reuse of released buffers. They cover single-block EOF, ring wraparound, cancellation and output failure; they do not emulate real firmware decoding.

```bash
python3 -m unittest discover -s tests -v
```

Optional browser regression tests require Node.js, Playwright and its Chromium
browser. Run `node tests/web_ui_browser.cjs` and
`node tests/web_login_browser.cjs` (or set `PLAYWRIGHT_MODULE` to an installed
Playwright module). The first uses deterministic media fixtures; the second
starts an isolated local Python server and exercises real login/logout,
password changes, navigation and remote commands. Neither contacts your
personal server or Plex installation.

Before treating a build as hardware-validated, test one complete episode on both LCD and TV, then pause/resume, seek, stop/start another file, subtitles and a WLAN disconnect/reconnect. A successful host test or build alone is not a claim of perfect real-device synchronization.


## Other components

- [StreamMaster firmware](../streammaster/README.md)
- [PSP Consolizer](../psp-controller/README.md)
- [StreamerOC](../psp-overclock/README.md)
- [FuSa Fullscreen](../psp-fusa-probe/FULLSCREEN.md)
- [Xbox client](../xbox-client/README.md)
- [Release packaging](RELEASE_LAYOUT.md)
