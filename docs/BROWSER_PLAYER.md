# Browser playback (server 0.1.70)

**Server 0.1.69:** Jellyfin embedded text tracks are fetched as a small subtitle
file before burn-in, retaining ASS styles. This avoids a second scan through
the remote episode before playback starts. Seek/resume restores original
subtitle timestamps during filtering. The PSP overlay path is unchanged.

Open a library item, choose **Playback target → This browser**, then **Play in
browser**. No PSP or browser extension is needed. Use the same server login as
the rest of the web interface. Credentials are not placed in media URLs.

- Video: software H.264 Main/AAC in fragmented MP4, up to the existing TV
  canvas of 720×480, preserving the source frame rate. Browser support for
  H.264/AAC is required. This is a first web target, not a full-resolution
  direct-play implementation.
- Music and live radio: MP3. The player stays visible while browsing.
- Select audio and subtitles before pressing Play. Embedded/external text and
  bitmap subtitles use the existing server burn-in path, not PSP overlays.
- The separate slider **below the video** seeks across the original duration
  by starting a new stream at that position. The video's native seek bar only
  knows its current streamed segment. The PSP download frame-rate/profile
  controls do not set browser video quality.
- Pause releases the encoder/connection. Use the **Resume button below the
  player** to reconnect at the saved position. Radio rejoins the live stream.
- Stop or closing the tab disconnects the stream. Failed/disconnected browser
  streams have bounded write/startup waits and bounded encoder cleanup.

Browser playback is local to the tab. It deliberately does **not** claim the
PSP's remote-control queue, media-player status, HA entity or pause lease.
The PSP can continue independently if server capacity permits. The existing
playlist controls are still PSP controls. The browser has its **own** automatic
next checkbox (enabled by default) and Previous/Next buttons. Only clean EOF
advances automatically; errors and closing the tab do not. Language/title
preferences carry to the next file instead of blindly reusing track indices;
saved series preferences take precedence. It does not consume the PSP queue.

Playing progress (every 15 seconds), pause and stop now report to Plex/Jellyfin
and shared history. Provider client/session keys are distinct from the PSP;
ordered reports ignore stale late requests. Closing the tab sends a best-effort
keepalive Stop (power loss/browser termination cannot guarantee delivery).
Reloading the page ends playback.

Sequential video may continue into the next season **of the same series**.
Plex/Jellyfin use season metadata. Files/DLNA require numbered season folder
names (`Season 1`, `Staffel 1`, `S01`, with optional suffix), and only inspect
one level of siblings. Empty seasons are skipped within bounded traversal.
Unrelated directories, music albums, shuffle and explicit provider playlists
are not traversed. This shared resolver also benefits the PSP and Xbox.

Both targets share `MAX_TRANSCODES`. With a limit of one, stop the previous
target before starting the other, or increase capacity deliberately. Browser
seek allows a short grace period for its previous encoder to stop; it never
kills an unrelated PSP encoder. Docker and the Home Assistant server App ship
the same code. The HACS integration continues to describe the PSP, not this tab.

## Checks

`tests/test_browser_player.py` verifies actual FFmpeg output, seek duration,
codec selection, endpoint validation, disconnect cleanup and PSP-state isolation.
With `PLAYWRIGHT_MODULE` set, it also plays actual video/audio in Chromium and
checks pause, resume, seek, stop, reporting, a real season transition and
switching back to PSP control.

```sh
PLAYWRIGHT_MODULE="$PWD/.toolchain/web-test/node_modules/playwright" \
  python3 -m unittest tests.test_browser_player -v
```

Other browsers, long-running remote provider streams and radio need normal
real-world testing; no universal browser/device compatibility is claimed.
