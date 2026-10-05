# Original Xbox — native player preview 0.7.2

0.7.2 adds native **PGS bitmap subtitles** under the same Subtitle overlay
setting. Sprites are fetched independently, then alpha-blended into the YUY2
presentation buffer, never the decoder's reference pictures. Coordinates follow
the source canvas at every video/output size; BT.601/709 follows the video path.
Hardware output uses its existing buffer; software fallback uses one reusable
packed buffer. Seek, reconnect and stop cancel/free the subtitle worker/cache.

Bounded PGS support: up to 8192 cues, four simultaneous objects, 262144 indexed
pixels per sprite (plus its 1024-byte palette), four cached sprites. Tracks
outside these limits, non-PGS bitmap codecs, empty/invalid metadata or initial
fetch failures retain server burn-in rather than losing subtitles. A later
prefetch failure is reported while media playback continues. Console testing
is still required. This is not completion of offline storage or HTTPS.

0.7.1 adds **Text subtitle overlay** in Settings (default on, changes apply to
the next playback start). It shares the PSP's millisecond cue-page parser and
stripped-text/UTF-8 transport, with two bounded pages and cancellable prefetch.
Text is shown against the displayed video PTS, including after seek/reconnect.
Both PVIDEO color-key and software presentation draw the text on the client;
unsupported responses retain server burn-in. Turn the option off to
use server rendering for complex ASS styling. The library/status footer is eight logical pixels lower to clear
the theme border. Hardware verification of this build is still required.

External Monkey textures go in `monkey/` beside `default.xbe`:
`supertex_a1` through `supertex_a5`, `supertex_b1` and `supertex_b2`, with
`.jpg`, `.png` or `.jpeg` extensions. See `monkey/README.txt` for size limits.
The shared loader uses that same relative directory on Xbox; restart Monkey
after replacing files. Original copyrighted images are not bundled.

0.7.0 optimization checkpoint (new console test required): direct GPU scaling
and presentation for Monkey/MilkDrop avoids CPU readback and HD scaling. The
effect setting `Direct GPU` can disable this path for comparison. SSE1 matrix
transforms preserve operation order; static receiver updates touch only the
needle region. Monkey looks four tunnel sections farther ahead (20 instead of
16, with matching path-cache capacity). Its exit hold now displays integer
seconds/tenths correctly on nxdk, whose printf lacks floating-point formatting.
MPEG-1/2 now services queued audio between completed slices (at most every
20 ms), so a costly HD picture need not block audio refill until the entire
picture finishes. This does not change decoding, quality or PTS synchronization
and is not a claim of higher decode throughput. Decoder chunk allocation also
fails cleanly instead of continuing with a NULL buffer under memory pressure.

The user confirmed the previous spectrum and MilkDrop improvements. This build
does not claim completion of downloads, client subtitles or HTTPS below.

0.6.9 preserves static music GUI pixels and updates only the effect/instrument
rectangles. Spectrum decay follows the PSP's 50 ms envelope independently of
redraw speed. Windowed spectrum clears only its active backing rows.

New portable features (console acceptance pending):

- Server hostnames using DHCP-provided DNS, cancellable within the network
  worker with an 8-second lookup deadline and at most four outstanding requests.
  Enter just the hostname in `host=`, not `http://`. **HTTPS is not implemented.**
- Settings: session stop timer, off or 15–180 minutes. Runs across file changes
  and pauses, stops playback/retries/autoplay, does not power off or rearm on boot.
- Video options: save/remove audio and subtitle choices for the series or file
  folder. Uses the existing durable server API, matches language/title rather
  than blindly carrying stream indices. Not offered for music/radio/DLNA.
- Right-stick click: contextual, paged help with controller diagram; left/right
  selects topics, B returns. Active video pauses while help is open and resumes
  afterwards. Library Y still opens help.
- Server **0.1.77**: separate Xbox one-shot menu buttons and text form under
  Remote control. Polling remains three seconds, no held/repeating network keys.
  Open a keyboard field first, send replacement text, confirm using START.
  Text is accepted only for the same field token; accepted/expired commands are
  erased. Setup of the initial connection still needs the controller/config.
- ICY station/title updates piggyback on that mailbox; Xbox radio framing fixed.

Offline downloads/local playback and TLS remain open implementation work;
client text/PGS subtitles need console verification, and server burn-in remains available. Full remaining
scope and optimization costs: `docs/XBOX_NEXT_STEPS.md`.

0.6.8 also prevents an interrupted network stream from waiting indefinitely
for a stale final DMA slot after its encoded queues are empty. It enters the
existing error/reconnect path; clean EOF draining and sample PTS are unchanged.
GUI and effect cadence now use a 20 Hz start-to-start ceiling, not extra waits
after expensive frames. Preset equations are unchanged.

0.6.7 replaces SDK byte-loop memcpy in the hot framebuffer, effect scaler and
texture staging/readback paths with bounded SSE1 block copies. Serial sampling
of 0.6.6 caught the byte loop in both texture upload preparation and effect
presentation. No SSE2, resolution reduction or preset simplification is used.
Five-second visual timing summaries separate composition and final presentation
without debugger halts. Speed on the Xbox remains a hardware acceptance test.

0.6.6 fixes the serially captured flight-start access violation: preserve the
shared CaveScene's 64-byte allocation alignment (required by generated SSE
instructions), instead of substituting ordinary malloc. Texture conversion now
uses cached staging/row reads and sequential GPU uploads; command packets are
batched before GPU publication. Audio-only cooperative refill runs between
rendering phases/batches without another decoder thread or renderer reentry.
Audio peak collection no longer reads samples back from write-combined DMA RAM.
The linked-build audit checks references to assertion-only nxdk C placeholders,
not just math objects. Real-console speed and flight-start validation remain
required; this does not claim complete SDK or Xbox feature coverage.

0.6.5 replaces the SDK's unimplemented strtof/strtod/strtold calls in application
code with musl v1.2.5 floating-point conversion and a string-only Xbox adapter.
License/source are included under vendor/musl. Presets are not modified.
Serial output confirmed Monkey stop completes; visualization speed remains open.

0.6.4 fixes the 0.6.3 stop assertion: preserve HAL's virtual framebuffer pointer
and restore physical scanout through pbkit, not XVideoSetFB. Pause audio DMA
before teardown. Monkey/MilkDrop/Spectrum now share an opaque integer scaler
which caches/reuses rows instead of repeatedly sampling GPU memory or using
SDL's general-purpose scaled texture path. Console speed remains to be measured.

0.6.3 skips the hidden menu pass beneath fullscreen music (including Spectrum),
copies only the active effect rectangle in windowed mode, and explicitly returns
scanout/encoder ownership to HAL after pbkit teardown. Stop-stage diagnostics
distinguish cleanup stalls from a still-running but invisible UI. Hardware
verification of the reported black screen and speed remains pending.

0.6.2 fixes the NV2A zeta-buffer limit error: custom color/depth targets use
pbkit's base-zero RAM DMA context instead of offsets into its own framebuffers.
The depth allocation/pitch covers both the 720x480 composition and 512x512
feedback surfaces. Console verification is still required.

0.6.1 fixes the SDK assertion on starting Monkey: the Xbox port now supplies
`exp2*` and `expm1*`, which are placeholders in the pinned SDK. A focused link-map
check rejects remaining linked SDK math assertion objects. PSP formulas and
sources are unchanged; real-console rendering verification remains necessary.

## Monkey and MilkDrop test build (0.6.0)

Copy `visual-font.raw`, `presets/` and `monkey/` beside the updated XBE and
existing theme/font. Preserve personal config, scores and external textures.
Music **X** now opens visualization selection, options and presets; **Y** toggles
fullscreen. The shared PSP engines include live transitions and Monkey flight,
combat, ships, scores and optional rumble (strengths default to zero).
Monkey LT+RT opens the intro; hold both five seconds to leave. Left stick steers,
A fires/confirms, D-pad up/down changes speed; LT/RT roll, double tap for a barrel
roll. Intro left/right selects the ship. Settings persist across restarts.

NV2A renders bounded offscreen effects (512x256, optionally 512x512 MilkDrop),
then SDL composes/scales to the selected output. This is not full-HD feedback
rendering. HLSL remains unsupported. Build and focused host checks pass; console
appearance/performance and music → video → music still need testing. PSP sources
and video PTS synchronization are unchanged.

GUI navigation and native video **with audio** were confirmed on a real Xbox
with 0.2.5 and 0.3.0. Version 0.5.0 extends that working foundation; its new features
still require console testing. It is a native nxdk XBE, not an XBMC skin or a
PSP executable. The proven PTS/sample-position clock is unchanged.

## Install and test

1. Update the PSPStreamer **server to 0.1.77 or newer** (Docker or Home Assistant).
2. Copy `bin/default.xbe`, `bin/theme.png` and `bin/font.ttf` to the same Xbox
   application directory. Keep the existing `server.cfg`, or create it from
   `server.cfg.example`. The font and image are required.
3. Use the direct LAN HTTP port. `host` is a hostname or IPv4 address without a scheme/path;
   `port` defaults to 8091, `password` is the existing server password. HTTP/Basic
   authentication is appropriate only on a trusted LAN. No TLS implementation yet.
4. Begin with `output_height=480` and standard video quality. Play one music
   track, then one video; check VU response, start sync and sync after several
   minutes. Test pause/resume, +/-30 seconds, Stop and a second title.
5. The app writes `xbox-player.log` beside the XBE (overwritten at startup).
   Copy it from the dashboard after the test. It records PTS, presented/dropped
   frames, underruns and queue occupancy, not credentials.

### Controls

| Context | Buttons |
|---|---|
| Library | D-pad up/down: entry; left/right: page; A: open; B: parent; Start: reload |
| Library | X: settings; Y: help; LB: selected-file/playlist actions; RB: search |
| Search keyboard | A: character; X: delete; Y: clear; Start: search; B: cancel |
| Playlist actions | Add/remove, move up/down, enable queue, repeat off/one/all, queue shuffle |
| Media options | Up/down: row; left/right: audio/subtitle/quality; A on Play: start |
| Playback | A: pause/resume; Start: controls menu; B: stop; left/right: -/+30 seconds |
| Video | X also opens the controls menu: pause, seek, chapters, previous/next file, stop |
| Music | X: visualization selection, spectrum/Monkey/MilkDrop options and presets |
| Playback | Up/down: volume; Y: fullscreen/receiver view |
| Network request | B: cancel, including stalled requests |
| Menu | Back: return to dashboard |

A long pause releases the stream/encoder. Resume requests a fresh stream at
the recorded audio position; it can therefore take a moment. Network failures
never count as a clean end: interrupted streams retry the same position up to
three times, with B to cancel. Live radio reconnects at the live position.

## New in 0.3.0

- Options X toggles source resume/from-start; Y shows duration, track counts,
  artist/album and a compact synopsis. Music hides video/subtitle controls.
- Playback LB/RB selects previous/next file; triggers select previous/next
  chapter. Clean EOF can automatically advance; errors never skip a title.
- Settings: autoplay next, repeat current, music folder shuffle, spectrum,
  video quality, volume, preferred audio language and subtitle language/off.
  Existing server series preferences take precedence over global languages.
- `preferences.cfg` beside the XBE stores these settings, with a `.bak`
  recovery copy. Updating the executable never replaces personal settings.
- Provider resume points and shared direct-file history are available. Root
  rows expose shared Favorites, Recently played, Playlist and Search.
- Plex/Jellyfin/DLNA artwork reuses the bounded server RGB565 image service:
  1280x720 dimmed backdrop and 240x336 cover; it does not decode unbounded images
  on the console. Artwork failure never prevents Play; Play cancels a pending
  optional image request. Artwork size is bounded, not an arbitrary source image.
- Music has a 24-band windowed frequency display plus the analog VU meters.
  Y toggles its fullscreen display; frequency analysis is disabled for video.
- Configured radio stations are playable; resume returns to live radio.
  ICY station/title display requires server 0.1.77 or newer.
- Playback progress (15 seconds), pause and stop go to Plex/Jellyfin and
  shared history in a bounded background queue, not the render/audio thread.
  Browser playback reports the same events. Client/session identities and
  monotonic report sequence numbers prevent cross-client queue interference
  and stale unload reports. These clients never consume PSP remote commands.
- Sequential video playback crosses numbered season folders of the same
  series. Plex/Jellyfin use their season metadata; files/DLNA recognize
  `Season 1`, `Staffel 1`, `S01`, etc. No arbitrary recursive folder traversal,
  music-album continuation, or modification of explicit provider playlists.

## New in 0.3.1

- Visible playback menu (Start/X) with chapter and file navigation. Music X
  opens persistent spectrum settings; LED segments light only as whole units.
- Web playback target **Xbox**, with its own current-title/status/seek and
  previous/pause/resume/stop/next controls. One Xbox mailbox, three-second
  asynchronous polls, one outstanding request, commands expire after 15 seconds.
  PSP command/state storage remains independent; this is not an Xbox HACS entity.
- Audio reserve increased from 10 to 24 existing DMA slots (576 ms), starting
  after 16 slots (384 ms). Hardware sample-position/PTS sync is unchanged.
  Routine log writes now run in the reporting worker rather than the playback
  thread. The rare audible stalls still need hardware verification.
- Fixed unsupported floating-point printf formatting in seek URLs and logs.
  Old `pos=` log columns were shifted and cannot be read as underrun counts.
  New diagnostics use `pos_ms=` and integer formatting.

## New in 0.3.2

- Reuse **unmodified** PSP `spectrum_analysis_impl.h` and `spectrum_paint.h`:
  desktop FFT (default) or original 12-bin legacy analysis; 12/24/32/48/64 FFT
  bands, -24..+24 dB display gain, Original/Rainbow/VU/Ice/Fire colors, separate
  LED toggle, 8–32 whole segments and peak hold. An Xbox adapter supplies the
  DMA-current PCM snapshot and an SDL texture. Analysis runs in the UI at up
  to 20 Hz, not during audio submission. The old 24-bin approximation is gone.
  One Xbox band-count preference replaces PSP-specific LCD/TV preferences.
- Main menu **X → Server connection**: edit hostname/port/password, test without
  saving, then Save and connect; B cancels. The controller keyboard follows
  the PSP editor: A types, X deletes, Y clears, Start accepts, B cancels.
  Passwords remain masked. All HTTP workers are quiesced before credentials
  change. `server.cfg` is atomically replaced via a `.bak` recovery copy.
  HTTP/trusted LAN only; DNS is supported, HTTPS is not implemented yet.
- Settings include debug logging and a 0–30 second next-episode countdown
  (0 keeps immediate continuation). A advances now; B cancels the countdown.
  Error/assertion diagnostics remain available when routine logging is off.
- A thin real-position progress bar returns at the bottom of fullscreen video.
- Web target selector is visible even without selecting a media file and is
  remembered per browser. Current playback, position and Remote control use
  the selected Xbox/PSP; PSP-only controller buttons hide for other targets.

### Still separate porting work

Xbox offline storage, HTTPS and local subtitle overlays
remain separate work. No parity with
all PSP features is claimed. Existing PSP plugins cannot run as Xbox plugins.

### Languages, library conveniences and artwork (0.5.0)

- Settings → **UI language** selects Dashboard (Auto), English or Deutsch.
  Auto reads `XC_LANGUAGE` without writing the EEPROM; unsupported dashboard
  languages fall back to English. The choice applies immediately and persists
  in `preferences.cfg`: `ui_language=0` (auto), `1` (English), `2` (German).
  Named UTF-8 strings live in `language_strings.h`; keep formatting placeholders
  identical in both columns. Technical decoder/network diagnostics remain English.
- Favorites, Recently played, Playlist and Search appear at the library root.
  Search covers films, series and music across enabled sources. It progressively
  refreshes results and shows running/limited/error counts; RB edits the query.
- **LB** on a media row opens favorite and playlist actions. In Playlist,
  remove/reorder the selected row and choose repeat off/one/all or queue shuffle.
  Queue shuffle is separate from music-folder shuffle. Enable the playlist to
  use its order at track end or with Previous/Next. The existing global autoplay
  switch still controls automatic advancement. Queue track/subtitle selections
  are reused; Xbox encoding settings are independent of PSP queue quality.
  Shared favorites/queue stay in the server's persistent state directory and
  are also visible in the web UI/PSP. Concurrent queue edits require a refresh
  rather than silently overwriting another client's changes. Live radio has no
  natural playlist end and is not added to the queue.
- **MP2 quality (48 kHz)** selects 128/192/256/320/384 kbit/s (default 192).
  `audio_quality=0..4` stores the corresponding choice. The codec/sample clock
  remain MP2/48 kHz; no unsupported AAC, MP3-VBR or passthrough choices appear.
  It applies on the next stream start and can also be selected in the web remote.
- Xbox artwork uses a separate bounded endpoint: **1280×720 background** and
  **240×336 cover**, instead of PSP 320×180/80×112. Preserve aspect ratio, crop
  backdrops and pad covers; missing images never block playing a file.
- The web remote now uses the **same playback panel as PSP**, including current
  item adoption in a fresh browser session, chapters, seek and transport buttons.
  Only the selected target's status/mailbox is used; the duplicate Xbox panel
  is removed. PSP download-quality settings remain separate.
- HACS integration **0.2.0** adds a separate Xbox media player with stable ID
  `<server-id>-xbox`, authenticated artwork and Xbox-only commands. The existing
  PSP entity ID is unchanged. Update the separate PSPStreamerHA repository.

Console checks: switch EN/DE/Auto and restart; add/remove a favorite; search,
reorder a two-item playlist and check repeat/shuffle; select an MP2 bitrate;
open a Plex/Jellyfin title to inspect artwork; check web/HA Xbox controls do
not control a concurrently connected PSP. Host checks do not replace these.

## Display and performance

0.4.6 accepts the web remote's Xbox video resolution on Play. Server 0.1.75
offers all six sizes plus "Use Xbox setting"; the latter sends no override.
The browser remembers its choice separately from PSP download settings. Music
and other targets hide it. This controls encoding resolution, **not** the Xbox's
physical TV output mode, and does not restart an already playing stream.

Audio completed-descriptor counts and the sample clock no longer move backwards
when DMA exposes an older ring slot after a stall. This is not a promise of
smooth full-HD decoding: 0.4.5 logs show rising dropped frames with 1080p video,
while 720p video on 1080i output is much steadier. The initially filled buffer
can hide the load briefly; CPU MPEG decoding is still required. No automatic
quality reduction. The 0.4.5 episode transition was confirmed on hardware.

0.4.5 increases the Xbox packet limit to 1 MiB together with server 0.1.74.
Complex 1080p MPEG-2 I pictures exceeded the former 256 KiB limit and could
terminate streams. The 1.5 MiB queue budget remains unchanged; packets allocate
only their actual length. Both client and server need updating for this repair.
Original network/allocation errors are retained instead of being overwritten
by a generic MPEG interruption. PSP and browser transport are unchanged.

At clean audio EOF, one zero-filled DMA descriptor is appended to the existing
ring. Once both output channels reach this marker, all real samples have played;
the media clock ends at the exact final sample PTS. This avoids relying solely
on the controller's terminal cursor/PICB behavior. No timeout skips the episode,
and no marker is inserted for network errors. Autoplay still waits for video
decoder drain as well. Real-console episode-end confirmation remains pending.

0.4.4 corrects vertically stretched video in 1080i output: the PVIDEO destination
uses 540 field lines, while the RGB GUI/color-key rectangle remains in 1080
framebuffer lines. Both fullscreen and embedded video use the correction,
following XBMC4Xbox ComboRenderer's 1080i handling. Source quality, SD, 720p,
software presentation and timestamp synchronization are unchanged. The 0.4.3
speedup and the 0.4.4 geometry correction were confirmed by the user.

At track end, AC97's halted-at-last-valid-descriptor status now takes precedence
over a residual sample count (only while RUN is set and current equals last).
This avoids an endless drain wait in that hardware state. The reported autoplay
stall still needs hardware confirmation: older logs lacked the DMA status needed
to establish its exact cause. End diagnostics now include network/clean-EOF,
decoder/pending-frame state and both DMA cursors. No timeout forces completion;
only a clean, fully drained stream may advance to another episode.

0.4.3 repairs the PVIDEO startup handoff: reset only the overlay engine, clear
the inherited pending-buffer state and release STOP before first submission.
The 0.4.2 console log showed **zero GPU submissions and software fallback**;
it was not evidence of successful GPU playback. The new hardware behavior
requires another console test. Ongoing buffer ownership checks remain active.

GUI updates now copy damaged rectangles only; static screen content remains
in SDL's existing backing surface. Textures for text use a bounded 96-entry /
2 MiB LRU cache, cleared on output changes and shutdown. The YUY2 packer handles
two luma rows per chroma load. PTS/audio synchronization and the decoder are
unchanged. `gui_kib` and `gui_copies` quantify actual framebuffer copies.
If playback sticks at Buffering after a dashboard/warm restart, cold boot first:
the previously observed audio-DMA startup issue is separate and remains open.

0.4.2 introduced **NV2A PVIDEO hardware video presentation** as the default. The CPU
still decodes MPEG-1/2 with libmpeg2, but packs YUV420 into YUY2 with Pentium III
MMX/SSE1; the video unit performs YUV-to-RGB conversion and scaling. Two packed
buffers replace per-frame RGB conversion/scaling/copying. This is not MPEG
hardware decoding, nor a GPU renderer for the GUI or visualizations.

Settings → **Video renderer** selects **NV2A overlay (auto)** or **Software**;
`video_hardware=1` / `0` in `preferences.cfg` stores that choice. Allocation or
persistent buffer-busy failures fall back to software for the stream. The
color-keyed GUI keeps playback controls and the progress bar visible, including
when video fills the screen. Progress/receiver updates run at 4 Hz while video
retains its timestamp cadence; control selection and fullscreen changes redraw
on the next video frame. Pause/seek/stop disable and release the overlay.
No server update beyond 0.1.73 is needed, and PSP code is unchanged.

Test a familiar episode at 720x480 first, then 720p if desired. Check color,
aspect, fullscreen/windowed, controls, pause/seek, stop and next episode. Compare
Software with NV2A at the **same video and display sizes**. Debug logs include
`renderer`, `gpu_frames`, `gpu_busy`, cumulative `decode_ms`, `pack_ms` and
`gui_ms`, alongside audio underruns and dropped frames. Console scanout and
performance remain unverified until this hardware test. Implementation and
focused test details: [GPU video notes](../docs/XBOX_GPU_VIDEO.md).

0.4.1 fixes output changes returning to the dashboard: the pinned SDL Xbox
driver retains its singleton window pointer after destruction. The app now
compiles that same upstream driver with a narrow DestroyWindow adapter that
clears the pointer. The SDK checkout is unchanged. The actual upstream failure
and repeated recreation across five mode sizes are covered by
`tests/xbox_video_lifecycle.c` (host hardware-boundary stubs; not a TV test).
Logs now identify hardware switching, window/renderer creation and theme load.
No server update beyond 0.1.73 is required for this client-only repair.

- Target: ordinary **64 MB / stock-clock Xbox**. No overclock requirement or
  overclock changes. Actual performance is subject to the hardware test.
- Default output: 640x480 (or the console's enabled 480p mode). Settings →
  Display output lists the cable/region/EEPROM-compatible modes, including
  enabled 720p and 1080i. A applies immediately; A again keeps it, B restores
  the old mode. Without confirmation it reverts after 15 seconds. Selection
  is saved in preferences.cfg; no region/EEPROM changes are performed.
  The pinned nxdk has 480-line PAL framebuffers, not native 576i/576p modes.
  Its 480p flag selects progressive instead of interlaced 480 output; the app
  does not change that dashboard setting. RGB is never forced into HD modes.
- The existing 720x480 PSP TV theme and needle/control coordinates share one
  scaling transform. Video is aspect-fitted. No AI repainting alters the artwork.
- Video choices: **480x272, 640x360, 720x480, 720x576, 1280x720, 1920x1080**.
  Both SD 720-wide profiles are anamorphic 16:9; 4:3 sources are pillarboxed.
  Settings → TV shape selects 16:9 or 4:3 letterbox (set the TV accordingly).
  Output and encoding resolution are independent; 1080i output does not
  promise smooth HD decoding on a 64 MB console. HD profiles are experimental.
- Decoder: **libmpeg2 0.5.1**, with Pentium III MMX/MMXEXT (not SSE2), shared
  by selectable MPEG-2 (default) and MPEG-1. No B pictures; real packet PTS are
  attached to the decoded pictures, including the final delayed reference
  picture. Clean EOF drains every picture and the PCM queue before autoplay;
  interrupted transport never counts as EOF. Decoder frame allocations fail
  gracefully rather than relying on unchecked internal allocations.
- MP2 stereo remains 48 kHz / 192 kbit/s. Audio downmix offers Stereo,
  Dolby Surround and Dolby Pro Logic II, performed on the server. Select the
  corresponding decoder on your receiver. Matrix stereo is not discrete
  AC-3/DTS passthrough; passthrough remains deferred. Existing stereo sources
  are not turned into genuine discrete surround channels.
- Video target rates by size: 1.5 / 2 / 4 / 5 / 9 / 16 Mbit/s. The server
  requires the new Xbox profile parameters; old clients retain MPEG-1 defaults.
  Native build uses **-O3 and LTO**, without fast-math or clock changes.
  Decoding stays on the CPU; NV2A accelerates presentation, not MPEG decoding.
- No H.264 decoding or hardware codec acceleration is claimed for this preview.

## Synchronization and isolation

The early file log misleadingly ended around controller initialization.
Serial KD subsequently proved the HTTP `sscanf` access violation (fixed in
0.2.4); the screen then exposed nxdk's unimplemented `strtod` (fixed in 0.2.5).
Do not treat the old controller hypothesis as a confirmed hardware fault.
SDK C assertions are saved to disk, sent over serial KD and displayed before
the fatal halt. Keep `xbox-player.log` if a new SDK assertion occurs.

Preview 0.2.1 fixes the first-menu black screen caused by a clip command
without a preceding viewport after SDL flushes temporary text textures.
`tests/xbox_clip.c` reproduces the crash with the pinned SDL source on a host
and verifies the corrected command order. Audio initializes only on Play;
startup stages and assertions are now recorded in `xbox-player.log`.

Separate `/api/xbox-stream/<id>` output encodes MPEG-TS, extracts actual 90 kHz
PES timestamps and sends bounded access units in an `XSM1` stream. Header:
`XSM1` plus little-endian flags (video=1, audio=2). Each record is `<c3xIq>` plus
payload (type, length, signed PTS). `V` is one MPEG-1/2 picture, `A` one 1152-sample
MP2 frame, `E` clean EOF. B pictures are disabled.

The client uses libmpeg2 for video and pl_mpeg for MP2 audio, **not a frame-count
playback clock**. Audio is master: the AC97 DMA descriptor and remaining sample count
locate the actual playback position inside the packet's PTS. Video waits for
its PTS; an excessively late image can be omitted without omitting predictive
decoding. Audio is never repeated to catch up. There are no 20.1/20.2 correction
factors or guessed output-latency offsets. The 23.976 encoding cadence limits
decoder load; it is not the sync clock. Video-only playback uses a monotonic
clock anchored to its first PTS. After a shorter audio track ends, video can
continue from the last audio time.

PC checks cover timestamps against FFprobe, real low-level decoding, packet
bounds, HTTP authentication and encoder cleanup. **DMA timing, cable/display
latency and sustained frame rate require the real Xbox test.**

PSP encoder options and playback/control protocols stay unchanged. Xbox and
browser streams have their own formats and never send PSP control commands.
They share source/track/subtitle resolution and server capacity only. Xbox
subtitles are server-burned, including supported bitmap tracks. The paged
library exposes Files/Plex/Jellyfin/DLNA, 64 entries per page.

## Build and licenses

Preview 0.2.5 also replaces the SDK's unimplemented `strtod` duration conversion
with a JSON decimal/exponent parser and sends SDK assertions over serial KD.
Preview 0.2.4 fixes a hardware-confirmed startup access violation in the pinned
SDK's `sscanf`: suppressed `%*s` still consumed/wrote an argument. HTTP status
parsing now uses a bounded, explicit parser. This affects only the Xbox client.

### Serial diagnostics (optional)

With a compatible Xbox serial debug port and kernel debugger enabled, run
`python3 xbox-client/tools/kd_serial.py /dev/serial/by-id/YOUR_ADAPTER` with
permission to access that port. The transport is 115200 baud, 8N1. Startup
messages include the thread address and stage; `bin/player.map` maps this
specific build's addresses to functions. Do not use a map from another build.

The small KD client acknowledges debug output and continues module-load
notifications, but **does not automatically continue exceptions**. Commands:
`break`, `context`, `read HEX_ADDRESS BYTE_COUNT`, `version`, `continue`,
`resume-break`, `reboot`, `quit`. `resume-break` advances EIP only over a verified
INT3 breakpoint; never use it to bypass an application fault. `reboot` requests
a console reset and may require a manual restart. Keep the client connected
while debugging: a stopped kernel can otherwise wait for its debugger.
Protocol layouts follow the [ReactOS KD definitions](https://github.com/reactos/reactos/blob/master/sdk/include/reactos/windbgkd.h).

Requirements: Git, make, Clang/LLD, flex, bison, CMake; no proprietary XDK.

```sh
git clone --recursive https://github.com/XboxDev/nxdk.git .toolchain/nxdk
git -C .toolchain/nxdk checkout 14d5ee97e73347c973f1f57b68b79ec08c9e77f2
git -C .toolchain/nxdk submodule update --init --recursive
export NXDK_DIR="$PWD/.toolchain/nxdk"
export PATH="$NXDK_DIR/bin:$PATH"
make -C xbox-client -j4
python3 xbox-client/package_preview.py
```

The packaging helper uses DejaVu Sans under `/usr/share/fonts/TTF/` and its
license under `/usr/share/licenses/ttf-dejavu/`; adapt those paths for other
distributions. Output is **one** `xbox-client/bin/` directory, separate from
stable PSP/firmware releases. Preserve licenses and sources when distributing.

- [nxdk](https://github.com/XboxDev/nxdk), pinned above, supplies the Xbox HAL,
  network, controllers and SDL2/SDL_image/SDL_ttf/FreeType dependencies.
- [pl_mpeg](https://github.com/phoboslab/pl_mpeg), vendored unchanged at
  `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023`, and [jsmn](https://github.com/zserge/jsmn)
  retain their MIT notices in `vendor/`.
- New source is GPL-2.0-or-later. The linked binary uses the **GPLv3** option
  because nxdk USB includes Apache-2.0 components; other targets are not relicensed.
  Font and dependency notices are included in `licenses/`.
