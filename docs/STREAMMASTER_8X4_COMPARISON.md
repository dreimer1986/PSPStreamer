# StreamMaster 8 KiB x 4 comparison

**Archived experiment; completed 2026-10-01.** Four-deep startup/cancellation and
full download tests passed on 0.3.10-bt-qio80-iram (747.5 KiB/s). This profile is
now the ordinary app default; the historical test Makefile switch below was
removed. Use [the consolidated release](RELEASE_LAYOUT.md).

Baseline: Onju 0.3.9-bt-qio80, 8 KiB x 2, same 170143443-byte HTTP file,
PSP download clock 333 MHz. Complete-body throughput 700.5 KiB/s.

The comparison app is built with `make STREAMMASTER_TEST_8X4=1`. Clean before
switching build options. Only stm_tuning() changes: override incoming config
to 8 KiB and depth 4 without saving either value. Existing capability negotiation,
integrity checks, recovery and two internal ESP reply workspaces are unchanged.
No firmware update or PSRAM/CPU clock change. This tests the existing extended
bulk path, not a new protocol. Larger 16/32 KiB replies are not enabled.

Copy EBOOT.PBP and PSPStreamer.prx from the separate test package; include its
StreamMasterUSB.prx to avoid stale drivers. Keep the existing config/assets.
Enable debug and retain download_cpu_mhz=333. Compare the same HTTP file/AP/card
and controller use. The log must show `kib=8 depth=4 ext=1`; a capability fallback
is not an 8x4 test. Check controller response and cancellation as well as speed.
Reinstall the normal release app to return to the previous configuration.

Do not promote until measured: more outstanding work may increase control
latency. This is not an IRAM optimization and promises no throughput increase.

## First hardware run and follow-up

First group failed before any bulk payload arrived (HTTP 200, only 3575 initial
bytes, depth=4/ext=1, finish waits 21–500 ms). This is not a throughput result.
Extended receive requests were always 32768 bytes even for 8 KiB frames. Bound
each receive/cache range to the requested wire size rounded to a USB packet:
8256 bytes for extended 8 KiB (8193 on wire), 16448 for 16 KiB, 32768 for 32 KiB.
Legacy 8x2 sizing and all timeouts remain unchanged. Include the actual finish
return code in download diagnostics. Hardware validation of whether this fixes
the observed failure remains pending; do not claim a proven root cause yet.

## October 1 stability follow-up

The latest recorded session has four first-group timeouts (500–512 ms), all
after only 3575 HTTP-header-buffer body bytes, with no storage write. A later
attempt completes all 170143443 bytes in 223471 ms (743.5 KiB/s). The device's
flash app descriptor was read directly: `0.3.9-bt-qio80-iram`, Oct 1 20:46:53.
This confirms the expected comparison firmware, not a measured speed regression
or proof of a particular USB failure cause. Earlier QIO80 8x4 measured 756.2.

Version 0.3.10 reserves 8256 rather than 8192 DMA bytes before playback starts;
the first full extended reply no longer needs a replacement allocation while
the old buffer is still allocated. The PSP prepares all receive requests before
submitting sends. Timeouts, four-deep window, internal reply buffers, clocks,
checksum and file integrity verification remain unchanged.

These remove startup hazards, but hardware validation is still required.
On another failure, `download USB failure` records the awaited slot/direction,
pending mask and all four completed byte counts/statuses before cancellation.
`ESP firmware` logs the complete version and profile; the StreamMaster settings
network-status row displays the version too. Older firmware's already truncated
version string cannot be reconstructed; install the companion firmware.

Test several cold starts/reconnections and complete HTTP downloads with debug
enabled, 333 MHz and the same AP. Include cancellation/controller response.
No fallback silently replays bytes after an ambiguous USB failure.
