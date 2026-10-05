# musl floating-point string conversion

`floatscan.c` is unchanged from musl v1.2.5:
https://git.musl-libc.org/cgit/musl/tree/src/internal/floatscan.c?h=v1.2.5

MIT license, see COPYING. `shgetc.h` / `floatscan.h` are Xbox-specific string
input adapters, not musl stdio. Binary-radix scaling uses nxdk ldexp/ldexpl
instead of its unimplemented scalbn/scalbnl. Public app wrappers live in
visual_strto.c; shared PSP source remains unchanged.
