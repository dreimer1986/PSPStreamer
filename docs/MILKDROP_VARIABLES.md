# Named persistent preset variables

Preset init/frame programs can share up to **16 user variables**, in addition
to built-in inputs/outputs and q1–q32. Names use letters/underscore followed by
letters/digits/underscore, up to 31 bytes, case-sensitive in this PSP subset.
Each distinct name used on either side of an assignment reserves a slot.
Reads before assignment return zero on a fresh activation.

```ini
per_frame_init_1=last_time=time; held=0;
per_frame_1=dt=min(.25,max(0,time-last_time)); last_time=time;
per_frame_2=held=max(psp_low,held*exp(-dt*3)); wave_r=.2+.7*held;
```

`held` preserves an impulse and decays over subsequent frames. Unlike q
variables, its frame writes are retained. The demo uses elapsed time rather
than assuming a constant visualization FPS; its .25-second dt cap deliberately
limits movement after a long render interruption.

| Variable category | Before each frame |
| --- | --- |
| Ordinary renderer outputs | Reset from static preset values |
| q1–q32 | Restored from init seeds |
| User names | Restored from the preceding successful frame, initially from init |

Each activation has independent storage. Turning the visualization off/on,
restarting music or changing songs resets it. Pause still advances visualization
time with zero music input, so held impulses can decay. Fullscreen/layout
changes do not reset state. Throttled calls do not execute frame formulas.
Remembered effect/fullscreen selection across songs is independent of this
fresh per-track runtime state.

## Bounds and diagnostics

Init and frame share a 516-byte name table, built at load time. Runtime uses
numeric slots, not string lookup. State adds 64 bytes per activation and 16
float slots to evaluator arrays; there is no per-frame allocation or new GU,
audio or decoder work. Existing 128-instruction budgets per program, line/file
limits and forward-only branch restrictions remain in force.

The seventeenth distinct user variable is a load error. Failed compilation
rolls back both appended instructions and symbol-table changes. Runtime
arithmetic or output validation failures commit neither state nor rendered
outputs. Other presets and activations cannot see these variables.

Built-in function names, built-in read-only inputs and unsupported engine
inputs (`fps`, `frame`, `progress`, `monitor`, `x/y/rad/ang`, sample inputs,
mesh/pixel/aspect sizes) cannot be silently shadowed. Invalid q register names,
numbered t registers and global `regNN` names remain unsupported.
Unknown **function calls** remain errors. Unknown ordinary variable names are
now user slots: a typo such as `coutner` will create a different zero-initialized
variable, so take care when authoring presets. Static file-field validation
is unchanged.

## Reference and test

The inspected MilkDrop 2 source allocates a preset VM, registers/resets its
variables during recompilation, and reloads only designated built-in/q values
before frame execution. Its user-variable memory consequently persists:
[`state.cpp`](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/vis_milk2/state.cpp),
[`milkdropfs.cpp`](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/vis_milk2/milkdropfs.cpp),
[`nseel-eval.c`](https://github.com/eef2697d62fbe08e2fd927278/milkdrop2/blob/b5e4136c2f050eafa10aa199bb72c8e5c12c9320/ns-eel2/nseel-eval.c).
This bounded float implementation is not full NS-EEL compatibility: notably,
the name limit/reserved set and case-sensitive naming are explicit restrictions.

Copy `presets/memory-pulse-demo.milk` as `presets/active.milk`, restart music
and select the custom effect. Watch the slowly releasing response after bass
impulses and the moving waveform. Check LCD/TV, fullscreen, pause and track
replacement, including unchanged crackle-free audio. No server update.

Host coverage includes zero initialization, cross-frame accumulation, q seed
isolation, fresh activation, output-failure rollback, 16-name/31-byte limits,
reserved names, compiler namespace rollback, a 30-minute impulse simulation,
and the actual parser/evaluator/GU-adapter demo path on both displays.
