# QIO80 transport IRAM comparison

Optional firmware `0.3.9-bt-qio80-iram`; normal QIO80 remains the default.
Build/package with `bash build-bluetooth.sh qio80-iram` and
`bash package-bluetooth.sh qio80-iram` after activating ESP-IDF.

Only three application functions are placed in instruction RAM:

- `sm_sockets_bulk_read`: ring extraction, bookkeeping and transport checksum.
- `copy_out`: bounded, wrap-aware ring copy helper.
- `transfer_done`: USB completion bookkeeping/wakeup.

They are kept out of line in this comparison to preserve the explicit IRAM
placement. No checksum algorithm, protocol, buffer ownership/size, core affinity,
priority or clock changes. Other callees keep their placement: this is NOT a
cache-disabled execution guarantee. PSRAM holds the same data as before.

Internal memory is shared and scarce. Inspect linked symbol placement and IRAM
growth before packaging; reject an unexpectedly large footprint. Compare the
same app (8x4), HTTP file, PSP 333 MHz, AP and Bluetooth activity. Latest complete
8x4 baseline: 170143443 bytes / 219724 ms = 756.2 KiB/s; one earlier first-group
timeout is still unresolved and must not be attributed to this new firmware.
Keep logging enabled for comparison. Check connection, controller, media and
download cancellation as well as rate. Final SHA verification is separate.

If there is no repeatable benefit or less stability, reinstall normal QIO80
with its component flash_args. Both packages preserve NVS when flashed via
flash_args; merged factory images are for fresh installations, not this test.

Build inspection: all three symbols reside at 0x40376xxx (IRAM), not 0x420xxxxx
(mapped flash). IRAM text grows by 740 bytes; the aligned text end advances by
768 bytes. No out-of-line sm_bulk_checksum symbol remains: its loop is included
in the bulk-read routine. After building, the focused ring-copy boundary and
protocol tests passed (33024 ring cases). Hardware benefit remains unmeasured.
