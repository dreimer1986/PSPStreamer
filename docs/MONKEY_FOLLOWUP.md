# Flight-mode follow-up

Confirmed working by the user on 2026-09-27: four music-reactive exhaust lights,
ship fade-in, wall recoil/blinking recovery and double-L/R barrel rolls.
Normal visualization remains unchanged by default. An optional cosmetic
autopilot ship now uses model 2 without changing camera motion or enabling combat.

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
were reported as rarely visible. Placement now alternates drone/turret without
rerolling failed attempts, and permits higher drone positions when the floor
is too tight. They use a non-player ship with inverted colors and bounded short
patrol segments. At most one enemy is active. Damage, firing and rewards are
unchanged. Kills now use the existing player particle explosion.

Astro Shield pickup: 42-triangle rest pose, embedded 128×128 green honeycomb
texture, +66 shield points capped at 100. First attempt after 25 seconds, then
35 seconds after collection/miss; three-second placement retries, one item max.
Collection sweeps the player's movement through the item plane, avoiding skips
between frames. No collectible activity outside the live, unpaused Easter egg.

Targeted generated-tunnel checks: all 63 sampled enemy sites spawned (33 drones,
30 turrets); 57/63 shield placements fit. Host checks also cover collection,
cap/single consumption, misses, blocked placement, explosion expiry and pause.
These are bounded sample checks, not a guarantee for every random tunnel.
Hardware feedback is pending for these changes and the autopilot display.

Remaining proposals, not approved implementation work:

- Full free-flying enemy AI only as a separate, larger project.

The consolidated active checklist is
`/home/dreimer/psp-streamer/psp-client/release/Probleme und Ideen.txt`.
