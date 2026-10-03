# 2.3 — PSP Consolizer, Fullscreen TV Gaming and Configurable Rumble

Changes since tag **2.2**:

- **PSP Consolizer:** use a StreamMaster-connected controller in PSP games,
  homebrew, the XMB and PS1 games, with automatic reconnection, a dedicated
  mappable PS/Home button and an optional Start+Select shortcut.
- **TV-ready console setup:** automatic TV activation, cold-boot XMB handling,
  explicit USB handoff for Memory Stick access, optional controller name/battery
  metadata, configurable status overlays and switchable diagnostics.
- **Per-game configuration:** title-ID and exact-path overrides for Consolizer
  and StreamerOC, with title-ID precedence and global defaults.
- **FuSa Fullscreen:** a standalone 720×480 progressive TV scaling plugin with
  automatic activation, persistent fullscreen, optimized capture/scaling and
  no session timer. This scales the image, not the game's internal resolution.
- **Fullscreen compatibility:** repaired scene/movie transitions, Sony HOME,
  save dialogs and CustomHOME; both OC and Consolizer overlay modes can now
  render directly into fullscreen output. Validated with Metal Slug XX,
  Soul Calibur and Star Ocean on the tested PSP-3000 / ARK-5 setup.
- **Real PS1 rumble:** POPS motor commands reach supported Bluetooth controllers
  through StreamMaster. Confirmed with the SF30 Pro in XInput mode in Need for
  Speed: High Stakes and Wipeout 3. This path preserves real PS1 motor commands.
- **Wired USB HID:** added input support through the existing controller
  mapping workflow; individual adapters still require compatibility testing.
- **Lean plugin memory use:** compact, on-demand overlay storage and coordinated
  overlay handling improve coexistence without exhausting kernel RAM.
- **StreamMaster robustness:** hardened four-deep USB transfer startup,
  capability-sized receive buffers, firmware reporting and transfer diagnostics.
- **Plugin settings on the PSP:** edit OC, Consolizer and FuSa options, manage
  title/path rules and Consolizer path filters, with verified writes, INI backups
  and bilingual help. No PC is needed to adjust an installed plugin.
- **Monkey combat:** enemy collisions now damage both hulls (30% player / 60%
  enemy maximum health), once per contact, with swept detection, explosions and
  kill credit. Three-shot blaster kills remain unchanged.
- **Native-app rumble:** optional, independently adjustable bass/beat and impact
  feedback in Monkey, including light wall scraping, heavier crashes and enemy
  hits. Uses the existing StreamMaster controller channel, with expiring output
  and no additional network polling. Both strength sliders default to off.
- **PSP game hit rumble:** opt-in, read-only health monitoring through per-title
  or launch-path profiles, configurable directly in PSPStreamer. Supports
  integer/float health, optional pointer and battle flag, pulse strength/duration
  and cooldown. Includes bilingual guidance and diagnostics; verified game
  addresses must be supplied by the user. No automatic cheat/address detection.
- **Release cleanup:** FuSa Fullscreen now has its permanent name, a concise
  installation guide and a separate development history; confirmed tasks have
  been removed from the ToDo.

## Updating

- Copy **EBOOT.PBP, PSPStreamer.prx and StreamMasterUSB.prx together**. Preserve your player config,
  media, controller mappings and plugin INIs.
- Use the current Consolizer/OC plugins for fullscreen overlay integration.
- Native Monkey rumble also needs the updated **PSPConsolizerUSB.prx** when the
  resident plugin is used; restart the PSP. Existing POPS-rumble-capable ESP
  firmware remains compatible and does not need reflashing for this addition.
- PSP health rumble requires that same updated resident plugin. See
  `psp-controller/RUMBLE.md` (also packaged as `RUMBLE.md`) before enabling a
  profile. Preserve your `PSPConsolizer-rumble.ini` across updates.
- Migrate the old fullscreen folder/INI/ARK entry to
  `SEPLUGINS/FuSaFullscreen/FuSaFullscreen.prx`; do not enable both copies.
- The new plugin editor does not install plugins or modify ARK registrations.
  Saved options take effect at the next game/application launch; restart the
  XMB/PSP for VSH changes. Overclock stability remains hardware-dependent.
- Supported POPS rumble requires the corresponding StreamMaster firmware;
  it is not a promise of universal controller, firmware or game compatibility.

The settings editor, Monkey collision/native-rumble and health-profile additions have passed
their builds and focused host-side checks; their final on-device tests remain
pending. Existing fullscreen and
overlay behavior was confirmed by the user before these additions.
