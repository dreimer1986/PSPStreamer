# Monkey audio reference path

Previously the reaction to a beat was reconstructed from Monkey, but its detector
used PSPStreamer's twelve display bands. Desktop FFT mode replaces that
approximation. Light mode deliberately retains the inexpensive old path.
Select Music analysis = Desktop FFT in main Settings or Spectrum options.

Audited local input: `vis_monkey.dll`, SHA-256
`c7dcfc838d0979870947d8e5b3f1fd5ad5a9e9c8e92fa77ba8de23c842fc614d`.
No DLL code or assets are bundled. Reconstructed equations:

- `0x100104a0`: 576 signed-eight-bit samples, current/previous sample mean
  (first sample repeated), Hann envelope, zero padding to a 1024-point FFT.
  Beats use the left channel; the unused right-channel transform is omitted.
  Equalization: `-0.02 * log((512-bin)/512)`.
- `0x1001058c`: boundaries from `256 * pow(group/3, 2.1)`, truncated:
  0, 25, 109, 256. Average each group and multiply by the original factors
  1/0.270, 1/0.343, 1/0.295. NOT MilkDrop 2's custom ranges.
- `0x10010649`: average retention 0.2 rising / 0.5 falling; long average
  retention 0.96, at a 14 Hz reference. Initial averages are 1. Unused medium
  history is omitted. Retention is adjusted for elapsed render time.
- `0x10009d80`: threshold starts at 5, target/previous peak at 1.3. Sensitivity
  changes relaxation and rearm thresholds. A peak triggers once; smoothed
  relative energy must fall below threshold before rearming. No substituted
  250 ms lockout or bass-only trigger in Desktop FFT mode.
- `0x10006904`: existing recovered response: probabilistic style change,
  alternating rotation impulse or forward impulse, with source decay.

The legacy bass/pulse envelopes remain for our flight-mode exhaust extension,
not normal Monkey beat events in FFT mode. Bar count and display gain never
affect these calculations.

## Boundaries and verification

This closes the known analysis/detector substitution, not a claim of identical
frames/random events for a song: snapshot timing, render cadence, random
sequence and existing PSP geometry budgets still differ. One shared FFT is
capped at 20 Hz outside the audio thread. Monkey updates history/detection at
render cadence from that snapshot. Near-underflow silence has a division guard
rather than allowing non-finite PSP values.

`tests/test_monkey_audio_reference.py` executes bounded isolated routines in
Unicorn, not Winamp or Windows imports. It checks grouping/history and 1,200
detector steps over sensitivities 0/4/8/12/16 against the DLL, including exact
trigger/rearm decisions. The input filter has an independent DFT check.
MilkDrop's unchanged analysis is separately compared to its original C++ FFT.
The standalone Cave integration/bounds harness passes. The older full GU host
harness cannot currently compile (stale title API and missing GU constants);
it is not counted as passed. The actual PSP toolchain build succeeds.

Hardware test: compare Light/Desktop FFT with the same rhythmic music in Monkey,
change sensitivity/response, pause/resume and switch tracks/visualizations.
Confirm clean audio in LCD/TV and unchanged flight controls.
