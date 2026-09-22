# Sparse global memory and remaining preset failures

Historical batch. The subsequent [import/resource update](MILKDROP_RESOURCE_LIMITS.md)
supersedes the capacities and remaining-failure counts below.

This batch changes the bounded MilkDrop interpreter/importer only. It does not
change audio, video synchronization, networking, OC, or the server.

## Reference-backed corrections

MilkDrop 2's `ns-eel2/nseel-lextab.c` recognizes a lone `.` as `DBLCONST`.
`nseel-eval.c:nseel_translate` converts it with `atof`, producing zero. Our
formula parser now does the same: `.-.4` is `0-.4`, not a malformed number.
Adjacent tokens without an operator still fail; this does not change numeric
INI settings. Four collection presets used this spelling.

Desktop's `ns-eel.h` defines `NSEEL_SHARED_GRAM_SIZE` as `1<<20`.
Many presets use high `gmegabuf` addresses for small, separate working areas:
10,000, 30,000 or 50,000. Rejecting every address above 8,191 unnecessarily
rejected some presets which fit our resident memory budget.

The PSP now maps global memory in pages:

* Logical addresses: 0 through 1,048,575.
* Resident data: unchanged 8,192 floats / 32 KiB.
* Page size: 256 floats; at most 32 resident pages across all contexts.
* Extra fixed bookkeeping: about 4.1 KiB, not duplicated per context.
* Untouched pages read as zero. Zero stores to them consume no page.
* First nonzero writes reserve a page from the already cleared pool. Reset
  clears the same 32 KiB as before; rollback restores free pages to zero.
  Pages are not cleared twice, so init work is not moved into the frame budget.
* No runtime heap allocation, eviction, address aliasing, or discarded writes.
* Failed calls roll back new page reservations as well as values/registers.
* Native init clear loops clear the intersecting resident logical ranges,
  preserving unrelated high-address pages and rollback behavior.

This is bounded address virtualization, not a full 1-Mi-value RAM allocation.
Page fragmentation matters: 33 nonzero cells in 33 distinct pages can exhaust
the pool. Pages are retained until preset/global reset, even if later zeroed.
Local `megabuf` remains 4,096 values per context. Invalid/out-of-domain addresses
still fail; Desktop's out-of-range global-address masking is not implemented.
All existing execution budgets remain unchanged.

## Remaining import cases

The other ten rejected files are distinct from the corrected EEL dot syntax:

| Cases | Cause | Handling |
| --- | --- | --- |
| 2 BrainStain exports | Orphan `per_frame_8` line without `=` before a later proper entry | Still reported; no automatic editing of source/duplicate semantics |
| 1 multi-author export | Empty key `=1` | Still reported as malformed input |
| 2 static rotation entries | `rot=-` | Desktop uses its default after failed numeric conversion; PSP retains explicit validation |
| 2 Suksma/Hexcollie exports | Missing statement separator across joined formula records | Still reported, not silently rewritten |
| `amandio c - new life` and `martin - pixies party d-strux wille` | 128 named-variable limit | **Further redesign needed** |
| `amandio c - epicenter the end of the world we never knew` | 64-KiB file limit | **Further resource/loader work needed**; later limits may also apply |

Some malformed records can be ignored or defaulted by Desktop. Doing that
automatically can silently change which expressions execute, so it is not
included in this correction. No user presets were modified.

## Diagnostics and focused validation

Final collection status: **1,705 imports and 1,646 successful 120-frame runs
out of 1,715 files**, compared with 1,701 / 1,637 before this batch. No previously
passing preset regressed. A full collection run initially exposed one
`tendrils reflected` regression from charging a redundant page clear in frame
code. Removing that duplicate work fixed it; all 83 global-memory users were
then rerun for 120 frames, alongside the previously failing subset. Other
presets were unaffected by that final page-allocation-only correction.

Host audits compile with `PM_AUDIT` to distinguish variable/bytecode capacity,
local/global address errors, page exhaustion, and invocation/frame fuel.
`tools/check_milkdrop_collection.py` includes the available `failure_reason`
in its JSON. These hooks, writes and strings are absent from the PSP build.
Unclassified errors keep their existing line/key diagnosis.

The targeted 120-frame rerun of all 78 previously rejected/failed files yields
four additional imports and nine additional full passes (eight from global
memory, one from dot syntax). Three other newly imported presets now reach
shape initialization but exhaust its invocation budget. The 59 remaining
runtime failures in this group are **51 invocation-budget failures** and
**eight local-memory address failures**, not further global-page failures.
Five Pixies Party variants make it through frame zero but exhaust the main
frame allowance at frame one; a successful first frame alone is insufficient.
These remaining memory/complex-formula cases require further redesign rather
than silently truncating loops or increasing every budget.

Tests cover high-address sharing, no aliasing, last-address bounds, partial
clears, rollback of new page reservations, full-pool failure, untouched reads
and zero stores without allocation, ordinary fuel protection, lone-dot syntax,
and the existing LCD/TV renderer ownership path. The full collection's import
and 120-frame checks are relevant because global-memory execution is shared by
all presets; no server tests are needed for this batch.

## PSP test

Install both `EBOOT.PBP` and `PSPStreamer.prx`, and copy the supplied
`sparse-global-demo.milk` into `presets/`.

The demo should show a moving cyan disc and a small stationary orange diamond
on the left. It shares values at 10,000, 50,000 and 1,048,575 across frame and
shape contexts, and uses the lone-dot spelling. Try LCD and TV, window and
fullscreen; audio should remain clean. Change preset and return to check reset.

Useful original collection checks: `martin - chain breaker`,
and `Halfbreak - Light of Breakers`.
Complex presets can remain slow even when every formula passes. Host tests
cannot establish PSP frame rate or Desktop visual equivalence.
