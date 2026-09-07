# PSP Streamer Home Assistant Add-on

Exposes Home Assistant's `/media` directory read-only on port 8091 for the PSP client.

Mount or place the library under Home Assistant's media directory. SMB credentials stay outside this add-on.

Options: `port` (default 8091) and `max_transcodes` (default 4). Video uses one FFmpeg process containing both H.264 and MP3 with shared FLV timestamps; music uses one MP3 process. The add-on includes MKVToolNix for native PGS subtitle extraction.

Update the add-on to **0.1.19 or later before installing the timestamp-based PSP client**. Older servers do not support its FLV endpoint. Restart the add-on after updating. No media mount or configuration changes are needed; older PSP clients can still use the legacy endpoints.
