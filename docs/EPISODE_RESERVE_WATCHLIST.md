# Episode reserve and Plex Watchlist — server 0.1.66

## Reserve ownership and limits

Disabled by default. Up to 16 rules, each 1–20 episodes, share a configurable
128–1048576 MiB **server** cache budget and the existing 128-job offline queue.
Choose the starting episode and track/quality options on the media detail page.
One rule per series (Plex/Jellyfin) or mounted file folder replaces an older rule.
Subsequent episodes resolve language/title preferences against their own tracks;
missing languages stop that rule. Normal frame rate/profile conversion applies.

The background worker checks at startup, after edits/Refresh, and every 120 s.
Plex/Jellyfin queries read fresh watched flags, continue through later seasons
and reject an incomplete series scan above 10000 entries. Files follow the
existing natural folder order and durable completion records, retained separately
from the bounded recent-history list. Unknown files count as unwatched. The
starting episode is included if unwatched; earlier episodes are never backfilled.
No update changes provider watched state or automatically issues Play.

Only jobs created by the worker acquire a private `cache_owner` marker. Manual
jobs are reused but never adopted. A manual request for an existing reserve job
or a requested download pins it permanently, under the queue lock. A stale
cleanup snapshot rechecks ownership before cancelling/removing anything. Thus
manifest/file requests and multi-file/resumable transfers cannot lose a package
to automatic eviction. Pinned/manual packages are outside the automatic limit.
Sources and PSP files are never cleanup targets. Symlink job directories are
rejected. Cleanup is limited to validated UUID job directories and generated
files, retaining error manifests after an oversized conversion.

Budget checks run before subprocesses, during capture/progress, and before
publishing a package. Brief overshoot is possible between checks; this is not an
OS disk quota. Newest/farthest owned packages are dropped first after reducing
the budget. Too-small budgets and failed conversions stay visible rather than
causing an endless encode/evict loop. Remove the failed job after correcting the
cause and use Refresh, or replace the rule. Disabling/removing rules cancels and
cleans only their unpinned jobs; converter shutdown is allowed to finish first.

State lives in the existing persistent state/download volumes. Provider/account
changes invalidate a rule instead of resolving old IDs on a new server. Offline
PSP watched-state upload is not implemented. This feature prepares packages for
the existing manual ZIP or PSP download paths, not unattended PSP synchronization.

## Watchlist

Explicit opt-in under Plex settings; requires the linked cloud account plus a
selected/enabled Plex server. The cloud account token is used only at the fixed
HTTPS Discover origin, with redirects refused. Local mapping uses the selected
server's credential and exact Plex GUID/type, with no fuzzy title matching.
Cloud rating keys never become playback IDs. Multiple local matches remain
selectable. Missing entries are explicit in the web UI; PSP compact shelves show
available media only. Library artwork and media-version selection are reused.

Eight cloud entries per page, at most four simultaneous bounded mapping calls.
Mapping failures are errors, not proof that a title is unavailable. There is no
background Watchlist poll, persistent mapping executor or Watchlist mutation.
Show-level entries open seasons, never replace/append the episode play queue.

API reference: [python-plexapi MyPlexAccount.watchlist](https://github.com/pkkid/python-plexapi/blob/master/plexapi/myplex.py)
and [Library.search](https://github.com/pkkid/python-plexapi/blob/master/plexapi/library.py).

## Focused verification

`tests/test_episode_cache.py` covers ownership/pinning, restarts, symlinks,
obsolete package cleanup, budget retention, per-episode track matching, durable
file completion, fresh Plex/Jellyfin flags, and a real FFmpeg/HTTP reserve-fill,
completion, refill and download-pin cycle. `tests/test_watchlist.py` covers
exact GUID mapping, unavailable titles, paging, PSP paths and opt-in persistence.
Existing offline reuse, provider shelf, Plex and comfort endpoint tests also run.
The targeted set passed 37 checks (one unrelated opt-in browser test skipped).
The separate Chromium check passed reserve editing/saving, Watchlist unavailable
rows and provider switching. A stale Plex-root expectation was updated to include
the already-existing Playlist folder; no catalogue behavior changed for that.
No PSP build is needed: neither client nor firmware changed. Live Plex account
Watchlist contents and the user's series need user validation after deployment.
