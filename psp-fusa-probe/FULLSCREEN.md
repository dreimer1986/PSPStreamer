# FuSa Fullscreen 0.26

Standalone full-screen 720×480 progressive TV output for PSP games on the
tested PSP-3000 / ARK-5 setup. It cooperates with PSP Consolizer and StreamerOC
but does not require either plugin. It scales the game's image; it does not
make games render internally at 720×480. Other models/titles are not universally
validated. Component TV output is required by the current implementation.

## Install or update

Copy the release's `FuSaFullscreen` folder into `ms0:/SEPLUGINS/`:

- `FuSaFullscreen.prx` — fullscreen plugin.
- `FuSaFullscreen.ini.example` — copy to `FuSaFullscreen.ini` for a new install;
  preserve your existing INI when updating.
- `dvemgr.prx` — required TV-output helper.

Use one GAME entry in ARK's `SEPLUGINS/PLUGINS.TXT`:

```text
game, ms0:/SEPLUGINS/FuSaFullscreen/FuSaFullscreen.prx, on
```

When migrating an older installation, move its INI and helper into this folder,
rename the INI to `FuSaFullscreen.ini`, and replace the old plugin entry. Do not
enable both installations. Keep your existing settings; do not overwrite them
with defaults. Restart the PSP after installing. VSH TV activation remains a
separate PSP Consolizer function, not an instruction to enable this in VSH.

## Controls and configuration

PSPStreamer exposes these four settings under **Settings → Plugins →
FuSaFullscreen**. Start saves a backup-protected INI; restart the game to apply.
This does not register the plugin in ARK or alter an active display session.

First enable normal component TV output, manually or through Consolizer.
**NOTE + R** toggles fullscreen. The PSP Screen button retains LCD/TV switching.
There is no session-duration limit.

```ini
auto_zoom=0
auto_zoom_delay_seconds=5
keep_fullscreen=1
experimental_speedboost=0
```

| Setting | Meaning |
| --- | --- |
| `auto_zoom` | `1` enables fullscreen automatically after normal TV output starts. |
| `auto_zoom_delay_seconds` | Delay of 1–60 seconds before automatic activation. |
| `keep_fullscreen` | `1` retains fullscreen intent across recoverable transitions; NOTE+R explicitly disables it. |
| `experimental_speedboost` | Optional bounded guard for an in-progress snapshot after a game's normal VBlank wait; may affect timing. It never waits for a future output slot. Not a literal port of the released legacy Speedbooster. |

The last setting keeps its existing name for configuration compatibility.
It is experimental behavior, not a leftover product/test filename.
The tested user's configuration enables automatic zoom and the bounded guard;
the distributed defaults leave these opt-in.

## Menus and overlays

Sony HOME and CustomHOME are captured as menu sources. Updated StreamerOC and
PSPConsolizerUSB publish their status text to the fullscreen compositor: OC at
top left, Consolizer at top right. Both overlay methods (`1` and `2`), timed
visibility and always-on remain controlled by each plugin's own INI. Outside
fullscreen, their normal renderers remain in use. No extra screen backups or
ESP firmware changes are required.

## Confirmed and still open

User-confirmed: Metal Slug XX, Soul Calibur, Star Ocean's movie after name entry,
CustomHOME, and OC/Consolizer overlays. GTA3-mod and Snes9xTYL follow-ups remain
open. Compatibility is not a blanket guarantee for every game, firmware or
combination of plugins.

Diagnostics append to `ms0:/SEPLUGINS/FuSaFullscreen/fullscreen.log`.
The current diagnostic logging remains enabled; no log-rotation feature is
claimed. Back up the log before deleting it if it contains a problem report.

Build from the repository with the PSP SDK configured:

```sh
make -C psp-fusa-probe -f Makefile.fullscreen
```

See [reference differences](REFERENCE_COMPARISON.md) and
[development history](HISTORY.md) for design decisions and earlier evidence.
