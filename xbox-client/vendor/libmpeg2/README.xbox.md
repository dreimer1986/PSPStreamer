# libmpeg2 0.5.1 — GPL-2.0-or-later

Upstream: https://libmpeg2.sourceforge.io/
Source archive mirror:
https://deb.debian.org/debian/pool/main/m/mpeg2dec/mpeg2dec_0.5.1.orig.tar.gz

Archive SHA256:
`dee22e893cb5fc2b2b6ebd60b88478ab8556cb3b93f9a0d7ce8f3b61851871d4`

This is the decoder subset (C + x86 MMX/MMXEXT), public/internal
headers and COPYING from that archive, with an Xbox-specific config.h.
No decoder algorithms or rounding rules are replaced. CPU detection via
signals is disabled: the adapter selects Pentium III MMX/MMXEXT explicitly.
The Xbox does not have SSE2. The library handles both MPEG-1 and MPEG-2.

Local integration changes: optional per-decoder service callback after each
completed slice (after MMX state restoration), used for main-thread audio refill;
checked allocation of the compressed-data chunk buffer. The callback must never
reenter/free the decoder or change its input. It is NULL unless explicitly set.
Decoding algorithms, bitstream interpretation and picture timing are unchanged.

The app provides checked, aligned YUV frame allocations using upstream's
mpeg2_set_buf API. mpeg_video.h owns their lifetime; libmpeg2 does not free
user-supplied buffers. The same adapter is exercised by host EOF/PTS tests.

This is the classic library, not a newly released MPEG-2 implementation.
