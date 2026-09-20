# VFPU feasibility and measurement — 2026-09-20

Of the three immediate app tasks, this **assessment** has the smallest scope.
The remaining preset failures span several interpreter/budget paths; live
transitions require two simultaneously evaluated presets and resource planning.
Neither is addressed by enabling a compiler flag or replacing one math call.

## Code findings

* The main thread already declares `THREAD_ATTR_VFPU`. The decoder and audio
  output workers do not. Keep any future visual SIMD work on the render thread;
  do not insert VFPU instructions into the PCM producer opportunistically.
* `md_wave_spectrum` is a scalar, 1024-point radix-2 FFT. Each call has 5,120
  complex butterflies, an input/window pass, bit reversal and 512 magnitudes.
  Hann coefficients are initialized once. Stage sin/cos coefficients are
  currently recomputed per call; butterflies carry a serial twiddle recurrence.
* Custom spectrum waves compute up to two channel FFTs from a fresh snapshot.
  Built-in wave mode 8 computes another FFT inside `md_wave_extra`. The existing
  `audio_fft` frame phase therefore is **not** the total FFT cost: built-in
  mode 8 is accounted inside `geometry_submit`.
* These are MilkDrop FFTs, not the separate ordinary spectrum display or the
  analog VU meters. Speeding them up would not speed every visualization.
* General preset formula execution is a branching scalar interpreter. A VFPU
  FFT does not remove its per-point instruction budget or fix import failures.

## Candidates, ordered by implementation complexity

1. Window multiplication and magnitude calculation: regular element-wise
   operations, hence the smallest vector experiment. Costs include loading,
   integer-to-float conversion, alignment/tails and preserving required VFPU
   state. Do not assume the operation count alone implies a speedup.
2. Wave geometry transforms/smoothing: potentially batchable, but vertices
   interleave positions, UVs and packed colors; endpoints, clipping and color
   preservation must remain identical.
3. FFT butterflies: potentially greater benefit but require reorganized
   twiddles/data access. Benchmark a cached scalar twiddle implementation too;
   SIMD is not automatically the cheapest or fastest solution.

No VFPU kernel or new approximation is enabled in this build. No fast-math,
clock, FFT size, audio-buffer or scheduling changes were made. The first step
is to establish how much render time these particular operations consume.

## Direct measurement

With the existing `debug=1`, every MilkDrop profile now includes:

* `fft_warm_calls`, `cold_calls`, `cold_total_us`;
* `fft_prepare`: window/input preparation and bit reversal;
* `fft_transform`: butterfly stages including their coefficient setup;
* `fft_magnitude`: squared magnitude, square root and normalization.

Each phase reports average and maximum microseconds **per warm FFT call**,
not per rendered frame. Cold window initialization is recorded separately.
Both custom-channel FFTs and built-in mode 8 are counted at their common
function. These timings are nested within the existing frame timings: do not
add them to frame totals. Completed FFT calls are recorded even if later frame
evaluation fails. Clocks measure elapsed time, including preemption, not CPU
cycles; use representative averages, not a single scheduling spike.

Debug-disabled playback makes no profiling clock calls. Enabled measurement
uses four clock reads per FFT, stores counters in RAM and writes reports only
after playback stops. Preset/display profile records retain the existing cap.

## Hardware follow-up

Use the normal EBOOT/PRX pair, enable existing debug output, then play the same
music for roughly 30 seconds per preset: `fft-spectrum-demo.milk` (built-in
mode 8), `custom-spectrum-demo.milk` (custom stereo spectrum), and optionally Cauldron
as an interpreter-heavy comparison. Stop normally to flush the existing
diagnostic log in `PSP/SYSTEM`. Switch presets within the same playback before
stopping, or save the log after each playback: the next music start truncates
`PSPStreamer-watch-music.txt`. Keep CPU target, output and fullscreen setting
constant; actual OC initialization must succeed if comparing fixed clocks.

No new log is required merely to verify audible playback. A representative
profile is required before deciding whether a VFPU implementation pays off.
Host tests verify profiling lifecycle and unchanged scalar FFT output against
a double-precision DFT; they **do not** measure PSP throughput or future VFPU
accuracy. A future vector kernel needs a PSP-side scalar/vector timing and
numerical comparison, including silence, impulses, tones, maximum PCM and
non-finite handling where applicable. Report any observed errors explicitly.

## Hardware result and decision

The 2026-09-20 follow-up log contains `custom-spectrum-demo.milk`, LCD fullscreen,
341 completed frames, 664 warm FFT calls and no cold initialization. The user
reports 222 MHz; this profiler does not record actual CPU clock, so treat that
as reported operating context, not a verified frequency measurement. The
built-in spectrum test was not retained. No OC changes were made for this review.

| Measurement | Mean elapsed time |
| --- | ---: |
| Sum of seven existing frame phases | 31,704 µs/frame |
| Frame/shape evaluation | 10,103 µs/frame |
| Custom-wave evaluation | 7,380 µs/frame |
| FFT preparation | 276 µs/warm FFT |
| FFT transform | 952 µs/warm FFT |
| FFT magnitude | 120 µs/warm FFT |

The sum per FFT is 1,348 µs. At 664/341 FFTs per rendered frame, this is about
2,625 µs/frame, or **8.3%** of the measured frame-phase sum. Preparation plus
magnitude total about 771 µs/frame (**2.4%**). Preparation also includes scalar
bit reversal, so even this small share is not entirely the proposed window
multiplication kernel. Eliminating all work in those two phases would save
only that share in this sample; a real VFPU implementation cannot achieve
zero cost. The two evaluation phases together account for **55.1%**.

These calculations use rounded reported averages, assume the retained FFT
calls belong to the completed frames, and are approximate. GPU waits and
preemption are included in frame time. They are not measured VFPU speedups
or predicted display FPS; the scheduler also deliberately throttles rendering
(2,042 skipped calls in this run). Do not extrapolate linearly to another CPU
frequency, TV output or another preset.

**Decision: assessment complete; defer a VFPU rewrite.** The evidence does not
justify prioritizing it over formula/geometry evaluation work. Keep the scalar
FFT and optional profiling. Revisit SIMD if representative workloads show a
larger FFT share, or alongside a separately justified FFT redesign. No VFPU
implementation or frame-rate improvement is claimed by this change.
