# PSP Streamer Home Assistant Add-on

![PSP Streamer](logo.png)

Exposes Home Assistant's `/media` directory read-only on port 8091 for the PSP client.

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
