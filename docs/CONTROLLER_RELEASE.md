# Controller and visualization update — 2026-09-25

Install **EBOOT.PBP and PSPStreamer.prx together**. Keep the other firmware
bridges, subtitle font, Monkey textures and all PSP/SYSTEM config/state files.
The previous release pair and preset directories are archived on the build PC.

## Server

Update the Docker or Home Assistant server app to **0.1.53** and reload the web
page. Remote control → **PSP buttons and text input** opens the virtual controller.
Click/hold buttons, drag the analog stick, or use the keyboard shortcuts shown
there. Mouse users can latch L/R; multi-touch supports simultaneous buttons.
This does not control XMB or other apps and does not mirror the screen.

Open a text field in the PSP app. Send UTF-8 text from the phone to replace the
draft; then START accepts or Circle cancels. START in the settings page saves.
Existing password contents are never read back. Use HTTPS for remote access.
HACS integration 0.1.3 does not need an update for these web UI changes.

## Visualizations

MilkDrop and Monkey use the full inner monitor on LCD and TV. X repeats the
five-second track-title effect; TRIANGLE still toggles fullscreen. Titles enter
MilkDrop feedback and may leave decaying trails; Monkey projects coloured text
inside the scene. Spectrum retains its normal metadata area.
Automatic music changes retain the visualization, fullscreen choice and GU
state. Audio resources still reset safely. Metadata waits advance silent frames;
short synchronous cleanup/network waits hold the previous frame.

The new local preset folder has 51 shaderless, unmodified third-party originals
selected for modest CPU requirements. Its README/manifest records provenance.
Move the old PSP preset folder aside rather than merging the new pack into it;
otherwise the old tests remain visible. Never discard your personal presets.
Development presets are still available in source and in the release archive.
The repository carries the reproducible pack builder, not a blanket grant to
redistribute those third-party originals publicly.

## Focused checks / hardware test

Built first with -O3 -G0, then checked only affected paths: native input mailbox,
key ordering/expiry, UTF-8 field isolation, real authenticated HTTP/CSRF, browser
controls at desktop/mobile sizes, translated help, LCD/TV UI and autoplay paths.
Docker/HA source parity passes. All 51 pack entries parse and complete 120 host
evaluation frames without resource clamps. This is not a GE/FPS measurement.

On PSP: navigate settings remotely, hold a direction, test L/R combinations,
send an umlaut-containing profile name and cancel a password edit. Close the
browser while holding a button and ensure repetition stops. Then test both
visualizers in LCD/TV and fullscreen, X title replay, automatic music changes,
Stop and a switch to video. Try the pack at 333 MHz; frame rate and appearance
still require actual hardware validation.
