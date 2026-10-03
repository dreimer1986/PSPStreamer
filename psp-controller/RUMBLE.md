# PSP game rumble from health changes

PSP Consolizer can **read** a game's health value and send a short vibration
when it decreases. This is synthetic hit feedback, not native game rumble.
It never modifies game memory, enables an infinite-health cheat, or patches
game instructions. Existing PS1/POPS motor commands and Monkey's native-app
rumble remain separate.

There are **no verified Soul Calibur addresses included**. A game's title ID
does not guarantee that every revision, region or mod uses the same address.
Only enable a profile after checking your exact copy. A wrong but plausible
address can cause false vibrations; no generic detector can infer its meaning.

## Start with the simple setup

1. Update `PSPConsolizerUSB.prx` and restart the PSP. Use a supported rumble
   controller (the SF30 Pro XInput backend has been tested for POPS). No new
   ESP firmware is required by this feature.
2. Open PSPStreamer: **SELECT -> Plugins -> PSPConsolizer: Game rumble**.
3. Add the game's **title ID**. Alternatively use its exact launch path.
   Title-ID matches take precedence; the first equally specific match wins.
4. Enter the **full PSP health address**, its **type**, and the actual
   **minimum/maximum health**. Leave pointer and battle flag options off.
5. Keep the default motor strength/duration/cooldown initially. Enable the
   profile, press Circle to return to the profile list, then **START to save**.
   Circle from the profile list cancels unsaved changes.
6. Launch the game again. Lose some health and check the result. Test a new
   round, a loading screen and returning from pause as well.

X opens value entry; address/offset fields accept `0x...` hexadecimal or decimal.
LEFT/RIGHT adjust a value. SQUARE restores that field's built-in default.
The editor displays defaults for omitted fields, not a second hidden global
rumble profile. TRIANGLE twice deletes a profile; SQUARE on its name renames it.
The existing INI is backed up as `.ini.bak` when saved.

## What each field means

| Field | Meaning / default | Do I need to find it? |
|---|---|---|
| `enabled` | 0 = off, 1 = active for this title/path | Turn on last. |
| `address` | Full PSP virtual address, e.g. the debugger's `0x088...` address; 0 = unset | **Yes**, the player's health, not the opponent's or a display animation. |
| `type` | 1 = unsigned 8-bit, 2 = unsigned 16-bit (default), 3 = unsigned 32-bit, 4 = float32 | **Yes**, match how the game stores the number. A 100-point bar can use any of these. |
| `minimum` | Lowest valid health, normally 0 | Usually 0; negative/signed health is not supported. |
| `maximum` | Highest valid health; default 100 is an example, not a detected value | **Yes**, use the real full-health value, which may be 240, 1000, 1.0, etc. Bounds are whole numbers; float values inside them retain their fractional precision. |
| `strength` | Large motor amplitude 0–255; default 180 | Personal preference, not game memory. 0 disables the effect. |
| `duration_ms` | Each pulse lasts 10–1000 ms; default 120 | Personal preference. |
| `cooldown_ms` | Minimum gap between new triggers, 20–5000 ms; default 200 | Prevents a draining health bar or repeated samples from starting too many pulses. |
| `pointer` | Default 0: read address directly. 1: read a 32-bit pointer at address | **Optional**, only for health that moves in RAM. |
| `offset` | With pointer=1, add this nonnegative byte offset to that pointer; default 0 | **Optional**, must be measured for that game's object structure. One pointer level is supported. |
| `gate_address` | Optional address of a uint32 battle-active flag; 0 disables the check | **Optional**, useful when the health location is reused during menus/loading. |
| `gate_value` | Exact flag value that permits monitoring; default 1 | Only needed if a battle flag address is supplied. |

The valid health range is **not a vibration setting**. If normal health is
0–240 and a menu reuses the location for 65,535, the sample is ignored and the
baseline reset. However, a menu writing 0 would still be inside that range:
**range validation alone cannot identify loading screens or real damage**.
That is why the optional battle flag exists. Do not invent a flag address;
leave it disabled unless you have verified one. Health regeneration, a new
pointer, invalid samples and controller disconnection reset/re-arm the baseline.
After arming or a health increase, 500 ms elapse before decreases can trigger.
The initial health sample never counts as damage. Hits during this settling
window are intentionally ignored; damage reaching zero can trigger normally.

Only aligned addresses in `0x08800000`–`0x09FFFFFF` are accepted, also suitable
for the PSP-1000's user RAM. This excludes hardware registers, kernel memory,
uncached aliases and extra slim-model RAM. A pointer's resolved address is
checked again on every read. u16 requires 2-byte alignment; u32/float/flags
require 4-byte alignment. Infinity, NaN and negative floats are rejected without
using the FPU in the kernel worker.

## Finding an address with PPSSPP or other memory tools

Use the **same game region/revision** as on the PSP. Search candidate values
at full health, take a controlled hit, filter for decreased values, then repeat.
If the exact health number is hidden, start with an unknown-value search and
compare changed/decreased/unchanged states. Check integer widths and float32
as necessary. Verify that healing/new rounds increase the same value and that
only the player's damage changes it. A graphical bar's pixel width is not a
reliable maximum health value.

Repeat across round changes and a fresh game launch. If the address changes,
find a stable pointer plus offset, or leave that game unsupported for now.
An emulator observation is a **candidate** until tested on the real PSP.
Use the PSP virtual address, **not the PC process's host address**. CWCheat
instruction words are not full health addresses: their high bits encode an
operation. Do not paste an entire cheat line into the address field. An
infinite-health cheat might patch instructions instead of storing health.

Useful findings to report: title ID, game revision/region, full address, type,
full-health value, one value after damage, and whether the address survives a
round change and restart. Pointer/flag details are optional refinements.

## Files, diagnostics and cost

Profiles live in `ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer-rumble.ini`.
See the **disabled** `.ini.example`. The GUI writes decimal values, displays
addresses as hex, and also accepts hand-written `0x` values in this file.
No profile file/match means off. An invalid file disables only health rumble,
not controller input or POPS rumble. Profiles are loaded at game startup, not
continuously from disk. `pops_rumble=1` is **not** needed for PSP health rumble.

The existing controller worker samples at up to 50 Hz: no extra thread,
network polling, RAM scan or heap allocation. With `report=1`, the normal
five-second Consolizer diagnostics include the matched section at startup,
baseline validity, resolved address, raw value bits and number of detected
events. `report=0` suppresses these logs too. Controller loss, app ownership,
USB pause and suspend clear the effect/baseline. Motor output uses the existing
finite-duration firmware commands and freshness timeout, never an indefinite
motor-on command. This reduces risk but is not a claim that any guessed profile
will identify damage correctly.

## Kurzfassung auf Deutsch

**SELECT -> Plugins -> PSPConsolizer: Spiel-Vibration**. Spielkennung hinzufügen,
Lebensadresse, Datentyp und gültigen Lebensbereich eintragen; erst dann aktivieren.
Kreis zurück zur Liste, START speichern, Spiel neu starten. Ohne geprüfte Adresse
bleibt das Profil aus. Quadrat setzt einen Wert auf Standard zurück.

Der Lebensbereich filtert unplausible Zahlen, z.B. 65.535 bei normalerweise
0–240 Leben. Er bestimmt **nicht** die Rumble-Stärke und erkennt allein keinen
Kampf. „Motor“ (0–255), Impulsdauer und Sperrzeit sind persönliche Einstellungen.
Zeiger/Offset und Kampfstatus sind optional und bleiben zunächst AUS/0.
Eine plausible Zahl an einer falschen Adresse bleibt trotzdem ein Fehlalarm.
Nicht den CWCheat-Befehl oder eine PC-Adresse als PSP-Lebensadresse verwenden!

Datentyp: uint8/uint16/uint32 = positive Ganzzahl mit 1/2/4 Bytes;
float32 = Gleitkommazahl mit 4 Bytes. Der Default uint16 ist keine automatische
Erkennung. Minimum meist 0, Maximum der echte volle Lebenswert (nicht die
gezeichnete Balkenlänge). Zeiger lesen bedeutet `[Adresse] + Offset`, nicht
`Adresse + Offset`. Die optionale Kampfstatus-Adresse wird als uint32 gelesen
und muss exakt den eingestellten Kampfstatus-Wert enthalten.
