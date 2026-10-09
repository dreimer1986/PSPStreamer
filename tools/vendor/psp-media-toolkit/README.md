# PSP media toolkit snapshot

Source: https://github.com/TotalKommando/psp-media-toolkit (master, retrieved
2026-10-09 via the raw source endpoint). MIT license retained alongside source.
`pspmedia.py` is unchanged upstream code, not an independently audited codec.

Used for native ICON1.PMF conversion/inspection and ATRAC RIFF loop metadata.
Conversion checks include AU round-trip, index sizes and FFmpeg decoding.
Real PSP XMB validation is still necessary; do not equate host decoding with
hardware compatibility. The separate ATRAC encoder is not bundled.

The source was obtained using the available web reader because shell GitHub DNS
was unavailable. All 845 source lines were recovered; the PMF conversion ran
locally. Exact snapshot identity is its Git-tracked content/hash.
