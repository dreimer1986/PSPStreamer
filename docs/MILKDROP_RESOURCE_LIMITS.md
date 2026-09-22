# Import and bounded-memory expansion

This batch addresses the old ToDo items 3 and 4 together. Audio, networking,
video clocks, the server and the OC plugin are unchanged. Hardware playback
confirmation is still required; successful host evaluation is not a frame-rate
or visual-equivalence claim.

## Import corrections

MilkDrop 2's `utility.cpp:GetPrivateProfileFloat` preserves its initialized
default when numeric conversion fails. The nine base float fields now follow
that behavior instead of rejecting entries such as `rot=-`. Existing PSP
defaults are retained; nonfinite numbers, overflow and trailing garbage still
report an error.

Named INI lookup does not treat orphan lines or empty keys as formula entries.
Those records are ignored inside the preset section. Repeated numbered formula
and shape scalar keys keep the first value, matching Desktop's INI lookup.
Static shape flags use integer conversion before truth testing: `.0525` is
false, not a texture requirement. Missing operators/statement separators are
not invented and third-party preset files are not rewritten.

Capacities are now 256 KiB per file, 256 named variables per context, 2048
numbered records per block, 8192 instructions per init/frame program and
32768 instructions per preset. Pixel/point programs retain their 4096-instruction
cap. Bytecode/source allocation remains transactional; empty programs allocate
nothing. Allocation failures leave the old preset usable.

## Sparse local memory and uniform initialization

Local `megabuf` now addresses 0–1048575 with 32 resident pages of 256 floats
(32 KiB) per context. The shared `gmegabuf` pool has 64 pages (64 KiB), also
covering 1048576 logical cells. Shared indices use the reference's integer
conversion/address mask, including negative/wrapped indices. Local invalid
indices still report an error. Desktop local memory can be larger (8 Mi cells);
the PSP logical ceiling is an explicit implementation limit.

An exact recognized init loop such as
`n=0;loop(300000,megabuf(n)=.1;n+=1;);` stores a uniform range default rather
than writing/allocating 300000 floats. Up to eight defaults coexist. Subsequent
sparse changes allocate pages initialized from those defaults. Loops with
additional side effects are not replaced. Native global zero clears handle
wrapped ranges without clearing unrelated cells.

The runtimes own their arrays by value, with no playback-time heap allocation
or shared ownership pointers. Failure rollback restores registers, random
state, memory writes, page reservations and range defaults. Sparse allocation
avoids reserving a full megabyte-valued array per formula context, but costs
about 4 KiB of page bookkeeping per context and increases state-copy traffic.

### Deliberate quiet PSP limit

When all resident pages are occupied, a store needing a new page is discarded.
Existing pages are never evicted or aliased. Reads from absent shared pages
return zero; absent local pages return their uniform default (otherwise zero).
Stores to existing cells continue normally. This allows memory-heavy presets
to continue, **but is not Desktop-equivalent memory** and may visibly alter
their output. Fragmentation matters; even isolated cells can fill the page pool.
The host audit's `resource_limits` mask records local saturation as bit 1 and
shared saturation as bit 2. These are not formula parse failures.

## Execution budgets and remaining hard case

Setup and the main per-frame program each have a 4194304-instruction hard cap.
They no longer consume the separate 262144-step point/geometry pool, so a
large initialization does not leave the first image without geometry. Ordinary
point calls retain their 4096-step cap and adaptive mesh/wave/shape density.
The larger programs and bulk memory operations cooperatively yield every
4096 steps on PSP. This lets audio/network workers run; it does not make an
expensive visualization fast or guarantee responsive controls on hardware.

Arbitrary infinite/huge loops still fail transactionally. In particular,
`martin - city of shadows` exhausts main-frame fuel at frame 116 of the
120-frame synthetic test. Its formulas perform nested 3D search/ray-marching
work in addition to its unsupported shaders. **Umbau nötig:** faster formula
execution or interruptible/resumable evaluation, not another unchecked budget
increase. It is not included as an expected-success hardware test.

## Validation and PSP test

The complete 1715-file import check now loads **1713** (previously 1705).
The two rejected `suksma - Hexcollie - Julian Carnival - shimmy dumb grid
dogmaklyasm nz+5/nz+6` files omit a separator between `gamma=1 + bass*bass_att`
and `chng=sin(time*.5);`. That remains a real source error.
The focused 79-file problem set has 77 imports and **76 successful 120-frame
runs**; the one runtime failure is the case described above. The additional
full-collection execution run was stopped by the assistant after incorrectly
interpreting the user's build-first instruction as a cancellation request.
That run wrote no partial results. No new full-collection 120-frame pass count
is claimed; the completed import and focused execution results remain available.

Direct checks cover import boundaries, INI defaults/duplicates, sparse and
uniform memory, scalar/native clear equivalence, wrapped clears, rollback,
pool saturation without aliasing, infinite-loop protection, formula allocation
failures and LCD/TV renderer ownership. They do not exercise unrelated server
features or re-run media/network tests.

Install **both EBOOT.PBP and PSPStreamer.prx** from the same release and copy
the supplied `presets/` additions. No config change is needed.

1. `local-sparse-fill-demo.milk`: moving cyan circle around a stationary orange
   diamond in the center. Select another preset and return to check reset.
2. `amandio c - new life.milk`: expanded variable capacity.
3. `martin - lock and release.milk`: larger local memory/initialization.
4. `martin - sphery tales.milk`: heavy local uniform initialization and formulas;
   a low visualization frame rate is possible.
5. `amandio c - epicenter the end of the world we never knew.milk`: large-file
   import and shared-memory saturation. Appearance may differ because of the
   documented memory approximation and ignored shaders.

The four third-party files are supplied only in local release folders from the
user's collection, not added to Git. Test LCD/TV as convenient, clean audio,
preset switching and return to video. Report preset name and whether an error,
audio interruption or unresponsive controls occurs. Hardware success can close
the implementation/test items; malformed inputs and the separate heavy 3D
formula case must not be counted as solved.
