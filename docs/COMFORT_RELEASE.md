# PSP comfort update — 2026-09-25

Built with the established `-O3 -G0` PSP toolchain. Install **EBOOT.PBP and
PSPStreamer.prx together** in the existing PSP/GAME/PSPStreamer directory.
Keep all firmware bridges, font, presets, textures and PSP/SYSTEM files.
Do not copy anything over your PSPStreamer.cfg or PSPStreamer.state.

## Controls

- Browser SELECT → **Quick access / playback limits** (above Help): favorites,
  recently played, sleep timer, completed-file limit and five server profiles.
- Highlight a file/folder before opening Quick access to toggle its favorite.
- Video resume: X continues, Square restarts, Circle cancels. Applies to local,
  file/SMB and DLNA sources as well as provider positions from Plex/Jellyfin.
- Local storage: L opens travel inventory/free-space summary. Transfers show
  remaining space needed after conversion; X confirms, Circle cancels. In a
  queue transfer, confirmation is requested per ready job.
- Server profiles: save the active settings with Start first; Square stores a
  named profile, X activates it, Triangle removes it. Includes the password.

## Persistence

`ms0:/PSP/SYSTEM/PSPStreamer.state` and `.bak` retain own video positions,
favorites, history and connection profiles across ordinary updates. Positions
are saved when playback returns, not during sudden power loss or forced exit.
Profile passwords are plaintext on the stick; protect backups. Sleep/file limits
last only for the current app session and stop playback, not the PSP itself.

## Server / Home Assistant

Server/add-on **0.1.52** adds DLNA cover proxies; HACS integration **0.1.3** accepts
their media IDs. No new HA entity is needed. DLNA cover availability depends on
the source. Cover images are not reused as fake landscape backgrounds.

## Focused validation and PSP handoff

Native host checks cover state recovery, provider/local resume decisions,
completion limits, inventory/partial-transfer sizes, settings navigation and
translated help layout. DLNA proxy/image conversion and HA media-image paths
are checked separately. Docker and HA source parity is checked. No preset sweep
or broad decoder benchmark was run for these menu changes.

On hardware, verify a stopped video resumes after an app restart, a completed
video does not resume, and remote/autoplay starts do not wait for a dialog.
Check favorite folder/radio shortcuts, profile switching, local inventory and
one confirmed download. Set the file limit to one, seek near the end and check
that the next episode does not start; then test a 15-minute timer. Check both
LCD/TV menu layout. The existing timestamped decoder remains unchanged apart
from checking the timer in its control loop and saving state after cleanup.
