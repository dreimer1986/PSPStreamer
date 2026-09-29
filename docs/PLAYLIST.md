# Shared playlist — server 0.1.64 and matching PSP client

The first version provides one ordered, shared queue, with up to 128 unique
media IDs. Files, Plex, Jellyfin and DLNA entries can be mixed, including music
and videos. Internet radio is excluded because live streams have no natural
track end. This list is independent of provider-owned playlists and MilkDrop
preset lists; it does not modify either of them.

## Web controls

- Library: use **Add to playlist**, or check several files and use **Add selected
  to playlist**. Multiple pages/sources can contribute to the same selection.
- Media details: select audio/subtitle tracks and quality, then **Add to playlist** to keep
  those choices with this entry. Library bulk additions default to first audio
  and no subtitles. Adding an existing entry does not duplicate or update it;
  remove it first if its saved tracks need changing.
- Open **Playlist**: Play any entry, move up/down, remove, clear, or select
  previous/next relative to the PSP's reported current item. Play enables list
  order automatically. Removing/clearing only removes references, never files.
- Turn off **Use playlist instead of folder order** for ordinary folder
  continuation. Playlist shuffle is separate from the PSP folder-shuffle setting.
- **Repeat off / one / all**: stop at the end, repeat the current entry after
  natural EOF, or wrap the list. Manual Previous/Next bypass Repeat One.
- **Playlist shuffle** uses a saved, stable permutation: visit every entry once
  per cycle. It does not reorder the displayed list. Turning shuffle off/on
  creates a new order; Repeat All cycles the current order. Modes survive updates.
- Drag the **⠿ handle** onto another entry to move to that position. Up/down
  buttons remain available on touch devices and keyboards. Dragging edits the
  stored order, not the active shuffle permutation. Revision conflicts reload
  rather than silently overwriting another controller's changes.
- **Next** shows the server-resolved successor, including Repeat One, shuffle
  and Play Next overrides. With queue mode off it says Folder order; it does not
  crawl provider folders on every refresh. An offline PSP has no current successor.
- **Total (one pass)** sums full entry durations, not remaining time or repeated
  cycles. Unknown durations are shown separately, never silently counted as zero.
  Only while this page is visible, the browser fetches durations one at a time.
  Plex/Jellyfin use provider metadata; files/DLNA use bounded ffprobe inspection.
  Failed lookups do not block playback; **Retry unknown durations** retries them.
- Every row has **audio quality** (CBR 96/128/160, VBR V6/V5/V4/V3) and, for
  video only, **20 / 23.976 fps**. **PSP default** inherits the settings from before
  this queue session, not the previous row's override. Edits are saved immediately
  and apply on the next start of that item, not halfway through a running stream.
  Library additions inherit defaults; media-detail additions capture its selectors.

## Play next (0.1.63 web action)

Schedule a different item after the currently reported online track without
stopping it. The server enables the shared queue and adds the current item if
needed, atomically with the insertion. Existing entries move rather than
duplicate. A one-shot override bypasses Repeat One; it is consumed only when
the PSP reports the selected item playing, never merely by looking up Next.
Shuffle retains the adjusted order. Subsequent items follow this queue, not
the original folder. Disable queue mode to return to folder continuation.
Live radio and local-only PSP media cannot be anchors. The web library, media
details and playlist expose this action; PSP successor lookup honors it too.

## PSP controls

Open **Playlist** at the library root. **X** opens the usual playback options
and starts from that entry. **Triangle** opens move up/down, remove, and the
list/folder-order switch, repeat and playlist shuffle. **Circle** closes the editor; **Left** returns from
the playlist folder. The illustrated help includes a playlist page.
Adding new media remains a web operation. Native edits use the existing
cancellable request worker; no additional playback polling is introduced.

The saved track choices are loaded initially; playback options on the PSP may
override them for this start. Later entries use their own saved indices.
Saved quality/frame rate apply to both web and native PSP starts and automatic
continuation. PSP playback options may override them for this start only; entry
quality overrides do not change global CFG defaults. Limits and display mode
retain the PSP's current settings. Older clients ignore entry quality fields.
The playlist is not a download queue and does not include local-only stick files.

## Persistence and boundaries

`playlist.json` is stored atomically in the existing server state directory
(`PSP_STREAMER_STATE_DIR`, with the existing legacy settings override). Keep
that volume for Docker updates; the HA add-on uses its persistent data area.
The playlist survives restarts, but a server restart does not itself issue Play.
Removed-current-entry positions are retained, bounded to 128, so the next item
can still be resolved after removal and restart. Errors/unavailable media stop
normally rather than silently skipping files. A new Stop/Play takes precedence
over an automatic transition.

Every edit carries a revision. A stale web editor reloads and asks for a retry;
the PSP reports the failed edit and reloads its list. There is no silent
last-writer-wins replacement. The existing password/authentication applies.

Not yet included: multiple named lists, duplicates, import/export,
or gapless music playback. Repeat modes apply to this playlist,
not ordinary folder playback or Internet radio.

## Verification

Build before focused tests: playlist persistence/conflicts, mixed HTTP queue,
native POST cancellation/body ownership, music/video transition loop, browser
add/reorder/play/reload/remove and subtitle index zero. The user confirmed all
newly delivered functionality working on 2026-09-27, including playlist modes.
