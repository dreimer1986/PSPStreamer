# Shared shortcuts and cross-source search (0.1.56)

## Favorites, history and resume

The web navigation has **Favorites & history**, with **Favorites**, **Recently
played** and **Continue watching** tabs. Stars next to library/search results
and in media details add or remove favorites. Folders are supported too.
Selecting a file opens its normal track/quality options; it does not start it
without confirmation. Resume selects the saved position; press **Play on PSP**.
Plex/Jellyfin's current resume position, including zero, takes precedence when
opening a provider item. Removing history does not delete media or favorites.

New playback history comes from the existing PSP status reports. Positions are
persisted at most once per 30 seconds, plus track changes/stops. No new music or
video polling is needed. The new `comfort.json` sits in the durable settings
directory (`PSP_STREAMER_SETTINGS_DIR`, otherwise `PSP_STREAMER_STATE_DIR`,
otherwise `~/.cache/psp-streamer`). Keep the existing Docker settings volume or
Home Assistant `/data` storage when updating. Server connection profiles and
passwords are not stored in this file.

## Bringing existing PSP shortcuts across

Install both new PSP files and server 0.1.56. Open **Comfort** on the PSP while
idle. The client sends only records for the currently configured server and
merges the reply into its existing backed-up state. Favorites/history edits in
that menu also synchronize. Open Comfort again to collect later browser edits.
There is no continuous sync during playback. An unavailable server leaves local
state usable; retry by reopening Comfort. The request has a ten-second total
budget, not a media timeout change. Old servers simply reject the new endpoint.

The PSP creates `PSP/SYSTEM/PSPStreamer-comfort.id` as its persistent sync identity;
keep it with `PSPStreamer.state` when updating. Each server has its own store.
Local downloaded media, sleep/stop-after timers and server connection profiles
remain device-local. These device functions are still reachable with the web's
virtual PSP controller, but are not separate server preferences.

Server capacity is 512 records; old non-favorites are evicted first. The PSP's
existing 64-record capacity is unchanged (including its local/other-server
records). Sync prioritizes server favorites and newer history; protected local
favorites are never evicted. A larger server list therefore need not fit fully
on the PSP. Names are safely shortened to its existing 127-byte field.

## Search

**Search all sources** is above the library. Enter at least two characters.
The search matches film, series, music and folder names case-insensitively with
Unicode normalization. It uses the same source adapters as browsing, not a
second transcoding path. Only enabled Files/SMB, Plex, Jellyfin, DLNA and radio
sources are traversed. It does not inspect media contents or perform a full-text
search in plot descriptions/ID3 metadata.

Results show their source and can be opened or starred. Each source is scanned
in its own background thread, with partial results available while scanning.
There is one active search per server; repeated reads reuse its results for two
minutes. Each source gets 60 seconds, up to 2,000 catalogue pages and a bounded
pending-folder queue. The combined result limit is 300. Slow/unavailable sources
and exhausted limits are reported explicitly: such a result is **not complete**.
Closing the result panel stops browser polling, not an already running scan.
Large remote libraries may require browsing the relevant source directly.

## Focused verification

Tests cover persisted favorite/history merges, completion and failed starts,
actual native PSP serialization/parsing with protected local state, authenticated
JSON endpoints, the HTTP client's POST body transfer, source search, and actual
browser favorite/search navigation. Hardware test: sync Comfort, change a star
in the browser, reopen Comfort, then verify music playback remains uninterrupted.
