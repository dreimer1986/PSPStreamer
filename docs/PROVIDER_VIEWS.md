# Provider views and file navigation — 0.1.60

Open **Provider library** in the web UI and select Plex or Jellyfin. The source
must already be connected and enabled in server settings. These views are
requested on demand, not polled during playback. They use the connected user's
provider state, so playback on another client with that same account is included.

- **Continue watching**: Plex's Continue Watching hub; Jellyfin Resume plus Next
  Up, deduplicated within each page. Opening a resumable entry preselects its
  resume position. Press Play to start it.
- **Recently added**, **Unwatched** and **Collections**: choose a library for
  Plex; Jellyfin also supports all libraries. Unwatched means the provider's
  unplayed flag, not an inference from the PSP's own history.
- Artwork and alternative versions use the existing source handling. Episodes
  started from these shelves continue within their season, not the changing
  feed. Music continues within its album. The shared playlist takes precedence
  when enabled and the current item belongs to it.
- Lists are paginated in batches of 50 per provider endpoint. Jellyfin Continue
  Watching can show up to 100 entries because it combines two endpoints. A new
  Refresh rereads the provider. These new shelves are web-only for now.

In a file's detail/control page, **Open previous file** and **Open next file**
open the adjacent library entry's controls exactly like selecting it in the
library. They never send Play, Stop or Seek, and do not follow playlist shuffle.
Existing playback continues. Folder/season boundaries are retained; no successor
is reported at either end. Internet radio has no adjacent-file buttons.

Provider API references: [Plex client implementation](https://github.com/pushingkarmaorg/python-plexapi/blob/master/plexapi/server.py)
and [Jellyfin API client](https://github.com/jellyfin/jellyfin-apiclient-python/blob/master/jellyfin_apiclient_python/api.py).

## Verification for this release

PSP build completed before tests. Ten focused host tests pass (playlist modes
and persistence, mocked provider queries/context, native autoplay, flight
geometry/fade/roll/recovery, help layout), as does the real Chromium UI test
for queue modes and detail navigation without playback commands. Release PBP,
embedded PRX, loose PRX and ZIP copies match; Docker/HA sources match.
The older combined GU adapter harness does not compile against today's title
API (`md_set_tv_title_bottom` removed; missing GU mock constants); it is not a
passing graphics check. Real PSP LCD/TV rendering and live provider shelves
still require user confirmation. No server was restarted during verification.
