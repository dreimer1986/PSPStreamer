# Earlier development notes

Historical snapshots moved from the former README. Version-specific statements and test notices describe their original state, not necessarily today's release. Start with the [project overview](../README.md) and [Setup](SETUP.md).


Latest release draft: [2.3 — PSP Consolizer, Fullscreen TV Gaming and Configurable Rumble](../docs/RELEASE_2.3.md).

See the [changelog](../CHANGELOG.md) for the release summary since tag 1.8.

Server **0.1.75** includes a [browser playback target](../docs/BROWSER_PLAYER.md):
choose **This browser** in a media item's playback target selector. Video/music,
audio-track/subtitle selection, seek, automatic next episode and Plex/Jellyfin
progress reporting run independently of PSP control. Sequential episodes can
continue into the next season of the same series, also for PSP and Xbox.
The experimental [native original Xbox player preview](../xbox-client/README.md)
adds the familiar receiver GUI, VU meters and timestamp-driven MPEG-1/2 + MP2
playback. Version **0.3.0 video/audio is hardware-confirmed**; **0.4.1 with server
0.1.73** adds libmpeg2, SD/HD encoding profiles, runtime output selection,
anamorphic widescreen and optional matrix-surround downmix. It retains the
transport/chapter menu, shared PSP spectrum code, in-app server settings,
fullscreen progress and independent Xbox web remote.
New features await console verification. PSP decoding is unchanged.

[FuSa Fullscreen](../psp-fusa-probe/FULLSCREEN.md) is the standalone game-TV
scaler, with Sony HOME, CustomHOME and OC/Consolizer overlay support. Install
`SEPLUGINS/FuSaFullscreen/FuSaFullscreen.prx` with its INI and `dvemgr.prx`.
It remains separate from PSP Consolizer; replace old plugin entries when
updating and preserve your INI settings.

[PSP Consolizer](../psp-controller/README.md) brings StreamMaster Bluetooth input
to games and XMB, with optional TV activation and compact overlays. Install
`SEPLUGINS/PSPConsolizer/PSPConsolizer.prx` plus `PSPConsolizerUSB.prx` and
`PSPConsolizer.ini`; remove old plugin entries instead of enabling two copies.
Firmware 0.3.13 and the matching PSPStreamer build add a learnable **PS / Home**
button. Find it under Settings > StreamMaster USB > Bluetooth controller >
Buttons / reconnect. The English/German in-app help now covers these menus
and the physical NOTE + Volume Down USB-storage hand-off in XMB.

Music analysis now offers an optional [desktop MilkDrop FFT mode](../docs/SPECTRUM_ANALYSIS.md)
and 12/24/32/48/64 analyzer bars. The original light 12-band mode remains the
default. Configure it in PSP Settings or press Circle while using the spectrum.
FFT display gain (`spectrum_gain_db`, -24..+24 dB, default 0) affects only the
bars, not volume or effect response. Desktop FFT mode also enables Monkey's
[reference audio analysis and adaptive beat detector](../docs/MONKEY_AUDIO.md).
No server update is required.

Server **0.1.61** adds a **Cover view** switch to all new Plex/Jellyfin provider
views, sharing the library's saved preference. Server/web update only; the PSP
build does not need changing for that web-only update. The newer optional
music-analysis build above remains compatible with this server version.

Server **0.1.60** adds playlist repeat and independent playlist shuffle,
[provider library views and adjacent-file navigation](../docs/PROVIDER_VIEWS.md).
Update both server and PSP EBOOT/PRX. The flight Easter egg now fades in,
blinks/recoils on wall contact and recovers from prolonged blocking. Double-tap
**L** or **R** within 280 ms for a full barrel roll; **L+R** still toggles flight.
Recovery temporarily relaxes collision only until a safe position is found;
it does not implement health, enemies or projectile protection.

Server **0.1.59** adds a [shared playlist](../docs/PLAYLIST.md), editable in the web
UI and on the PSP. Update both server and the matching EBOOT/PRX pair. Entries
and order survive server updates when the existing persistent volume is kept.
The flight Easter egg's exhaust lights now have a wider, music-driven
brightness and amber-to-pale-gold color range.

Server **0.1.58** shows the PSP's current playback in every browser session,
including playback started on the PSP or another controller. Open **Remote
control** or **Control current playback** to load its position and controls
without restarting it. Opening the same library entry also synchronizes its
position. This view follows automatic episode/song changes; selecting another
file deliberately keeps that selection.

StreamerOC optionally supports `overlay_always=1` in its INI (default `0`).
The flight Easter egg now has four warm exhaust outlets driven by filtered music
bass; normal Monkey visualization is unchanged.

Server **0.1.56** adds shared [favorites, history, resume and cross-source
search](../docs/WEB_COMFORT.md). Install the matching PSP pair and open **Comfort**
once to synchronize its existing server-scoped shortcuts. No continuous sync
or extra playback polling is introduced.

The measured LCD/TV screen rectangles, text limits and layout checks are
documented in [Theme layout](../docs/THEME_LAYOUT.md).

### Full browser controller and visualization update (2026-09-25)

Server/app **0.1.55** includes **Remote control → PSP buttons and text input**.
Install the matching PSP EBOOT/PRX pair. The clickable PSP provides the D-pad,
face buttons, L/R, START, SELECT and an analog stick. Hold buttons for repeats;
use multi-touch combinations or the **Hold L / Hold R** switches with a mouse.
Keyboard shortcuts are listed below the controller. This controls PSPStreamer
menus and playback only, not XMB, HOME, hardware switches or other applications.
It is not a live screenshot: watch the PSP or TV for the current menu.

Quick clicks release on the PSP after 60 ms rather than waiting for a network
release message; holding for at least 350 ms enables hold renewal. This requires
both the matching server and PSP update. START also exits the app from the local
storage browser; during playback it still stops the current file first.

Virtual controls leave a two-second gap after each response or failure to reduce
PSP HTTPS overhead. Tap/text events expire after 15 seconds; short held-input
leases are unchanged. Expect delayed clicks and intermittent held/analog input,
not real-time gameplay. Existing media-remote polling is unchanged.

Open a text field on the PSP (also possible with the virtual buttons). The web
text box becomes available; **Send text** replaces that field's draft. Check the
result and press virtual START to accept. Settings still require START again
to save. Circle cancels. UTF-8 is transferred intact; byte limits and existing
field-specific validation remain in force. Font coverage is unchanged.
Passwords stay masked and existing field contents are never sent to the browser.
Use HTTPS outside a trusted network, especially when entering credentials.

Inputs are acknowledged in order and are not saved on the server. A two-second
browser lease, one-second PSP hold expiry, field IDs and app-start IDs prevent
stuck keys, late text in another field and replay after restart. Hiding/closing
the controls releases held buttons; the expiry also works if release cannot
reach the PSP. Only one browser controls the virtual pad at a time. Physical
buttons remain usable and physical analog movement has priority. An unresponsive
or disconnected PSP cannot be recovered through this channel; physical controls
remain the fallback. The existing direct Play/Pause/Stop/Seek controls are unchanged.
Docker and the Home Assistant server app include identical assets. HACS 0.1.3
needs no update for these web-only additions.

MilkDrop and Monkey now use the full inner monitor, on LCD and TV. Track names
appear for five seconds; **X** shows them again. MilkDrop injects a transparent
title texture into its feedback before echo/gamma, so trails can outlive the
five-second injection. Monkey renders a projected, material-coloured title
inside its scene, without a GUI text box. This is a PSP title effect, not a
claim that both desktop plugins implement titles identically.
Automatic music changes retain the renderer and selected fullscreen mode;
audio workers and codec queues are still joined and recreated normally.
Metadata preparation advances the retained effect with silent input; short
synchronous network/cleanup waits retain its last frame rather than a folder.
Stop, errors, playback limits and video playback release those GU resources.

The local release includes a curated **51-preset shaderless pack**; all 51
passed parsing and 120 host evaluation frames, with no resource clamps reported.
333 MHz smoothness still needs PSP testing, particularly with live transitions.
No original equations were changed. Old release tests are archived, not deleted;
development presets remain under `psp-client/presets` in Git. See
[selection and packaging notes](../docs/STANDARD_PRESETS.md) for the reproducible
builder, provenance and third-party redistribution boundary.

### Quick access, resume and travel preparation (2026-09-25)

Update the PSP's matching **EBOOT.PBP and PSPStreamer.prx** together. The new
features use **SELECT → Quick access / playback limits** in the main browser
(one row above Help). No decoder clocks, A/V synchronization or preset formulas
are changed.

- **Resume:** manually opening a video offers **X Continue**, **Square Restart**
  or **Circle Back** when a meaningful position is known. Plex/Jellyfin's returned
  position is authoritative. Filesystem/SMB, DLNA and downloaded videos retain
  their own positions on the Memory Stick after playback returns, including a
  normal Stop and recoverable playback errors. Natural completion clears the
  position. Autoplay, remote starts and internal seeks do not ask again. An
  abrupt power-off/forced app exit before playback returns does not checkpoint
  the latest position; there are no background disk writes during playback.
- **Favorites/recent:** highlight a folder, album, file or radio station, open
  Quick access and choose **Add/remove selected favorite**. Open Favorites or
  Recently played with X; Triangle removes an entry from that list. History
  includes downloaded media. Online entries are scoped to the configured host,
  port and HTTP/HTTPS mode; local entries remain available across servers.
  There are 64 shared records; oldest non-favorites are recycled. Favorites are
  never silently evicted, so a full favorites list leaves no new history slots.
- **Playback limits:** Left/Right sets a wall-clock timer in 15-minute increments
  up to 120 minutes, or stops after 1–10 naturally completed files. Zero disables
  a limit; whichever active limit is reached first stops playback through the
  ordinary cleanup path. Timer time includes pauses/menus; seeks are not counted
  as completed files. These are session-only limits, not power-off commands.
- **Travel overview:** open Local storage with Circle, then **L** for completed /
  incomplete download counts and free space. Completion checks use the ready
  marker and manifest file sizes, including FAT aliases for PC-copied media.
  This is not a fresh checksum scan. Once server conversion is ready, downloads
  show additional bytes needed (existing partial transfers deducted) and free
  space before **X** confirms transfer. Each transferred file still gets SHA256
  verification. Size is not guessed before conversion finishes.
- **Server profiles:** five named slots store host, port, HTTP/HTTPS and password.
  Save main settings with Start first; in Server profiles, Square saves the
  currently active connection, X switches, Triangle deletes a slot. Switching
  closes the settings screen and resets navigation/connection caches, without
  restarting the app. Unsaved outer settings are discarded when switching or
  opening a saved media shortcut. Profiles do not change PSP WLAN credentials.

State is stored separately from application binaries in
**`ms0:/PSP/SYSTEM/PSPStreamer.state`**, with a version/checksum and `.bak`
recovery copy. Normal app and server updates do not overwrite it. Back up this
file together with `PSPStreamer.cfg`; deleting the state file **and its backup**
resets bookmarks, favorites/history and profiles. Profile passwords are stored
in plaintext, just like the active connection in the CFG: protect the stick and
its backups. The file is not included in release packages. Server profile
changes are also saved to the active CFG. Existing configuration keys remain
unchanged.

**DLNA covers (server 0.1.52, HA integration 0.1.3):** the standard
`upnp:albumArtURI` field supplies covers for web browsing, current-media artwork
in HA and idle PSP menus on LCD/TV. Images stay behind the authenticated server
proxy, are bounded and cannot redirect to another host. The PSP reuses the
existing image sizing/caching path. No fake backdrop is generated from a cover;
vendor-specific background-image fields are not interpreted. See the
[ContentDirectory specification](https://upnp.org/specs/av/UPnP-av-ContentDirectory-v3-Service.pdf).
DLNA resume is local to the PSP, not written back to the DLNA server.

### DLNA, original versions and external subtitles (server 0.1.51)

**DLNA:** Settings → DLNA / UPnP → **Discover DLNA servers** searches the
server's local network, not the PSP's hotspot. Found servers are saved and the
DLNA source is enabled. Alternatively enter the device-description XML URL
(for example `http://192.168.1.10:8200/rootDesc.xml`), not a web dashboard URL.
Use **Save sources** to disable/re-enable DLNA; **Remove** forgets one server.
Refresh the PSP library with Square, then open DLNA → server → folders.

Audio/video browsing, range-capable streaming, offline conversion, next/previous
and music shuffle use the existing player pipeline. Original HTTP resources are
preferred over resources marked as transcoded. DLNA cannot guarantee that the
upstream server never transcodes; use the direct Plex/Jellyfin adapters for those
servers. Watched/resume reporting is not standardized by ContentDirectory and
is not promised for DLNA. Vendor-specific sidecar subtitle extensions are not
implemented; embedded tracks work through the existing pipeline.

Automatic discovery needs multicast reachability. The HA app now uses host
networking; set its listening port with the **port** app option (default 8091),
not a Docker port mapping. If an integration/reverse proxy used a container-only
DNS name, point it at the HA host address instead. Existing host-IP/domain
connections on the same port remain valid. Avoid port conflicts.
For ordinary Docker on Linux, use this alternative Compose file from the same
repository directory/project to keep the existing settings volume:

```sh
docker compose -f compose.dlna.yaml up -d --build
```

The ordinary `compose.yaml` remains in bridge mode: manually supplied description
URLs work there, but multicast discovery may not. VLANs, VPNs and isolated WLANs
can also block discovery; manual URLs work when HTTP routing is available.
Only register trusted servers. XML declarations/entities and cross-host resource
URLs are rejected; use the same hostname in the server description/resources.
Limits: 32 saved servers, 100 entries per Browse page, 1,000 entries for automatic
next/shuffle, bounded XML responses and PSP-sized object IDs. Unsupported resources
and servers that ignore the page bound produce errors rather than unbounded reads.

**Plex/Jellyfin versions:** When an item advertises multiple originals, it appears
as a folder containing the versions. Choose a version there on PSP or web; that
choice also applies to conversion/downloads. IDs remain stable if the provider
reorders its list; a removed version produces an error instead of playing another
one. Automatic next-episode playback still uses the next item's default original,
not a guessed equivalent of the previous item's version. Recursive conversion of
a version folder includes all its versions; select individual files if unwanted.
Multipart movies remain unsupported.

**External subtitles:** Sidecar tracks managed by Plex/Jellyfin are appended after
embedded tracks, with language and an `External` label. They belong to the selected
original version. Text subtitles (SRT/ASS/SSA/WebVTT) use the same plain-text PSP
overlay as embedded text; style simplification is unchanged. Single-file PGS
sidecars use the existing bitmap pipeline. Downloads include overlays or use
the existing burn-in fallback when needed. Unsupported codecs, including paired
IDX/SUB sidecars, report an explicit error; they are not mistaken for an embedded
track. Existing PSP track-display limits still apply. No PSP executable update is
required for these server features; Docker and HA ship the same implementation.

Protocol references: [UPnP ContentDirectory](https://upnp.org/specs/av/UPnP-av-ContentDirectory-v2-Service.pdf),
[Plex server API](https://developer.plex.tv/pms/),
[Jellyfin video API](https://typescript-sdk.jellyfin.org/functions/generated-client.VideoApiFp.html).

### Settings survive updates (server 0.1.50)

Home Assistant now keeps Plex/Jellyfin tokens, source switches, path mappings
and the server identity in its persistent `/data` directory, alongside radio
stations and `/data/downloads` (conversion queue, files and saved track/quality
choices). Its password, port and TLS options remain managed by Home Assistant.
Ordinary updates preserve these files; uninstalling the app/data does not.

Docker Compose already mounts the named `streamer-settings` volume at `/data`.
Keep that same volume when recreating/updating containers; do not use
`docker compose down -v` if you want to retain settings. With `docker run`,
explicitly reuse `-v streamer-settings:/data` rather than a fresh anonymous
volume. Back up `/data` securely: it contains media-server access tokens.

Standalone installations use `PSP_STREAMER_SETTINGS_DIR`, otherwise
`PSP_STREAMER_STATE_DIR`, otherwise the existing `~/.cache/psp-streamer` path.
`PSP_STREAMER_STATE_DIR` selects data storage without enabling the WebUI password
editor; this is how HA retains control of its password. The download/radio
directory overrides still apply. Browser language/cover-view choices stay in
browser storage; deployment settings stay in Compose/environment or HA options.

HA copies any surviving legacy Plex/Jellyfin/radio settings and server ID from
its old container cache at startup, without overwriting existing `/data` files.
An update may already have discarded that old cache: if credentials are missing,
sign in once after updating to 0.1.50. Previously deleted credentials cannot be
recovered automatically. Later updates retain the newly saved connections.

### Covers and backgrounds (server 0.1.46)

The web library's **Cover view** button switches between the compact list and
a poster grid; the choice is remembered in that browser. Plex and Jellyfin supply
episode thumbnails, movie/series posters, album covers and series backgrounds.
Selecting an item shows its image and background in the remote-control view.
Missing images do not prevent browsing or playback. File shares and radio keep
their text view unless artwork is supplied by a media-server source.

**Preview theme song** requests the provider's theme music (including inherited
series themes) and plays it in the browser only. This is optional: not every
series has theme media. It does not extract an intro from the episode, transcode
the preview or start playback on the PSP. Browser-supported audio formats up to
16 MiB are accepted; theme videos are not implemented.

With the separate **PSPStreamerHA integration 0.1.1**, current-media artwork also
appears on Home Assistant's media-player entity. Update both server and
integration, then reload the integration. Images remain password-protected;
neither Plex/Jellyfin tokens nor the PSPStreamer password appear in browser URLs.
The server keeps a bounded image cache (24 MiB per provider) and loads grid
thumbnails lazily.

### PSP menu artwork (server 0.1.47)

With an updated PSP application, Plex/Jellyfin artwork also appears on LCD and
native TV menus. Pause the selection briefly: the menu requests images in the
background, dims the backdrop behind the main list/options and contains the
cover in the right-hand display. Episode menus use series artwork; songs use
their album. Text remains outside the cover. The information page shows a cover
only if there is enough space below its track list.

No setting or extra image files on the Memory Stick are needed. The server
prepares a bounded RGB565 packet with FFmpeg; the PSP does not initialize any
JPEG/MPEG decoder for it. Full artwork is about 130 KiB, released before media
playback. Artwork shares the cancellable idle-browser worker, waits 500 ms after
selection changes and never runs concurrently with playback. Missing images or
an older server leave the normal skin intact. Square/Refresh retries a failed
load. There is no artwork for ordinary SMB/local-file entries yet.

Image requests get a dedicated turn after a control reply, even if a slow TV
redraw overruns the usual idle polling interval. LCD backdrop bounds are
`x=36, y=29, width=312, height=123`; the cover remains separate from status text.
TV backdrops fit the inner glass at `x=27, y=61, width=506, height=231`, with
the skin's rounded corners preserved. While an image is downloading, automatic full-TV
redraws pause (navigation still redraws). Artwork alone has a cancellable
10-second request budget, retries failures after 15 seconds and empty image
responses after 60 seconds; it no longer permanently gives up after two tries.
Changing selection cancels the obsolete image request. Streaming timeouts are
unchanged.

With **server 0.1.48 and the matching PSP build**, selecting another episode of
the same series (or song from the same album) reuses the last image after a
68-byte server confirmation. The previous image stays hidden until confirmed;
only one image is retained, and playback releases it. Different accounts and
changed image tags do not share identities. Older server/client versions still
use full image packets. Cold requests share bounded image-conversion jobs.

Idle TV library updates redraw only the receiver controls, not the complete
menu/background. Full redraws present once, including connection notices.
The server also reuses successful local-file metadata/subtitle-codec probes with
file-change detection and bounded caches; remote/live probes are not retained.
Docker and the Home Assistant app include the same changes. No configuration
changes are required. See [optimization status](../docs/OPTIMIZATION_NEXT_STEPS.md)
for the implementation review and remaining measurement-dependent work.

PSP Streamer makes a local or DynDNS-reachable media library available on a PSP-2000/3000 with custom firmware. The Python server browses allowed folders and transcodes with FFmpeg. Video is delivered in one FLV stream containing H.264 and MP3 audio, both decoded locally by the PSP.

### Plex library and playlists (server 0.1.37)

Directory navigation uses a background request on the PSP. While loading,
the status shows elapsed seconds; **Circle** requests cancellation. Failed or
cancelled loads keep the previous directory. Network transfers have a separate
30-second directory budget, independent of playback. Pending
remote-control requests are cancelled cooperatively without an unbounded UI
thread join. A worker that has not yet relinquished its resources is not killed
or replaced: the UI displays the stopping state until safe cleanup is possible.

Before playback, metadata and subtitle preparation also run in a worker with
an elapsed-time display and **Circle** to cancel. The menu stays on its current
LCD/TV output until preparation finishes; no decoder or playback clock is
started during that wait. Cold Plex subtitle extraction may scan the original
over the network and take considerably longer than a cached request. Preparation
has separate total budgets (metadata 60 s, text/type query 210 s, bitmap extraction
630 s), allowing the server's extraction deadlines without changing live stream
timeouts. TV bitmap burn-in skips the unused PSP sprite extraction entirely.

Update **both the server and PSP application**. Docker and the Home Assistant app
provide the same integration. No Plex password or token belongs in the PSP CFG.

1. Open **Settings → Plex** in the server web interface. Choose **Link Plex
   account**, then **Open Plex sign-in**. Authorize PSPStreamer on Plex's own
   website. The local page detects completion; the link expires after five minutes.
2. Select a discovered server connection and press **Use connection**. Prefer a
   reachable HTTPS connection; an HTTP LAN connection also works, but does not
   encrypt the token on that LAN. TLS verification is never disabled.
3. **Plex library** is enabled automatically after the connection is verified.
   Optionally disable **Filesystem library** and/or **Internet radio**, then
   **Save sources**. Refresh the PSP library with Square.
   Enter **Plex** to browse films, series/seasons, music/artists/albums or playlists.

**No path mapping or SMB mount is required.** PSP Streamer reads the original
file directly from the selected Plex HTTP(S) endpoint. Plex and PSP Streamer
can be on different networks, provided that endpoint is reachable and the Plex
account is allowed to read the original. Use a reachable public/remote HTTPS
connection from the server list when the LAN address is not accessible.
For a Plex/radio-only installation, `MEDIA_ROOTS` may be empty. Unavailable
filesystem roots produce an empty file library, not a server startup failure.

Plex does not transcode: FFmpeg on PSP Streamer keeps the established H.264/MP3
encoding and LCD/TV profiles. A private loopback bridge forwards byte-range
requests, so FFprobe and FFmpeg can seek without downloading the entire file
first. The Plex token remains in upstream HTTP headers in the server process;
it is not sent to the PSP/browser or placed in FFmpeg's command line. Neither
the extra ephemeral port nor a new Docker port mapping needs to be exposed.
TLS certificate verification and redirect blocking also apply to original files.

Audio/video inspection, embedded text subtitle extraction, PGS extraction and
offline conversion all support this direct path. Remote PGS uses lossless
FFmpeg stream-copy instead of mkvextract; local files keep their existing path.
Subtitle extraction may need to read much of the original, so a slow internet
link can still increase startup time. The Plex uplink must carry the **original
bitrate**, not merely the smaller PSP output bitrate.

If the same media is already mounted, **Optional: use existing local media
mounts** permits a path mapping to avoid the HTTP transfer. For example,
Plex `/volume1/video` → container `/media/video` (inside `MEDIA_ROOTS`). Missing
mapped files automatically fall back to Plex HTTP(S). Multipart originals are
reported as unsupported rather than silently playing only the first part; for
multiple versions, a version folder allows choosing the original explicitly.
Embedded tracks and supported external sidecars are selectable (see 0.1.51 above).

- Large item lists have 100-entry pages. Playlist order is preserved; automatic
  next/previous playback can cross item-page boundaries. Changed playlists stop
  advancement rather than guessing the next item. Music shuffle stays within
  the current Plex collection/playlist.
- The web information area shows title, series/artist, season/album, year,
  description, watched state and a **Resume at …** button. This button positions
  the seek slider; press Play to start. PSP Triangle information displays Plex
  title, series/artist and season/album. Poster rendering and an automatic resume
  prompt on the PSP are not included in this first version.
- During **online playback**, the updated PSP reports its actual played position
  in milliseconds and pause/stop state. A bounded background worker forwards
  Plex timeline updates at most every five seconds, plus state changes. The
  server never infers watched status from transcoding or download completion;
  Plex applies its own watched/resume rules. Reporting errors are shown under
  Media sources, without interrupting playback. Final Stop reporting is
  best-effort if the network fails. Offline playback progress is not synced yet.
- Credentials are stored only in `plex.json` in the settings directory described above
  (`/data` in the containers; otherwise `~/.cache/psp-streamer`). The file has
  owner-only permissions. Back it up securely; do not commit it. Disconnect
  removes local credentials and restores the filesystem library. Revoke the
  device in your Plex account as well if you want to invalidate its authorization.

Protocol references: [Plex authentication flow](https://forums.plex.tv/t/authenticating-with-plex/609370)
and [Plex Media Server API](https://developer.plex.tv/pms/). The adapter is an
independent implementation; no Tautulli source was copied.

### Jellyfin (server/HA app 0.1.40)

Update both the server and PSP client. Open **Settings → Jellyfin**, enter your
server's HTTP(S) address, username and password, then **Connect Jellyfin**.
Connection enables the source; **Save sources** independently enables/disables
Files, Plex, Jellyfin and Radio. Refresh the PSP library with Square.
Use a dedicated Jellyfin user with access to the desired libraries and permission
to download originals. Use HTTPS for credentials outside a trusted local network.

Movies, series/seasons/episodes, music and playlists use the existing PSP browser.
Lists are paginated; next/previous and audio shuffle use the source collection
or playlist, including page boundaries. Web metadata includes description,
watched status and a resume-position button. Actual online playback reports
start/progress/pause/stop to Jellyfin in its native ticks; downloading a file
alone does not mark it watched. Reporting runs outside playback/UI threads.

Jellyfin supplies the original file over authenticated, range-capable HTTP(S).
Only PSPStreamer transcodes it; no shared media mount is necessary. Embedded
audio, text/PGS subtitle selection and offline conversion reuse the existing
pipeline. Version folders and external text/single-file PGS sidecars are supported
since 0.1.51. Multipart files, Jellyfin-device remote control and offline progress
synchronization remain unsupported. Original-file bandwidth between Jellyfin and
PSPStreamer still applies. Artwork is supported as described above.
Embedded text subtitles are requested through Jellyfin's subtitle-export API
where its media-source mapping is available, avoiding a complete video transfer
just to extract text. With server 0.1.41 and the matching PSP client, text cues
are loaded in 256-entry pages in the background, including for Plex and files
on mounted SMB shares. There is no whole-episode cue-count cutoff. Seeking
loads the page matching the target timestamp. Identical neighbouring ASS cues
are merged; bitmap subtitle handling is unchanged. Older clients retain the
legacy response limit. Offline downloads still use whole-file overlays; new
conversions exceeding 960 normalized text intervals use burn-in instead of
silently truncating subtitles. Previously converted files need reconversion.

Subtitle choices show format and available Forced/SDH (hearing-impaired)/Default
flags alongside the title. Same-language alternatives are numbered. `smaller`
marks the smallest track only when all alternatives of that language have known
byte sizes and the same format. This does **not** identify its content as Forced
or signs/songs. No extra scan is performed to obtain missing statistics. The
labels apply to mounted files, Plex and Jellyfin on both PSP and web. After an
update, reselect a saved web preference if multiple tracks make it ambiguous.

The password is used only to obtain a user token, not saved. Tokens and device
identity live in owner-only `jellyfin.json` in the settings directory described above
(`/data` in Docker/Home Assistant). Disconnect removes the local token; revoke
the device in Jellyfin as well to invalidate it upstream. Tokens never enter
PSP configuration, public API replies or FFmpeg arguments. Redirects carrying
credentials are blocked, and HTTPS certificates are verified.

The ordinary Docker image and Home Assistant app contain identical adapters.
Reference: [official Jellyfin API client](https://github.com/jellyfin/jellyfin-apiclient-python).

### Internet radio and music tags (server 0.1.33)

Update the server **and the PSP build**. In the web UI, open **Settings → Internet Radio**,
enter a name and a direct HTTP(S) audio URL, then save.
Stations appear in the PSP's **Internet radio / Internetradio** library folder;
press Square to refresh. Select a station, choose audio quality and start it.
The web remote can select/play stations and replace music/video playback too.

- Direct MP3, AAC, Ogg/Opus/Vorbis, FLAC and WAV inputs use the existing
  44.1-kHz stereo MP3 output and quality settings. Existing file encoding is
  unchanged. Simple URL-based `.m3u` and `.pls` lists resolve their first entry
  (up to 64 KiB, bounded nesting). HLS/M3U8 and HTML-page scraping are excluded.
- **Select** pauses by disconnecting. Select/X resumes at the live programme;
  **Start** stops. The web Pause/Resume/Stop buttons do the same. Radio has no
  seek, download, shuffle or automatic next-station playback. Spectrum and
  MilkDrop work as for music; fullscreen visualizations stay overlay-free.
- Interrupted radio playback shows a retry screen and reconnects after five
  seconds; Square/X can retry sooner, Start/Circle cancels. If Wi-Fi association
  itself fails, return to the library and use its existing Square reconnect.
  Silent senders are detected after 30 seconds without audio progress, not
  mistaken for the end of a song. No file/video timeouts were shortened.
- ICY/Shoutcast `StreamTitle` and `icy-name` are requested on the actual audio
  connection. Titles appear in the web remote and normal PSP music view.
  Missing metadata falls back to the saved station name. UTF-8 and legacy
  Windows-1252 text are handled; titles can lead audible playback by the small
  transcode/audio buffer. No exact ICY-title timestamp synchronization is claimed.
- Music files now display **title and artist** from ID3/container tags, falling
  back to the filename. The file-information screen (Triangle) also shows the
  album. MP3 ID3 and equivalent FFprobe-readable FLAC/Ogg/M4A tags are supported;
  embedded artwork and tag editing are not included.

Stations persist in `radio.json`: `/data/radio.json` in Docker and Home Assistant.
For standalone Python, use `PSP_STREAMER_RADIO_DIR`, otherwise
`PSP_STREAMER_SETTINGS_DIR`, otherwise `~/.cache/psp-streamer`. Keep the Docker
`/data` volume mounted. HA 0.1.33 and ordinary Docker ship identical server/UI
features. Sender edits do not interrupt an already playing connection; restart
that sender to apply its changed URL.

Radio management is protected by the existing server password and same-origin
JSON rules. Only configure trusted URLs: fetching stations intentionally permits
access to LAN senders, including HA-NetMD. Do not expose an unprotected server.
URLs stay on the server; PSP clients receive opaque station IDs. Basic auth
credentials inside sender URLs are not supported. Stream redirects are limited
to FFmpeg's HTTP(S) transport; local-file demux/protocol inputs are disabled.
The ICY adapter consumes the metadata events from
[FFmpeg's HTTP implementation](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/http.c),
without opening an additional receiver connection or logging arbitrary sender output.

### Software encoding / Main with CABAC (0.1.29)

All deployments use software `libx264` again; VA-API/NVENC support and its
configuration have been removed following PSP decoder failures. Encoding
uses H.264 **Main, level 3.0, CABAC enabled**, with B-frames
and weighted P prediction disabled. Resolution, selected frame rate, bitrates,
MP3 audio and container timestamp synchronization are unchanged. Docker and the
Home Assistant app use identical encoder commands. Update/rebuild the server;
this change does not require a new PSP executable. Main/CABAC subsequently
passed the user's real PSP playback test. Successful FFmpeg decoding alone
is not proof of PSP compatibility.

## Offline downloads (server/app 0.1.30 and matching PSP client)

Choose **Streaming** or **Download, then play** in the PSP video options.
The latter queues a complete file from its beginning, waits for conversion,
downloads and verifies it, then starts the existing FLV/PTS player from the
Memory Stick. It is not a general local-file browser.

**Music downloads (server/app 0.1.34 and matching PSP client):** music options
also offer **Streaming / Download, then play**. The server converts music files
to stereo 44.1-kHz MP3 at the selected CBR/VBR quality. Download progress,
resuming interrupted transfers, SHA256 checks and PC/USB ZIP export work just
like video. Radio/live streams cannot be downloaded this way.

Open downloaded songs in **Local storage**. Playback needs no server or WLAN,
retains title/artist metadata and uses the normal music decoder, volume, pause,
Spectrum/MilkDrop and fullscreen controls. Select pauses/resumes, Start stops.
There is no local-music seek control in this batch. End-of-track advance stays
within music entries; Shuffle visits each ready local song at most once per
browser playback run. Videos are not mixed into that music sequence.

The web **Prepare MP3 download** button converts music, without video-output
or subtitle options. For an album, queue songs on the web and use **R** in the
PSP server queue to transfer pending/ready entries. Starting a single download
directly from the music options plays that song; use the Local storage browser
for automatic multi-song playback. Keep the entire managed job folder even
for MP3: metadata/ready and tiny empty sidecars retain the existing bundle format.

For several episodes, use the Library checkboxes and **Convert selected**, or
**Convert this folder**. Subfolders are included only when explicitly enabled.
In Downloads, choose audio/subtitle language, quality, frame rate and output,
then **Check tracks**. The preview resolves language and title separately in
every file, rather than copying a stream index across episodes. Missing or
ambiguous tracks are shown and excluded; review the results, then **Queue
checked files**. At most 128 files and 256 visited folders are allowed per
batch, including Plex pages. All-music batches show only audio quality.
Single files still use **Convert for download** in the web remote. Each
job retains its own audio track, subtitle track, audio quality, frame rate and
LCD/TV profile. Conversions run sequentially, without real-time throttling;
the queue survives server restarts. A single conversion shares the server's
existing transcode limit with streaming. The web list reports conversion
state, percentage and produced size; cancel/delete controls affect only
server jobs/copies, not downloaded PSP files.

The web UI remembers audio/subtitle labels and languages, quality, frame rate
and output profile on the server across restarts. Tracks match by label first,
then language; a missing preferred track produces a warning to check the
fallback selection. Subtitle Off is remembered too. Queued jobs retain their
own settings; PSP config defaults are unchanged. FFmpeg/libx264 chooses its
thread count automatically, with no single-core limit imposed by the queue.

### Recommended: PC/USB transfer (server 0.1.31)

For a finished conversion, click **Download Memory Stick ZIP** (PC / USB is recommended).
Download and extract it on your PC, close PSP Streamer, then merge the contained
**PSP** folder into the Memory Stick root (not into another PSP folder).
Safely eject and open **Local storage**. The existing 0.1.30 PSP client needs
no update. This works with HA and Docker without SSH or internal filesystem access.

Copy the entire `PSP/VIDEO/PSPStreamer/<job-id>/` folder: FLV or MP3, `subtitles.ovl`,
`seek.idx`, compact `job.json` and `ready`, not just the video. Multiple ZIPs
can be merged thanks to unique job folders. Download ZIPs onto the PC first:
the combined archive can exceed FAT32's limit even when individual files fit.
The server streams the ZIP without creating another video-sized disk copy.
Interrupted ZIP downloads must be repeated. Direct Wi-Fi remains the fallback.

### Alternative: direct PSP Wi-Fi download

On the PSP, open **Local storage** at the library root, or press **Circle**
in any library folder. **Square** switches between local files and the server
queue. In the queue, **X** downloads the selected job (waiting if it is still
converting); **R** downloads all pending/ready jobs in the displayed list,
one after another. Use Square twice to refresh the queue. During transfer,
the screen shows file bytes/total size, KiB/s and estimated seconds remaining.
Size is final only after conversion. Subtitle/index sidecars follow the video;
the byte counter then refers to the current sidecar. SHA256 verification has
its own status. **Circle** cancels the PSP wait/transfer, keeping both the
server job and partial download. Select it again to resume via HTTP Range.

Local **X** plays a completed download or resumes an incomplete transfer.
**Triangle** offers deletion with confirmation; **Circle** goes back.
Playback supports pause, stop and keyframe-based seek using the existing
controls. Next/previous and end-of-file advance use the local list's filename
order (and stop at an incomplete entry). Local seeking also reads preceding
MP3 packets through the FLV backward links: it warms the decoder silently,
then publishes audio at/after the indexed video keyframe. This supplies the
MP3 bit reservoir without re-encoding existing downloads or changing PTS sync.
Decoder errors are tolerated only for silent lead-in frames whose required
preceding data has not yet been fed. After that, errors still stop playback.
Local playback makes no server or
remote-control requests, including for subtitles. Hold **R while launching**
to skip Wi-Fi association entirely; Square in the main library can connect later.

If Wi-Fi fails at startup or drops while browsing, press **Square** in the main
library to reconnect and reload without restarting the app. **L + Square** forces
a disconnect/reconnect even if the PSP still reports an IP address. Association
waits up to 30 seconds (plus at most 3 seconds to disconnect); **Circle** cancels.
Network modules initialize only once, including after a partially failed startup.
These controls do not change stream/subtitle preparation timeouts.

Files live under
`ms0:/PSP/VIDEO/PSPStreamer/<job-id>/<original-name>.flv`, with a subtitle
package, seek index and job metadata alongside. Unique job directories prevent
collisions between different series/language variants. Filesystem-unsafe
characters are replaced and exceptionally long names shortened. `+` identifies
a completed, verified entry; `~` an incomplete one. Local deletion is limited
to these managed files and never touches unrelated Memory Stick content.
For PC-copied files whose Unicode long name the PSP cannot open, the client
uses the FAT short-name alias inside that job folder. The fallback requires
one unambiguous FLV with the manifest's byte size; files are not renamed.
The original UTF-8 title remains visible. Existing exports need no conversion
or server update for this client-side compatibility fix.

Choose the output profile before converting: LCD (480×272) or TV (720×480).
A downloaded file requires its matching output; connect/disconnect the TV
cable accordingly. The PSP's direct-download option detects the cable.
`FFFFFA87` means this output/profile check failed, not that VLC-compatible
video is corrupt. The local list shows the resolution; the error screen shows
the required output and how to correct it. For TV playback, select the TV
profile on the server before converting; LCD copies cannot be reused as
native 720×480 TV copies (or vice versa).
Text subtitles use the existing offline overlay; supported LCD MKV/PGS cues
and sprites are packaged too. TV bitmap subtitles and unsupported/oversized
bitmap-overlay cases retain the existing server burn-in fallback. No network
fetches remain during playback. Current 960-cue subtitle bounds still apply.

Server 0.1.65 prepares LCD bitmap sprites at 480×272 canvas resolution before
transfer or packaging, preserving palette, timing and the old LCD sampling.
Update both server and PSP client: the matching client prefetches sprites in a
separate worker, never blocking video rendering on a subtitle download. Failed
cues are skipped for that playback rather than retried every frame. TV burn-in
and text subtitles are unchanged; existing offline packages are not rewritten.
See [LCD bitmap recovery](../docs/LCD_BITMAP_RECOVERY.md) for the diagnosis and test.

### Automatic episode reserve and Plex Watchlist (server 0.1.66)

In a video's web detail page, set **Unwatched episodes to keep ready** (1–20)
and select **Reserve this series/folder**. The selected episode is the starting
point and is included if unwatched. Audio/subtitle languages and output quality
are captured from that page. In **Downloads → Episode reserve**, enable the
feature and choose its server storage limit. Rules refresh every two minutes;
**Refresh** requests an earlier check. Plex/Jellyfin watched state includes other
clients; mounted files use PSPStreamer's recorded completed playback.

This keeps converted **server packages** ready, not automatic copies on the
Memory Stick. Use the existing ZIP export or **Local storage → Server queue**
on the PSP. The existing PSP client works unchanged. Missing preferred tracks
pause a rule instead of silently choosing another language. Failed jobs are not
retried forever: correct the cause, remove the failed job and press Refresh.

Only packages marked as automatic reserve are evicted. Manual conversions,
source files and Memory Stick files are never cleaned. Requesting a download
(manifest, file or ZIP), or manually requesting the same conversion, pins its
server package as a manual job, outside the automatic storage budget. Delete
these manual server copies explicitly when no longer needed. Reducing the limit
may leave fewer than N episodes ready; in-progress output can briefly exceed the
limit until the next converter check. The limit is not a filesystem quota.
Disabling the reserve/removing a rule cleans its unpinned packages.

Enable **Settings → Plex → Enable Plex Watchlist**, then select **Provider
library → Plex → Plex Watchlist**. The linked Plex account's films and series
are matched to the selected server by exact Plex GUID, not title guesses. Missing
titles are labelled unavailable in the web UI; available entries also appear
under **Plex → Provider views → Plex Watchlist** on PSP. A series opens its
seasons; no episodes are silently added to the playlist. This is read-only and
does not add/remove items from the Plex account's Watchlist.

Rules (`episode-cache.json`), watched-file history (`comfort.json`) and the
Watchlist switch (`plex.json`) use the persistent server state volume in both
Docker and Home Assistant. Preserve that volume across updates. Offline playback
on the PSP does not yet sync watched state back; mark it watched on the provider
or update the reserve's starting episode. Details: [episode reserve and Watchlist](../docs/EPISODE_RESERVE_WATCHLIST.md).

Free Memory Stick space is checked before each file; individual files above
FAT32's 4 GiB-minus-one-byte limit are rejected. Keep free space on the server:
manual converted copies remain until explicitly deleted. Docker stores the queue in
its persistent `/data/downloads` volume; Home Assistant uses its app's own
`/data/downloads`. Native server default: `~/.cache/psp-streamer/downloads`.
Override with `PSP_STREAMER_DOWNLOAD_DIR` if needed. All offline endpoints use
the same password/HTTPS protection as streaming. The queue is capped at 128
jobs. This release requires real PSP testing of Memory Stick transfers,
offline playback and interruption/resume in addition to host tests.

Video profiles are 480×272 for LCD and 720×480 for component TV output, with 44.1 kHz MP3. The encoder currently produces 20 fps to limit decoder workload; that number is **not a playback clock or an A/V calibration value**. Text subtitles and LCD PGS bitmap subtitles use native PSP overlays. Video and common music formats use the same MP3 DAC path; music playback includes a receiver UI with live stereo VU meters and a real 12-band PCM spectrum display.

## Timestamp-based audio/video synchronization

The idle file browser polls remote commands in a cancellable background task,
so an HTTPS handshake or unavailable server does not block list navigation.
The task is joined before settings, other network requests or playback begin;
media and subtitle timeout budgets are unchanged.

Both LCD and TV playback follow the audio-master approach in [PMPlayer Advance](https://github.com/DavisDev/pmplayer-advance/tree/9ce494d020d3c909ee312bda1932f8d806a2f05d/ppa/mod):

- One FFmpeg process muxes both streams, including their timestamps. The PSP reads FLV tag timestamps and the signed H.264 composition offset to obtain video PTS.
- H.264 stays length-prefixed (AVCC) from the FLV reader to the Media Engine. Each aligned queue packet owns its SPS/PPS configuration and payload; no Annex-B round trip or second video encoding is involved. This applies to streaming and downloaded FLV on LCD and TV; existing downloads remain compatible.
- Decoded PCM buffers retain their first MP3 packet's PTS. The output worker publishes that timestamp immediately before its blocking DAC call, matching PPA's buffer-based audio clock.
- Video is decoded and composited with its subtitles in a RAM staging frame. The finished picture is compared with fresh audio PTS, again at VBlank before copying to the established LCD/TV framebuffer. It waits when ahead and discards late pictures. As in PPA, the tolerance is two video-frame durations; here the duration comes from adjacent packet PTS.
- Codec initialization, decoding and related cache operations share one Media Engine semaphore. Network reads, audio output and display waits remain independent. PCM ownership follows PPA: a successful submission releases the previous buffer; the last buffer is released after the DAC drains. Audio content is never repeated or skipped to synchronize video.
- Subtitle cues and progress use milliseconds. Seek and reconnect start a new container timeline at the requested source position. Video-only files use PTS differences on a monotonic clock.

The old 20.1/20.2-fps playback estimates and artificial start offsets are no longer used by the client. Existing hardware initialization, LCD/TV layouts, MP3 decoding and DAC block sizes remain in place. This is a streaming adaptation of PPA's scheduling, not a replacement of the working hardware drivers. Hardware validation is still required for actual DAC/display latency, rendering load and long-episode playback; host tests cannot establish those.

### Measuring a remaining offset

With **Debug diagnostics enabled** (`debug=1`), the client records a bounded
trace in RAM during video playback and writes it after Stop/end, once playback
threads have joined. Diagnostics are **off by default**, including when an old
CFG has no debug entry. Press SELECT in the library, select Debug diagnostics,
change with left/right and save with START. `debug=0` disables trace sampling,
diagnostic timing/packet snapshots, watchdog threads and diagnostic file writes,
and hides informational decoder/profile/frame/audio-state debug readouts.
The SELECT+L+R component test card is also disabled unless debug is enabled.
Normal errors, subtitle/MilkDrop validation and network recovery stay enabled.
Existing log files are not deleted. The separate optional OC plugin has its own
`report` setting since it runs independently in other apps.

When enabled, trace files are:

- TV: `ms0:/PSP/SYSTEM/PSPStreamer-sync-tv.csv`
- LCD: `ms0:/PSP/SYSTEM/PSPStreamer-sync-lcd.csv`

Each file is overwritten by the next completed playback session using that output (including seek/reconnect restarts). Copy it off the Memory Stick after stopping the test video, before starting another one. Enable `debug=1`; no server update is required for recording. There are no trace file writes during playback. The trace retains up to 4096 samples: the first 32 decisions and subsequently at most one per second, overwriting the oldest rows when full.

Rows contain elapsed wall time, video PTS, submitted audio-block PTS, remaining DAC samples, outstanding PCM blocks, preparation/copy durations, display/drop counters and non-increasing audio-PTS counts. `action` is `0=wait`, `1=display handoff`, `2=drop`. `decode_us` includes ME lock wait, decode and CSC; `prepare_us` also includes overlays; `copy_us` measures transfer to VRAM and the display API call. `dac_rest_samples=-1` means the sample was unavailable or crossed an audio timestamp change.

The audio PTS describes the submitted block, not an exact sample at the speaker. The video handoff is measured on the PSP, not at the TV panel. The trace can expose internal stalls and timestamp discontinuities; it cannot directly measure a television's processing delay. The staging frame uses approximately 544 KiB on LCD or 1.41 MiB on TV, plus 192 KiB for the trace. The measured copy duration helps assess its cost on real hardware.

### Optional MilkDrop phase timings (`debug=1`)

Enable **Debug diagnostics** in settings (or `debug=1` in the configuration).
Play music, select a preset such as Cauldron painterly 3, and leave it running
for 30–60 seconds. Stop playback with START, then copy
`ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt` before starting another song.
No server update is required. This report contains the preset filename.

The `MilkDrop profile` sections separate LCD/TV, window/fullscreen and preset.
They report completed frames, throttled calls and average/maximum microseconds
for setup, frame/shape formulas, pixel formulas, audio snapshot/FFT, custom-wave
evaluation, geometry/command submission and final GPU wait. Custom-wave
evaluation includes its CPU geometry generation. Up to 16 distinct contexts
are retained per music playback; `untracked_calls` reports capacity overflow.
Failed frames and initial texture loading are not included. Disable automatic
preset switching for a clean comparison.

These are wall-clock measurements: thread preemption is included and CPU/GPU
work overlaps. `gpu_wait` measures only the remaining wait after submission,
not the GPU's total execution time. With batched shapes, intermediate GPU waits
are included in `geometry_submit`. Throttled calls are not failed frames;
the existing scheduler deliberately leaves time for audio and controls.
Counters stay in RAM during playback and are appended only at music teardown.
With `debug=0`, profiling takes no timestamps and produces no report.

Direct FFT counters also cover built-in spectrum wave mode 8, whose work is
otherwise inside `geometry_submit`. `fft_prepare`, `fft_transform` and
`fft_magnitude` report time per warm FFT call; `cold_calls`/`cold_total_us`
separate initial window setup. These are nested costs, not additional frame
time; completed FFTs count even when a later frame stage fails. See the
[VFPU assessment and measurement instructions](../docs/VFPU_REVIEW.md).

### Optional playback-stall diagnostics (`debug=1`)

An FLV reader failure (including `FFFFFAD8` / `-1320`) also creates
`ms0:/PSP/SYSTEM/PSPStreamer-stream-error.txt`. This does not wait for the
eight-second stall threshold: the reader captures the failure in RAM, and
the UI saves it after joining that reader, before the remaining teardown.
It records the failing phase, TCP EOF versus socket error versus inactivity
timeout, socket errno/poll events, partial-read sizes, received bytes, last
audio/video tag timestamps, time since received data and APCTL state at failure.
Parser/allocation failures are reported separately from network read failures.
No video bytes, credentials or media filenames are included. A new FLV error
replaces the previous report; healthy playback does not erase it. Save it with
the sync CSV and watchdog file after an unexpected video stop. These changes
do not adjust streaming/startup/subtitle timeouts or implement new recovery.

With diagnostics enabled, the client watches both music and video, including teardown. Before
playback it creates `ms0:/PSP/SYSTEM/PSPStreamer-watch-music.txt` or
`ms0:/PSP/SYSTEM/PSPStreamer-watch-video.txt` and checks that its worker can append
`monitor running`. A failed storage/monitor check stops playback with a
`Diagnostic file` error instead of silently losing diagnostics. Each new playback
replaces that media kind's previous file: copy evidence before starting another.
After eight seconds without a main-loop heartbeat, playback progress or a
successful remote poll, the
monitor records the stage, queues, codec state and remote HTTP phase/counters.
Music progress is counted in DAC blocks, video progress in presented PTS.
At most four reports are written, at least 30 seconds apart. Healthy playback
only writes at startup and shutdown, not on rendering/audio ticks.

After a hang, wait about 10 seconds before leaving via the PS button, then
copy both files. A higher-priority CPU lockup can still prevent a stall report,
but the startup record should already exist. This is diagnosis, not a claim
that every hang is fixed. Decoder, PTS and subtitle preparation timeouts remain
unchanged. Remote commands alone use a cancellable two-second HTTP budget and
the already resolved server address. HA app 0.1.22 adds server-session identity
so restarting the server cannot strand the client's command counter. This is
separate from investigating commands that only execute after a PSP app restart.
HA app 0.1.21 fixes the
web remote incorrectly treating subtitle index 0 as Off; update the app and
reload the browser page to receive that fix.
