# Home Assistant integration

## Installation

This repository contains both a server app/add-on and a native Home Assistant
custom integration. The integration works with the same server API in the
standalone Python, Docker and Home Assistant app versions. No MQTT broker is needed.

1. Update the server to **0.1.44 or newer** and install the matching PSP application
   (`EBOOT.PBP` and `PSPStreamer.prx` together). Existing PSP configuration is retained.
2. In **HACS → menu → Custom repositories**, add
   `https://github.com/dreimer1986/PSPStreamer` with type **Integration**.
3. Download **PSP Streamer** and restart Home Assistant. The repository revision
   selected in HACS must contain `custom_components/psp_streamer`; an older release
   from before this integration was added will not work.
4. Open **Settings → Devices & services → Add integration → PSP Streamer**.
5. Enter the URL of the **PSP Streamer server**, not Plex, Jellyfin or the HA UI.
   For example, `http://192.168.1.42:8091` or `https://psp.example.org`.
   Enter the server's shared password; leave it blank only for a passwordless server.
6. Start PSP Streamer on the PSP and connect it to that server. The media-player
   entity becomes available after the PSP's next remote-control poll.

Home Assistant **2026.3 or newer** is required. HACS is optional: manually copy
`custom_components/psp_streamer` into HA's `/config/custom_components/` and restart.
This is a **custom repository**, not a submission to the HACS default catalogue.
The GitHub workflow checks HACS packaging and Home Assistant's hassfest requirements.

Use the integration's **Reconfigure** menu to change its URL or password. Enter
the password again when reconfiguring (an empty field means no password).
An authentication failure also offers reauthentication. Reconfiguration must
refer to the same server identity; add a separate integration for another server.
Keep `player-id.txt` in the server's persistent settings directory (`/data` in
Docker/the app). It provides the entity's stable identity.

HTTPS uses Home Assistant's normal certificate verification. Unlike the PSP's
certificate-pinning mechanism, this integration does not accept an untrusted
certificate automatically. Use a publicly trusted certificate or a CA trusted
by Home Assistant. HTTP sends the password without encryption and should only
be used on a trusted network. Credentials belong in the setup form, not the URL.

## Controls and media browser

The `media_player` provides:

- Confirmed playing, paused, buffering and idle state, position and duration.
- Title and available artist/album metadata, including radio title updates.
- Play/resume, pause, stop and seeking in non-live media.
- Previous/next item in the active server folder or provider sequence.
- A media browser for the server's enabled Files, Plex, Jellyfin and Radio sources.

A simple dashboard card (use the entity ID created in your installation):

```yaml
type: media-control
entity: media_player.psp_streamer
```

For automations, `media_player.play_media` accepts a server media ID (as returned
by the media browser/API), not an arbitrary file path or Internet URL:

```yaml
action: media_player.play_media
target:
  entity_id: media_player.psp_streamer
data:
  media_content_type: psp_streamer
  media_content_id: "<server media ID>"
  extra:
    audio: 0
    subtitle: -1
    audio_quality: v5
    start: 0
```

`audio` and `subtitle` are zero-based track indices; `subtitle: -1` disables
subtitles. Optional `audio_quality` and `video_fps` use the existing server API
values; `start` is in seconds. Media-browser play and previous/next use the
server's default play options, not an HA-specific saved language selection.
For track selection use the server web UI or pass explicit `extra` options.
The PSP's existing automatic continuation remains independent of HA.

## State reporting and limits

HA polls authenticated `GET /api/player` every two seconds. This is a read-only
snapshot: it neither consumes remote commands nor keeps an absent PSP online.
The PSP publishes its playback state through the existing remote-control worker;
there is no extra PSP polling thread and no change to its A/V synchronization.
Commands are sent to `/api/remote/command`; a queued command is **not** treated as
confirmed playback. Network, buffering and decoder startup can delay confirmation.
As with the web remote, only the newest command is retained and an uncollected
command expires after 15 seconds.

One integration entry represents **one server and its current PSP**. Multiple
simultaneous PSPs on the same server are not distinguished. After 45 seconds
without a PSP report, the entity becomes unavailable. Closing the app, losing
Wi-Fi or a blocked worker can therefore take up to that long to show as offline.
The server itself can be configured while the PSP is offline.

This first version does not remotely power on the PSP, set its volume, manage
downloads, browse its Memory Stick or control local/offline files. Live radio
cannot seek; pause/resume uses the existing radio stop/reconnect behavior.
Missing metadata is left empty rather than probing a media provider from the
playback worker. A newly started server may need the file to be opened again
before its in-memory metadata cache has a title.

## Focused developer checks

Server and PSP-worker tests use the repository's normal test environment:

```bash
python3 -m unittest tests.test_player_status tests.test_music_remote -v
```

Integration checks require a **separate environment with Home Assistant** (not
the PSP Streamer server container). They exercise the actual HA classes and a
local authenticated test server without contacting your installation:

```bash
python -m unittest tests.ha_integration_checks -v
```

Installation through HACS and playback on a real PSP remain hardware/user tests.
