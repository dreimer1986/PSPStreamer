# PSPStreamFTP — experimental Memory Stick FTP

A separate, optional **VSH-only** plugin. The PSP performs file operations;
StreamMaster supplies incoming TCP over its Wi-Fi. Requires PSPConsolizer in
VSH and the **Onju V3 FTP test firmware**, not the standard firmware.
No PSPStreamer, Consolizer, Bluetooth or optical-audio code is replaced.
Real PSP/ESP operation still needs hardware testing; regression-free behavior
cannot be guaranteed. Keep the standard firmware for rollback.

Version 0.1 caused a reported VSH cold-start shutdown before any FTP status
file was written. Version 0.2 removes INI/file I/O from `module_start`, defers
initialization for 20 seconds, and halves the kernel worker stack to 8 KiB.
Large scratch buffers live in the user partition. Compiler stack-usage output
now reports 336 bytes for the worker and 2,784 bytes for the command handler;
this is not a hardware proof of the crash's cause. The next cold-boot test is
still required. No firmware change is needed from `ftp1`.

## Install

1. Copy `PSPStreamFTP` to `ms0:/SEPLUGINS/PSPStreamFTP/`.
2. Edit `PSPStreamFTP.ini`: set your own `user`, nonempty `password`, and
   `enabled=1`. Keep port `2121` unless needed otherwise. INI changes require
   restarting VSH. Credentials are stored as plain text on the card.
3. After the existing PSPConsolizer entry, add to ARK's `PLUGINS.TXT`:

   ```text
   vsh, ms0:/SEPLUGINS/PSPStreamFTP/PSPStreamFTP.prx, on
   ```

4. Flash the separate test **application** image to an Onju V3 already running
   StreamMaster. Disconnect the PSP before connecting the board to the PC:

   ```sh
   esptool --chip esp32s3 --port /dev/ttyACM0 --baud 460800 write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x10000 streammaster-onju-v3-ftp-test.bin
   ```

   This updates only the app partition, preserving the existing bootloader,
   partition table, saved Wi-Fi profiles and Bluetooth pairing. Do **not** erase
   flash. The application uses the established QIO/80 MHz configuration; `dio`
   here is the flash image header convention, not a change to the QIO baseline.
   This is not a factory image for an empty board or a generic ESP32 board.

5. Reconnect the normal PSP/Onju USB setup and restart VSH. Allow at least ten
   seconds (20–30 seconds with version 0.2). The plugin uses the ESP's saved Wi-Fi profiles. Read
   `SEPLUGINS/PSPStreamFTP/status.txt` for its IP/listener status; the same ESP
   address is available in PSPStreamer's StreamMaster settings.
6. Connect a PC FTP client to that IP, port `2121`: **plain FTP, passive mode,
   one connection**, your configured credentials. Prefer binary transfer mode.

## Scope and safeguards

- FTP `/` is exactly `ms0:/`. Device prefixes, backslashes and traversal outside
  that root are refused. No flash, internal storage or other devices are exposed.
- The complete Memory Stick is writable, including saves and plugin settings.
  Be careful; there is no recycle bin. `read_only=1` disables mutations.
- **FTP is unencrypted**, including its password. Use only a trusted LAN;
  do not expose/forward it to the Internet. No anonymous login or FTPS.
- One control connection and one passive transfer, with bounded buffers and
  timeouts. Passive data must originate from the control connection's IP.
- Uploads use a temporary sibling file; an existing destination is replaced only
  after a successful transfer. ABOR and detected socket errors discard the temp.
  A normal TCP end-of-file cannot prove that the sender intended a longer file.
  Compare size/checksum after important transfers.
- Replacement briefly preserves the old file as `.smftp-backup-*`. Power loss
  can leave a backup or `.smftp-part-*` file: recover/inspect it manually. Keep
  enough free card space for the old and new file together.
- Stop transfers before starting a game/app or entering USB mass-storage mode.
  This is **not** an in-game background file server. Unload is cooperative;
  unused firmware sockets expire after 20 seconds without plugin commands.
- Standard outgoing streaming sockets and USB ownership are unchanged. The
  firmware extension uses four separate nonblocking socket slots and no extra
  task or bulk transfer ring. Throughput is not promised to match downloads.
- UTF-8 paths, directory listings, create/remove/rename, upload/download and
  download resume are implemented. Upload resume and active FTP are not.
  `TYPE A` accepts listing clients; file bytes are not newline-translated.

## INI

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `0` | `1` starts service in VSH only. |
| `user` | `psp` | Login name, up to 63 bytes. |
| `password` | empty | Required, up to 127 bytes; no default password. |
| `port` | `2121` | Control port, 1–65535; passive ports are ephemeral. |
| `idle_seconds` | `300` | Control-session inactivity timeout, 30–3600 seconds. |
| `read_only` | `0` | `1` prohibits uploads, deletion, rename and mkdir. |

Use exactly `key=value`, one per line. Do not publish your configured INI.

## Test and rollback

First use a disposable directory: upload/download a small file, compare SHA-256,
then rename and delete it. Test a larger upload and cancellation with an existing
destination. Leave VSH, check normal PSPStreamer streaming, Bluetooth and S/PDIF.
The targeted host tests exercise the actual PSP FTP implementation and ESP socket
extension with real TCP, but cannot validate USB timing or PSP kernel behavior.

To revert, disable the plugin entry and restore your previous standard Onju
firmware. No standard release files were replaced. An app-only baseline image
can also be flashed at `0x10000`; never flash a factory image at that offset.

## Build

Use the normal PSPSDK environment and `make -C psp-ftp`. With the repository's
ESP-IDF environment active, run `bash streammaster/build-ftp.sh` to create the
separate firmware build. Run only the affected host tests:

```sh
python3 -m unittest discover -s tests -p test_streammaster_ftp.py -v
```
