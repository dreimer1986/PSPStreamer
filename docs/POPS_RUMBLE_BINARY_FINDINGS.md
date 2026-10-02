# POPS rumble: local binary inspection, 2026-10-02

Input: user-provided `Downloads/Popsloader_v4i/seplugins/popsloader/modules`.
Read-only analysis; no firmware or runtime memory patched. Files are already
decrypted MIPS PRX/ELF images. No Sony binaries are included in this repository.
The supplied collection ends at 6.60; it does not establish a 6.61 binary match.

## Finding

The 6.60 **03g** controller-serial routine does not implement the same
DualShock command path as **05g (PSP Go)**. Go contains an actual actuator
mapping/update/forwarding path. Consequently, polling two offsets on a PSP-3000
is not sufficient: its normal controller routine does not populate those bytes
with the game's motor commands.

### 6.60 PSP-3000: `pops_03g.prx`

SHA256: `5c693e880f95601b1befa8df205e1a758b0af5ad0768a570c09e7de861a92b70`

- Executable LOAD: file offset `0xC0`, virtual offset `0`.
- Serial-pad routine: module offset `0xA250`, ending before `0xA508`.
- Computes state pointer as `gp + 0x3C00 + port * 0x30`.
- Position 0 samples/maps PSP input and stores button state at state+0.
- Position 1 stores the incoming command byte at state+0x20 and returns ID.
- Position 2 returns the second header byte at state+0x2C.
- Subsequent positions only accept stored command `0x42` and return input
  bytes. The incoming serial data byte is not saved as a motor value here.
- The only state-relative stores in this routine are +0 (buttons), +0x20
  (command) and +0x25 (latched configuration state). There is no Go-style
  actuator handler dispatch in this routine.

This is a statement about the inspected routine/binary, not a proof that every
firmware or every emulation path lacks rumble.

### 6.60 PSP Go: `pops_05g.prx`

SHA256: `a8f41771eb241b9ad3c6bc53cb9fd1b66d078de2f3083b773f4f63d9bd7212b9`

The module information records gp=`0x10000`. The serial routine at `0xA268`
has an additional dispatcher gated by `(state[0x27] & 5) == 5`.
The command table at `0xD7788` covers commands `0x42` through `0x4D`.

| Item | Module offset / state offset | Evidence |
| --- | --- | --- |
| Poll handler (`0x42`) | `0xA6CC` | Maps incoming byte position using +0x2A/+0x2B; writes +0x28 or +0x29 |
| Actuator map (`0x4D`) | `0xABB8` | Incoming 0/1 configures destination selection; other values disable that byte position |
| Motor values | state+`0x28`, state+`0x29` | Both are passed together to an imported output function |
| Motor reset | `0xA7DC` | Clears both bytes and forwards zero output |
| Re-send stored output | `0xA64C` | Reads +0x28/+0x29 for enabled ports and calls the output import |
| Output import stub | `0x40254` | Library `sceMeAudio`, NID `0x41F87286` |

For this Go layout, the two bytes would be `0x13C28/0x13C29` for port 0 and
`0x13C58/0x13C59` for port 1. These are **not verified runtime rumble addresses
for the user's PSP-3000**. The mode gate, controller negotiation and command
mapping must be active for the values to be meaningful.

The actuator handler prologue also occurs in the supplied Go versions:

| Version | Handler offset |
| --- | --- |
| 6.10 | `0xA8FC` |
| 6.20 | `0xA978` |
| 6.35 | `0xA5CC` |
| 6.39 | `0xA630` |
| 6.60 | `0xA6CC` |

Only 6.60 was followed instruction-by-instruction through the output import;
matching older prologues does not establish full binary equivalence. Ordinary
non-Go versions from 6.00 onward match the shorter serial-handler prologue at
varying offsets. Older 3.xx–5.xx versions have different prologues and have not
been fully traced.

### Output forwarding through `popsman.prx` (6.60)

The export `sceMeAudio_41F87286` resolves to module offset `0x25C0`.
It validates the caller pointer, passes its two-byte payload as argument 2,
and calls `scePadSvc_driver` NID `0x7CAB5A3D` with argument 1 = 0.
This links the emulated actuator update to the controller service, rather than
merely finding adjacent bytes that happen to change.

## Consequences for Consolizer

1. Implement a signature-checked interception of the actual serial-pad path;
   identify the running model/version before patching anything.
2. For ordinary PSP POPS, supply the missing DualShock negotiation, controller
   responses and actuator mapping, preserving PSP button/axis sampling.
3. Capture motor changes into a bounded queue/state slot. Do not perform USB,
   file I/O or allocations in the serial hook.
4. Add controller-specific motor output in StreamMaster; the attached
   2563:0526 adapter still has no verified output protocol. A working input
   descriptor alone is insufficient.
5. Always stop motors on disconnect, suspend, exit and stale communication.

Do not replace a PSP-3000's POPS with the Go binary as a shortcut: its additional
hardware dependencies and mode setup are not validated on that model.

Next runtime evidence: exact selected POPS version, a PS1 game/region with a
reproducible rumble scene and preferably a save immediately beforehand. Initial
diagnostics should identify mode/commands; an all-zero memory poll of the stock
03g state would not disprove game support or locate missing motor processing.

## Implemented capture test

`psp-controller/pops_serial.h` implements the inspected command responses in
new C code. `pops_payload.c` is compiled into a self-contained user-memory blob;
the build rejects code relocations and external references. This does not
redistribute any Sony binary or copy a Go handler into the ordinary firmware.

Further tracing found the actual dispatcher at module offset `0x9E64`:
`0x9E88` / `0x9E98` construct the pad-handler address; `0x9EAC` executes
`jalr v0`, with `andi a0,a0,0xff` in its delay slot. Other serial targets,
including the memory-card path, share this call site. The replacement user stub
compares v0 against the validated original pad target, and tail-calls v0
unchanged for every other target. A single JAL instruction is patched atomically;
the original pad function is untouched. The user payload tail-calls it for
position zero to preserve Sony's PSP input mapping.

The runtime guard validates 26 handler instructions, the dispatch sequence,
the relocated handler address and GP. The optional real-binary host test passed
against the supplied 6.60 03g image, including simulated relocation and 111
negative signature cases. The serial-state test passed under ASan/UBSan for
all twelve commands, motor mapping/reset/peaks, independent ports, bypass and
invalid positions. The PRX was built before running these focused tests.

This still needs hardware validation. Runtime 6.61 matching, game negotiation,
soft resets and save states are not established by those host checks. No
controller motor-output protocol or firmware command is invented; physical
rumble remains a separate pending step. Enable with `pops_rumble=1`, `report=1`
as described in `psp-controller/README.md`.
