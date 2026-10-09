# PSP validation closeout — 2026-10-09

User confirmed completion of the current PSP/plugin/S/PDIF tests. Remove those
completed checks from the working ToDo, rather than retaining old pending labels.
Xbox work and physically untested generic ESP boards are separate.

- FuSa 0.27 persistent-framebuffer refresh: GTA cutscenes substantially smoother,
  confirmed after replacing the old 0.26 binary on the Memory Stick.
- GTA VCS ULES00502 cold start works at **333 MHz with Enforce enabled**.
  366 MHz failed cold starts; this title stays at 333 MHz. No universal OC
  stability is inferred. No attempt to force delayed higher clocks is planned.
- System optical audio and PSPStreamer optical playback accepted by the user.
  Established two-second loader delay restored after the early VSH experiment.
  A complete optical boot sound is not promised; do not reintroduce the early path.
- Consolizer overlay confirmed without StreamerOC. StreamerOC remains GAME-only;
  VSH/POPS entries are not part of supported installation.
- Current PSP plugin management, controller, rumble and related test checklist
  accepted by the user. Hardware/profile compatibility is not generalized beyond
  their tested equipment. Unimplemented features are not marked complete.

This is the acceptance record, not another open test list.
