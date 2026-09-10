# Preset initialization and q variables

`per_frame_init_1` through `per_frame_init_16` and `q1`–`q32` are now
supported. Init and ordinary frame programs have independent consecutive
numbering and may be interleaved in the file. Each has a 128-instruction
budget; the 16 KiB file, 255-byte line, 16-level nesting and 24-float stack
limits remain unchanged. Unknown names, `q0`, `q33`, and `q01` are rejected.

## Exact lifetime in this subset

1. Both programs are compiled when the preset file is read, before music
   workers start. There is no source parsing or allocation on rendering ticks.
2. On the first custom-effect frame, run init once with static outputs,
   current time/music inputs and all q values zero. Save its q results.
3. Before every frame program, reset ordinary outputs to their static values
   and q values to the saved init results. Frame assignments are sequential.
4. Frame q writes do **not** modify the saved init results. Turning the effect
   off/on or starting another track creates a fresh activation. Fullscreen,
   output-layout changes and throttled frames do not re-run init.

```ini
[preset00]
per_frame_init_1=q1=.7; q2=.12;
per_frame_1=q1=q1*time; wave_x=.5+q2*sin(q1);
```

Here q1 starts at .7 **each frame**, then becomes .7 times elapsed time for
that frame. `q1=q1+1` alone would not count frames. This is deliberate:
MilkDrop 2's `LoadPerFrameEvallibVars` reloads `q_values_after_init_code`
before each frame. Ordinary output assignments made by init are not retained
as new static defaults either; use q values to carry init results into the
frame program. Arbitrary named persistent variables and `monitor` remain
unsupported. q values are finite floats; existing output-range validation
still applies when assigning them to renderer outputs.

The reference executes init during preset compilation/loading. Our bounded
implementation defers execution to first custom activation so music inputs
are available. This timing difference is explicit: init using `time` or
audio values samples the first active custom frame, not file-loading time.
The first frame can execute up to 256 instructions (init + frame); subsequent
frames execute at most 128. No decoder/DAC work or timing is changed.

## Failure and isolation

Preset loading is transactional. Init arithmetic errors report `init formula`
with the source line. A failed init/frame evaluation commits neither renderer
outputs nor activation seeds. Existing UI failure handling disables the effect
and leaves music running. The same parsed preset can have independent states;
the playback adapter owns one state and resets it on activation.
The stateless convenience evaluation APIs use a fresh state per call; playback
uses `md_eval_preset_state` to avoid re-running init on every frame.

Additional storage: one 2,056-byte init program per parsed preset, 132 bytes
of activation state, and 32 more float slots in evaluation arrays. No extra
VRAM, textures, GU commands, waveform copying or audio-worker processing.

## Test

Copy `presets/init-orbit-demo.milk` as `presets/active.milk`, restart music
and select the custom slot (Square four times if starting with visualization
off). The waveform orbits smoothly using initialized speed/radius values;
rotation, colors and music-driven echo/zoom use the frame program.
Test receiver/fullscreen on LCD and TV, disable/re-enable, and change songs.
Effect selection and fullscreen still survive music track changes, but each
track initializes its effect state anew. No server/add-on change is needed.

Host tests cover q1/q32, zero defaults, non-accumulation, once-only init,
independent activations, static-output reset, numbering/budgets, errors with
atomic rollback, long-run demo evaluation and both real adapter layouts.

Reference, revision `b5e4136c2f050eafa10aa199bb72c8e5c12c9320`:
[`state.cpp`](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/vis_milk2/state.cpp)
(init execution and q capture) and
[`milkdropfs.cpp`](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/vis_milk2/milkdropfs.cpp)
(`LoadPerFrameEvallibVars` reseeding).
