# PSP Streamer Home Assistant Add-on

![PSP Streamer](logo.png)

Exposes Home Assistant's `/media` directory read-only on port 8091 for the PSP client.

Version **0.1.50** stores Plex/Jellyfin connections, source settings and server
identity in persistent `/data`, just like radio and the download queue. Updates
keep this data; uninstalling/removing app data does not. Password/port/TLS options
remain controlled by HA. Back up app data securely because it contains tokens.
Surviving legacy cache files are copied without overwriting saved settings.
If an earlier update already removed the cache, connect to the media servers
once again after installing 0.1.50; subsequent updates preserve the connections.

Version **0.1.40** adds Jellyfin alongside Files, Plex and Radio. In the web UI,
open **Settings → Jellyfin**, enter the server address and user credentials,
then connect. Source switches remain independent; refresh the PSP library
after installing the matching client build. The token is stored privately in
`/data/jellyfin.json`; the password is not saved. No extra HA options, shared
mounts or exposed ports are required. Originals are transcoded by PSPStreamer,
with embedded subtitles, offline conversion and online playback reporting.
See the main README for limitations and recommended dedicated-user permissions.

Version **0.1.33** adds Internet radio management in the web UI, a radio folder
on the PSP, ICY sender/current-title display and ID3 music tags. Install the
matching PSP build. Station URLs persist in `/data/radio.json`; no additional
app options are required. Pause disconnects radio, Resume rejoins live. Ordinary
Docker has identical features. Use trusted sender URLs and the server password.

Version **0.1.32** removes the obsolete synthetic A/V calibration streams and
hardens media root validation. Ordinary media conversion/playback is unchanged.
The PSP debug switch is a client setting, not an app/server option.

Version **0.1.31** adds Memory Stick ZIP exports for completed conversions.
Download/extract on a PC, then merge the PSP folder into the Memory Stick root
with PSP Streamer closed. Safely eject and select Local storage. No SSH is
needed; the existing 0.1.30 PSP client remains compatible. Web track/quality
and output defaults now persist on the server. Docker has the same features.

Version **0.1.30** adds the offline conversion queue and requires the matching
PSP client for downloads/local playback. Converted files and queue state use
the app's persistent `/data/downloads` directory. The media mount stays
read-only. Manage server copies in the web UI; deleting one does not remove
copies already downloaded to the PSP. The ordinary Docker deployment has the
same queue and protocol. See the main README's Offline downloads section.

Version **0.1.29** returns to software-only libx264 encoding and tests H.264
Main with CABAC (no B-frames or weighted P prediction). Hardware encoder options
from 0.1.27/0.1.28 no longer apply. Update/rebuild and restart the app; keep the
existing PSP executable. Audio and timestamp synchronization are unchanged.
Actual PSP compatibility must still be tested on LCD and TV output.

The app includes a square `icon.png` for Home Assistant and a `logo.png`
reusing the original PSP application artwork. Both files live beside
`config.yaml` and are discovered automatically; no configuration option is
needed. After these files are published, refresh the app store repositories
and reload the browser if the previous artwork is still cached. This artwork
change does not require rebuilding the server or updating the PSP client.

Mount or place the library under Home Assistant's media directory. SMB credentials stay outside this add-on.

Options: `port` (default 8091) and `max_transcodes` (default 4). Video uses one FFmpeg process containing both H.264 and MP3 with shared FLV timestamps; music uses one MP3 process. The add-on includes MKVToolNix for native PGS subtitle extraction.

Since 0.1.24, `password` sets one shared password (empty disables authentication).
Put the same value in `server_password=` in the PSP config. Browser login uses
the fixed username `psp` and that password; no accounts need to be created.
Restart the app after changing its password. Use a long random single-line
password, at most 128 UTF-8 bytes for PSP compatibility.

HTTP Basic does not encrypt credentials. Version 0.1.26 optionally serves HTTPS:
set `tls_cert` and `tls_key` to PEM files in the read-only `/ssl` mount (for
example `/ssl/fullchain.pem` and `/ssl/privkey.pem`). Leave both empty for HTTP.
Enable HTTPS on the matching PSP client, keeping the configured port. Restart
after changing the certificate. The PSP automatically accepts replacement
certificates and displays a notice; it does not enforce CA/hostname/expiry
trust. See the main README's Security section before public exposure.

The WebUI's Server settings reports that HA manages the password here. Change
it in HA options; the persistent WebUI password editor is for ordinary Docker.

Update the add-on to **0.1.20 or later for the current PSP client**. Version 0.1.19 introduced the FLV endpoint; 0.1.20 adds same-folder successor lookup so playback started through the web remote can continue to the next episode or music track. Install the matching PSP client as well. Restart the add-on after updating. No media mount or configuration changes are needed; older PSP clients can still use the legacy endpoints.
# Version 0.1.25: stream options

Update the PSP client together with this app to use MP3 VBR V6/V5/V4/V3 and
optional 23.976 fps video. Both settings are available in the browser remote
and the PSP playback dialog. Existing CBR and 20 fps defaults are preserved.
The browser remembers its choices locally and includes them with Play.
No additional Home Assistant app configuration is required for these options.
