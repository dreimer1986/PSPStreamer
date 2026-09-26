# Playback recovery update — 2026-09-26

Video startup/recovery keeps the menu display and a cancellable waiting indicator
until the first decoded frame is ready. The TV video-mode switch now happens
immediately before presenting that frame, not while waiting for stream data.
Removing the cable before starting falls back to the LCD menu. Debug recovery
logs record the startup reader phase every ten seconds and first-frame delivery.
This makes an empty stream queue visible; it does not fix the underlying network
outage. Decoder settings, PTS sync, frame rate and network budgets are unchanged.
Hardware check: start/resume on LCD and TV, cancel a stalled startup with START,
and confirm picture/audio return together after reconnecting.

Wi-Fi disconnect recovery: a requested rebuild stays pending until a real
disconnect/connect cycle reaches GOT_IP. A stale GOT_IP after a failed
disconnect is no longer accepted as success on the next normal retry.
Disconnect waits up to ten seconds with controller cancellation. After two
disconnect timeouts, playback recovery may terminate/reinitialize APCTL alone,
with network request workers already joined. Network Common, INET and AVC/audio
are not reinitialized. A successful APCTL rebuild is not repeated in the same
pending cycle. WLAN state transitions and APCTL results are logged.
Compare the previously stable anime with identical quality/subtitle settings;
the original reason for the repeated data interruptions remains unproven.

Subtitle preparation recovery: playback reuses the known server IP instead of
entering firmware DNS again after a link transition (observed to hang despite
its nominal timeout). Server setting changes/app restart invalidate the cache.
If the server itself changes IP during a session, restart the app to resolve it.
TCP/TLS/request transmission has a separate 15-second budget. The existing
210/630-second response budgets start after sending the request, preserving
cold subtitle generation. Transport failures now return to stream recovery
instead of silently leaving playback. Explicit HTTP errors (including wrong
credentials) and malformed/oversized responses are not Wi-Fi-retry candidates.
START or Circle cancels subtitle preparation. `PSPStreamer-recovery.txt` records
preparation begin/progress/result and reconnection steps even before the video
watchdog starts. It participates in the same rotation/24-hour retention policy;
no media URLs or passwords are recorded. Test with selected subtitles and an
uplink interruption during preparation/resume, plus one cold subtitle start.

Once successful HTTP headers have arrived, a 30-second data-inactivity budget
replaces the long cold-generation wait for missing response bytes. The long
preparation allowance before the response is unchanged. The recovery log names
this failure `response inactivity timeout`.

AVC recovery: streaming errors 80628001/80628002 specifically from `AVC: Decode`
now restart the stream after the old workers, queues and hardware decoder have
been shut down normally. The new transcode starts at the last presented second
with a fresh decoder and reference frames. The wait is five seconds and remains
cancellable; this path does not count as a Wi-Fi failure or force re-association.
At most three restarts are attempted without 30 seconds of playback progress.
Repeated failures then return the error to the browser instead of looping.
Local files and other decoder/format errors retain their existing handling.
Decoder-failure logs now include packet PTS/size, basic packet validity, queue
depths and free memory before teardown. Root cause of the observed AVC failures
is not yet established. Hardware test: repeat the interruption/recovery scenario.

Recovery escalation: after two unsuccessful stream reconnection attempts,
disconnect and rejoin the configured Wi-Fi profile. Full re-association is
limited to once per minute. Playback, media remote and virtual-button workers
are stopped before re-association. The known server address is reused rather
than re-entering firmware DNS. Thirty seconds of playback resets the failure
count. This also applies to radio, which resumes at its live edge.

Diagnostic history: existing watch, stream-error and sync CSV files are renamed
to `original-name.history-<unique timestamp>` at app startup and before another
playback would overwrite them. At startup, only these explicitly named logs
and their archives older than 24 hours are deleted. Configuration, state,
media and other plugins' logs are untouched. Invalid/unset PSP clocks disable
age-based deletion. Debug-off still generates no playback diagnostics.
Completed writes survive an app restart; a hard power cut can still lose an
in-flight filesystem write or sync measurements not yet saved from RAM.

Music and video now retry transport failures after releasing the old playback
workers and buffers. The waiting screen retries after five seconds; X/Square
retries immediately, START/Circle cancels. Server Stop and Play remain usable
when the server is reachable. Radio retains its existing live-edge recovery.
Finite media resumes at the last played position, rounded down to a second.
No server update is required for this recovery change.

Music tolerates 30 seconds without incoming data before reconnecting. Playback
pause/backpressure does not consume that budget. Initial preparation retains
its longer 180-second allowance. HTTP playback sockets now stay nonblocking,
TLS/connect/send operations are cancellable, and playback DNS uses a bounded
resolver. Wrong HTTP responses, local file errors and decoder errors are not
blindly retried. A clean stop must not restart playback.

Hardware check: play several songs, switch the hotspot's uplink between mobile
data and Wi-Fi, and repeat during a subtitled video. Check automatic resume,
Stop while waiting, and a new remote Play command. Test on LCD and TV as needed.
The six focused host checks pass; actual PSP/network recovery needs this test.

Server 0.1.56: web favorites/history/continue watching and background name search
across enabled sources. The matching PSP pair syncs shortcuts when opening
Comfort while idle, not during playback. Local downloads/passwords stay on PSP.
Keep PSPStreamer.state and PSPStreamer-comfort.id. See docs/WEB_COMFORT.md for
sync/storage/search limits and the hardware check.

Measured theme update: windowed visuals and menu backgrounds now use the user's
exact LCD/TV screen rectangles. Text is bounded to the glass, long LCD rows use
ellipsis, and English/German sidebar messages are shortened. Cover heights,
settings rows and spectrum partial redraws follow the new limits. See
`docs/THEME_LAYOUT.md` in the repository. Install both PSP files; no server update
is required. Fullscreen, receiver controls and the separate help strip remain.

Monkey title fix: use normalized texture coordinates for the 3D title surface.
MilkDrop's 2D title path is unchanged. Install both PSP files; no additional
server update beyond 0.1.55 is required. In Monkey, press X to show the title
again for five seconds (LCD/TV, windowed/fullscreen).

Install **EBOOT.PBP and PSPStreamer.prx together**. Keep the other firmware
bridges, subtitle font, Monkey textures and all PSP/SYSTEM config/state files.
The previous release pair and preset directories are archived on the build PC.

## Server

Update the Docker or Home Assistant server app to **0.1.55** and reload the web
page. Remote control → **PSP buttons and text input** opens the virtual controller.
Click/hold buttons, drag the analog stick, or use the keyboard shortcuts shown
there. Mouse users can latch L/R; multi-touch supports simultaneous buttons.
This does not control XMB or other apps and does not mirror the screen.

Version 0.1.54 releases quick button clicks locally after 60 ms, independently
of network delays. Holding a button for at least 350 ms enables hold renewal.
Update both the server and PSP files for this protocol change.
In the local-storage browser, START now exits the app through its normal cleanup;
Circle returns to the main browser. During playback, START still stops playback.

Version 0.1.55 leaves two seconds between a virtual-controller response/failure
and the next request, even while holding buttons. This reduces repeated PSP TLS
handshakes. Tap/text events wait for collection for at most 15 seconds, but hold
leases remain short. Held/analog input can be intermittent: this is deliberately
slow settings control. The existing media remote is unchanged. Update both ends.

Open a text field in the PSP app. Send UTF-8 text from the phone to replace the
draft; then START accepts or Circle cancels. START in the settings page saves.
Existing password contents are never read back. Use HTTPS for remote access.
HACS integration 0.1.3 does not need an update for these web UI changes.

## Visualizations

MilkDrop and Monkey use the full inner monitor on LCD and TV. X repeats the
five-second track-title effect; TRIANGLE still toggles fullscreen. Titles enter
MilkDrop feedback and may leave decaying trails; Monkey projects coloured text
inside the scene. Spectrum retains its normal metadata area.
Automatic music changes retain the visualization, fullscreen choice and GU
state. Audio resources still reset safely. Metadata waits advance silent frames;
short synchronous cleanup/network waits hold the previous frame.

The new local preset folder has 51 shaderless, unmodified third-party originals
selected for modest CPU requirements. Its README/manifest records provenance.
Move the old PSP preset folder aside rather than merging the new pack into it;
otherwise the old tests remain visible. Never discard your personal presets.
Development presets are still available in source and in the release archive.
The repository carries the reproducible pack builder, not a blanket grant to
redistribute those third-party originals publicly.

## Focused checks / hardware test

Built first with -O3 -G0, then checked only affected paths: native input mailbox,
key ordering/expiry, UTF-8 field isolation, real authenticated HTTP/CSRF, browser
controls at desktop/mobile sizes, translated help, LCD/TV UI and autoplay paths.
Docker/HA source parity passes. All 51 pack entries parse and complete 120 host
evaluation frames without resource clamps. This is not a GE/FPS measurement.

On PSP: navigate settings remotely, hold a direction, test L/R combinations,
send an umlaut-containing profile name and cancel a password edit. Close the
browser while holding a button and ensure repetition stops. Then test both
visualizers in LCD/TV and fullscreen, X title replay, automatic music changes,
Stop and a switch to video. Try the pack at 333 MHz; frame rate and appearance
still require actual hardware validation.
