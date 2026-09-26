# Player comparison and flight proposals — 2026-09-26

Discussion only: these proposals are **not approved implementation work**.
The comparison targets classic Winamp for Windows, not its new mobile/cloud
product. Our status includes the new shared playlist, still awaiting a PSP run.

## Classic Winamp comparison

Winamp documents editable/saved playlists, shuffle/repeat, a searchable library,
history/bookmarks, tags/artwork, Internet radio, visualizations, an equalizer,
ReplayGain, gapless/crossfading output, smart views, podcasts, portable-device
transfer, skins and extensions. Some features depend on version or plugins;
old online services listed in its manual are not assumed operational today.
Reference: [official Windows desktop manual](https://support.winamp.com/winamp-desktop-player-for-windows).

| Area | PSPStreamer now | Remaining opportunity |
|---|---|---|
| Playback | Audio/video, seek, pause, next/previous, folder order/music shuffle; LCD and TV | Repeat one/all; explicitly separate queue shuffle from folder shuffle |
| Lists | One persistent mixed queue, web additions, web/PSP move/remove/play; provider playlists | Named lists, duplicate entries, drag reorder, play-next, duration total, import/export |
| Library | Files/SMB mounts, Plex, Jellyfin, DLNA; cross-source web search | Rich artist/album/genre view for plain files; no need to duplicate provider indexes blindly |
| Personal access | Favorites, recent items, resume, playback limits | Music ratings and rule-based lists only if wanted; preset ratings already exist |
| Metadata | Audio tags, ICY titles, covers/backgrounds, provider text and track labels | Optional file-tag editor, but explicitly authorized writes and backups would be essential |
| Visuals | Spectrum, shaderless MilkDrop, live transitions, Monkey and flight Easter egg | No desktop HLSL engine, AVS or Windows DLL loading; heavy presets remain hardware-limited |
| Sound | Volume and selectable server encoding quality | No exposed EQ, ReplayGain policy, gapless or song crossfade |
| Offline | Conversion queue, PSP download, PC/USB export, local playback | Export a playlist with matching local references; not full portable-library synchronization |
| Remote | Web transport, virtual PSP controls/text, HACS player | Already tailored to a PSP at the TV; not a missing desktop window feature |
| Appearance | Dedicated LCD/TV theme, localized help, configurable controls | Not a general Winamp-skin interpreter |

File support is architectural, not a decoder-count contest: the server accepts
common audio/video sources and prepares MP3/H.264 FLV for the PSP. Winamp-style
native FLAC/MOD/MIDI decoding on the handheld is not implied by those inputs.
The measured, timestamp-based A/V path must remain independent of new comforts.

## Recommended order — my assessment

1. **Finish testing the shared queue**, then add repeat off/one/all, a visible
   current/next item, play-next and multiple named lists. Give duplicate entries
   separate entry IDs before allowing the same file twice. Low-to-medium scope,
   negligible steady-state PSP rendering cost.
2. **M3U8/PLS import/export and offline list bundles.** Resolve paths/URLs into
   authorized sources; do not export provider tokens or accept arbitrary local
   paths. Relative paths in a downloaded bundle would be useful on a train.
   Medium scope; playlists from other machines cannot be assumed resolvable.
3. **Optional ReplayGain, then optional EQ on the server.** Honor existing gain
   metadata, distinguish album/track modes, prevent clipping and avoid changing
   playback by default. A server filter is cheaper for the PSP but changing it
   midstream needs a controlled restart; offline copies must be regenerated.
   Keep processed-audio visualization honest. Medium scope, not a quick toggle.
4. **Gapless before crossfade**, only if listening tests make the benefit worth
   it. Prefetch/decoder ownership, encoder delay/padding, pauses and recovery all
   need a design. Server-side mixing is another option but complicates track
   boundaries, metadata and seeking. High scope; no promise of a cheap fix.
5. **Optional podcasts or smarter library views** if there is actual demand.
   Feed fetching, retention and storage quotas are server responsibilities.

I would not prioritize CD ripping/burning, a full skin/plugin ABI, another
general tag database or hardware-incompatible shader emulation. Those add a
large maintenance surface without solving the PSP use case. A sleep-style
playback timer is already present and is not proposed again.

## Plex client comparison

This is a comparison with Plex's personal-library clients across desktop/TV/
mobile, not a promise that every Plex app exposes every function. Plexamp is a
separate music product and is not silently treated as the standard Plex client.

| Area | PSPStreamer now | Useful gap / proposed next step |
|---|---|---|
| Progress | Plex playback reports, provider resume/watched status; own recent/continue lists | Provider-wide Continue Watching, including titles started on another Plex client |
| Browsing | Libraries, seasons, albums, playlists, cover art, backgrounds, descriptions and cross-source search | Collections, recently added and unwatched filters; first on the web, compact PSP rows later |
| Watchlist | Our favorites and new play queue | Optional Plex Watchlist bridge; do not confuse a show-level watchlist with an episode queue |
| Episodes | Automatic next episode, manual transport and seek | Optional next-episode countdown/cancel, especially useful before switching series |
| Markers | Chapter controls and skip buttons from supplied Plex intro/credits markers | Keep provider markers authoritative; do not guess from arbitrary chapter names |
| Tracks/versions | Audio/subtitle selection, text/bitmap handling, alternative media versions | Better per-series preferences could save repetitive choices; not a missing basic decoder |
| Offline | Own PSP encoding/downloads and USB bundles | A bounded “keep next N unwatched episodes” job; only delete managed cache copies, never originals |
| Remote | Our web/HACS controller | Not a registered Plex Companion receiver; optional discovery/control bridge is separate work |

Plex's TV navigation documents Continue Watching, recently added rows and
collection browsing. Those are good UI references for the first two proposals,
not a reason to load a full desktop home screen into PSP RAM.
[TV navigation](https://support.plex.tv/articles/navigating-the-big-screen-apps/).

Plex Watchlist contains films/shows, not individual episodes, and can include
items unavailable on the connected server. Any bridge needs GUID/provider
matching and an explicit unavailable state, not a blind Play button.
[Universal Watchlist](https://support.plex.tv/articles/universal-watchlist/).

Official offline Downloads are app-dependent and require the appropriate Plex
Pass/account permissions; our PSP conversion queue is our own mechanism, not
that client feature. Likewise skip-credit availability depends on Plex's
markers and entitlements. Do not promise either universally.
[Downloads](https://support.plex.tv/articles/downloads-overview/),
[credits](https://support.plex.tv/articles/credits-detection/).

**My priority:** provider-wide Continue Watching → unwatched/recent filters →
collections → optional managed offline-next-episodes. Watchlist comes after
that; provider playback reports must stay authoritative. A shared provider
interface should offer the same convenience for Jellyfin where supported.
These are medium-sized server/UI features, not changes to the stable decoder.

I would not chase Plex's ad-supported catalogue, rentals, live-TV/DVR or all
client-specific casting protocols for this player. Direct Play is also not a
checkbox for arbitrary originals: it requires genuinely compatible media;
the current PSP encoding path remains the safe default.
[Direct Play/Stream](https://support.plex.tv/articles/200250387-streaming-media-direct-play-and-direct-stream/).

## Monkey Easter egg — proposals for approval

Normal Monkey must remain untouched until flight mode is activated. None of
the following collision/combat changes is included in this build.

- **Wall recovery first:** one contact event, a short outward impulse and
  tangential sliding; keep forward progress where geometry permits. Contact
  cooldown prevents a single scrape causing damage every frame. If genuinely
  trapped, use the requested 4–5 second blinking escape phase; preferably end
  it once clear rather than force the entire duration. Never re-enable solid
  collision while still inside a wall. Start visible feedback at impact.
- **Barrel roll:** double-tap L/R within roughly 250 ms, animate a complete roll
  over roughly 0.6–0.8 s; simultaneous L+R retains Easter-egg priority. Preserve
  the existing space/camera convention. A short invulnerability window around
  the committed roll would reward timing without permanent protection from
  button spam; final timing needs your test, not a claim about the SNES game.
- **Enemies:** start with a small, fixed pool of anchored turrets and bullets.
  Flying enemies are not impossible: centerline/waypoint-following drones can
  use the generated tunnel rather than solve arbitrary maze navigation. Leave
  full free-flying AI for later. Spawn only in visible/reachable tunnel space.
- **Damage:** your 20% wall / 10% projectile values are a sensible first tuning
  target, applied once per contact/cooldown. Blink during protection. At zero
  health, offer restart or return to the visualizer; music must never stop.
- **Reproducibility and cost:** fixed object pools, deterministic seeds and
  simple swept collision for projectiles avoid tunneling and unbounded work.
  A short fixed-seed flight test is more useful than random enemy density.

Recommended flight order: recovery → roll/feedback → health → turrets → optional
drones. Keep it an optional Easter egg, not a prerequisite for music playback.
