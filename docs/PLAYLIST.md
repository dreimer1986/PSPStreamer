# Shared playlist — server 0.1.60 and matching PSP client

The first version provides one ordered, shared queue, with up to 128 unique
media IDs. Files, Plex, Jellyfin and DLNA entries can be mixed, including music
and videos. Internet radio is excluded because live streams have no natural
track end. This list is independent of provider-owned playlists and MilkDrop
preset lists; it does not modify either of them.

## Web controls

- Library: use **Add to playlist**, or check several files and use **Add selected
  to playlist**. Multiple pages/sources can contribute to the same selection.
- Media details: select audio/subtitle tracks, then **Add to playlist** to keep
  those indices with this entry. Library bulk additions default to first audio
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

## PSP controls

Open **Playlist** at the library root. **X** opens the usual playback options
and starts from that entry. **Triangle** opens move up/down, remove, and the
list/folder-order switch, repeat and playlist shuffle. **Circle** closes the editor; **Left** returns from
the playlist folder. The illustrated help includes a playlist page.
Adding new media remains a web operation. Native edits use the existing
cancellable request worker; no additional playback polling is introduced.

The saved track choices are loaded initially; playback options on the PSP may
override them for this start. Later entries use their own saved indices.
Quality, frame rate, limits and display mode retain the PSP's current settings.
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

Not yet included: multiple named lists, duplicates, drag-and-drop, import/export,
per-entry quality, or gapless music playback. Repeat modes apply to this playlist,
not ordinary folder playback or Internet radio.

## Verification

Build before focused tests: playlist persistence/conflicts, mixed HTTP queue,
native POST cancellation/body ownership, music/video transition loop, browser
add/reorder/play/reload/remove and subtitle index zero. Final confirmation on
real PSP hardware remains necessary, especially mixed media and TV output.
