# Flight-mode follow-up

Confirmed working by the user on 2026-09-27: four music-reactive exhaust lights,
ship fade-in, wall recoil/blinking recovery and double-L/R barrel rolls.
Normal visualization remains unchanged until the flight Easter egg is enabled.

## Current status — 2026-09-29

Shield, wall damage, explosion/Game Over, survival points, persistent top-ten
scores and the intro menu are implemented. Seven selectable ships with previews,
anchored turrets and projectile combat are also implemented; models are no longer
a blocker. Both sides fire every 500 ms; three player hits destroy a turret,
enemy hits cost 10 shield, and a kill earns 100 points. Barrel rolls protect
against projectiles, not walls. One missed spawn can be retried, without building
an enemy backlog. Square cannot change visualization during the game.

The user confirmed model, balance, spawn-retry, controls and Desktop FFT on
2026-09-29. Do not reopen these tests. The new floor-hovering waypoint drones
now need hardware feedback: they use a non-player ship with inverted colors,
bounded short patrol segments and hull-clearance checks. Turrets remain in the
spawn mix; at most one enemy is active. Damage, firing and rewards are unchanged.

Remaining proposals, not approved implementation work:

- Full free-flying enemy AI only as a separate, larger project.

The consolidated active checklist is
`/home/dreimer/psp-streamer/psp-client/release/Probleme und Ideen.txt`.
