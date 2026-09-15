# PSP Streamer Home Assistant Add-on

Exposes Home Assistant's `/media` directory read-only on port 8091 for the PSP client.

Mount or place the library under Home Assistant's media directory. SMB credentials stay outside this add-on.

Options: `port` (default 8091) and `max_transcodes` (default 4). Video uses one FFmpeg process containing both H.264 and MP3 with shared FLV timestamps; music uses one MP3 process. The add-on includes MKVToolNix for native PGS subtitle extraction.

Since 0.1.24, `password` sets one shared password (empty disables authentication).
Put the same value in `server_password=` in the PSP config. Browser login uses
the fixed username `psp` and that password; no accounts need to be created.
Restart the app after changing its password. Use a long random single-line
password, at most 128 UTF-8 bytes for PSP compatibility.

Do not forward this HTTP port directly to the Internet: HTTP Basic does not
encrypt credentials. Use a VPN for the PSP, and HTTPS plus a hardened reverse
proxy for any public browser access. Keep the backend private. See the main
README's Security section for details.

Update the add-on to **0.1.20 or later for the current PSP client**. Version 0.1.19 introduced the FLV endpoint; 0.1.20 adds same-folder successor lookup so playback started through the web remote can continue to the next episode or music track. Install the matching PSP client as well. Restart the add-on after updating. No media mount or configuration changes are needed; older PSP clients can still use the legacy endpoints.
# Version 0.1.25: stream options

Update the PSP client together with this app to use MP3 VBR V6/V5/V4/V3 and
optional 23.976 fps video. Both settings are available in the browser remote
and the PSP playback dialog. Existing CBR and 20 fps defaults are preserved.
The browser remembers its choices locally and includes them with Play.
No additional Home Assistant app configuration is required for these options.
