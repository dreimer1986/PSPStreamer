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

The latest model, balance, spawn-retry and control changes still need the targeted
hardware checks listed in the user's active ToDo. Do not reopen already confirmed
exhaust, fade-in, recovery or basic barrel-roll tests.

Remaining proposals, not approved implementation work:

- Waypoint-following drones as moving enemies.
- Full free-flying enemy AI only as a separate, larger project.

The consolidated active checklist is
`/home/dreimer/psp-streamer/psp-client/release/Probleme und Ideen.txt`.
