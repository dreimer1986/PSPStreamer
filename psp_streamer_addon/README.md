# PSP Streamer Home Assistant Add-on

Exposes Home Assistant's `/media` directory read-only on port 8091 for the PSP client.

Mount or place the library under Home Assistant's media directory. SMB credentials stay outside this add-on.

Options: `port` (default 8091) and `max_transcodes` (default 4). One PSP uses one video and one audio process.
