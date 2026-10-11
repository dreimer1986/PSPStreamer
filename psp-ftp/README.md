# PSPStreamFTP 0.4

Transfer files between a PC and the PSP Memory Stick over StreamMaster Wi-Fi.
This optional **VSH-only** plugin requires PSPConsolizer's resident USB bridge.
It exposes only `ms0:/`, never flash or other devices. Onju V3 operation has
been confirmed by the user; generic S3/S2 firmware remains hardware-UNTESTED.

## Install and connect

1. Copy this folder to `ms0:/SEPLUGINS/PSPStreamFTP/`.
2. For a new installation, edit `PSPStreamFTP.ini`: choose your own `user` and
   nonempty `password`, then set `enabled=1`. On updates **preserve your INI**.
3. Keep Consolizer enabled in VSH. Add after its entry in ARK's PLUGINS.TXT:

   ```text
   vsh, ms0:/SEPLUGINS/PSPStreamFTP/PSPStreamFTP.prx, on
   ```

4. Use StreamMaster 0.3.26 or later from the separate **StreamMaster** package.
   The earlier `0.3.25-bt-qio80-iram-ftp1` also works; the plugin update alone
   does not require reflashing. Follow your board's README/flash_args. Split
   images preserve NVS Wi-Fi profiles and Bluetooth bonds; factory images
   overwrite the NVS gap.
5. Restart PSP and wait 20–30 seconds in VSH. The service uses saved ESP Wi-Fi
   profiles. IP/readiness is recorded in this folder's `status.txt`.
6. Connect to the ESP's WLAN IP using **plain FTP, passive mode, port 2121**,
   your configured credentials and **one connection**.

**FileZilla:** Site Manager → Transfer Settings → enable the limit on simultaneous
connections and set it to **1** for this site. Limiting global file transfers
alone is not sufficient: FileZilla otherwise opens separate browsing/download
sessions. Extra sessions receive `421`. Refresh cached listings after updates.

## INI

| Key | Default | Meaning |
| --- | --- | --- |
| `enabled` | `0` | `1` starts FTP in VSH. |
| `user` | `psp` | Login name, maximum 63 bytes. |
| `password` | empty | Required, maximum 127 bytes; stored in plain text. |
| `port` | `2121` | Control port, 1–65535; passive ports are ephemeral. |
| `idle_seconds` | `300` | Control inactivity timeout, 30–3600 seconds. |
| `read_only` | `0` | `1` disables upload/delete/rename/mkdir. |

Use one `key=value` per line. Restart VSH after changing settings.

## Safety and limits

- **FTP is unencrypted, including credentials. Trusted LAN only; never forward
  the port to the Internet.** No anonymous login or FTPS.
- FTP root maps to `ms0:/`. Device prefixes and escaping this root are rejected.
  The whole Memory Stick, including saves/configuration, is writable unless
  read-only mode is enabled. No recycle bin.
- Finish transfers before launching games/apps or enabling USB mass storage.
  This is not an in-game server. The plugin never takes USB ownership.
- One control session and one passive transfer; data must originate from the
  same IP as control. Buffers/timeouts are bounded. Firmware sockets expire
  after 20 seconds without plugin commands.
- Uploads use a temporary sibling file, replacing the destination after success.
  ABOR and detected socket failure discard it. Normal TCP EOF cannot prove the
  sender intended a longer file: verify important transfers by size/checksum.
- Replacement briefly uses `.smftp-backup-*`. Power loss can leave that backup
  or `.smftp-part-*`; inspect/recover manually. Keep enough free card space
  for both the old and new file.
- UTF-8 names, listings, mkdir/delete/rename, upload/download and download resume
  are supported. No active FTP or upload resume. Prefer binary transfers;
  TYPE A accepts listing clients but does not convert file line endings.
- Scratch buffers use user RAM; the kernel worker stack is 8 KiB. Initialization
  is delayed 20 seconds. Firmware opens no FTP listener until the plugin asks.
  Existing streaming, Bluetooth and optical paths are unchanged.

## Troubleshooting and build

Check `status.txt`. If boot fails, disable only the FTP entry using a card reader.
Leave your working Consolizer configuration intact.

Build: `make -C psp-ftp` in the normal PSPSDK environment. Standard firmware
scripts include FTP transport. Package with `tools/package_release.py`;
there is no separate experimental FTP firmware package.

Focused host checks, after building:

```sh
python3 -m unittest discover -s tests -p test_streammaster_ftp.py -v
```

Tests cover authentication, root confinement without root stat metadata,
64-bit sizes, transfers/resume, aborts and extra connections. Hardware
verification is still required for previously untested boards.
