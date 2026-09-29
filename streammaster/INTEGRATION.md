# Integrating StreamMaster into another PSP homebrew

This guide describes the code shipped with firmware **0.2.2**, including the
optional O3 comparison build. It is a source-level integration, not an installed
replacement for Sony's WLAN driver. Unmodified games, the XMB browser and other
applications do **not** automatically use it.

Source links below are relative to a repository checkout. If reading this file
from a firmware ZIP, open the same guide in the
[PSPStreamer repository](https://github.com/dreimer1986/PSPStreamer/blob/master/streammaster/INTEGRATION.md)
to follow links to application sources.

The PSP is a USB device; the ESP32-S3 is the USB host and owns Wi-Fi, DNS, TCP and
optional TLS. Your application still owns its HTTP/media protocol, authentication,
buffering, user interface and recovery policy. No PSPStreamer server is required
for another application's TCP service.

## Prerequisites and scope

- Currently tested hardware: original Onju Voice V3, with StreamMaster firmware,
  a data-capable USB connection and externally supplied host VBUS. See the
  [hardware and flashing guide](README.md). Do not connect two USB hosts together
  or assume the Onju connector supplies power to the PSP. Avoid backfeeding power
  sources when making a direct cable.
- A CFW homebrew environment capable of loading the kernel bridge. PSPStreamer
  uses PSP firmware 6.61 / ARK and `kuKernelLoadModule`; other environments need
  their own compatibility testing.
- PSPSDK, kernel USB stubs and `kubridge`; native TLS fallback additionally uses
  the PSP builds of mbedTLS and system-control support.
- Outgoing IPv4 TCP only: no UDP, raw IP, inbound listening sockets, `accept`,
  multicast/DLNA discovery or general-purpose network-interface emulation.
- Six ESP TCP/TLS connections, twelve local PSP descriptor slots. A closing ESP
  connection remains occupied until its owner finishes. These are not six USB
  links: all traffic shares one serialized exchange path.

## Files to reuse

| File | Responsibility |
| --- | --- |
| [`protocol.h`](protocol.h) | Shared bounded wire structures and validation |
| [`streammaster_usb/`](../psp-client/streammaster_usb/) | Kernel USB device driver, built as `StreamMasterUSB.prx` |
| [`streammaster_transport.c`](../psp-client/streammaster_transport.c) and [header](../psp-client/streammaster_transport.h) | App-local socket adapter, lifecycle and TLS dispatch |
| [`streammaster_redirect.h`](../psp-client/streammaster_redirect.h) | Optional macros for selected Sony/socket calls |
| [`streammaster_fields.h`](../psp-client/streammaster_fields.h) | DHCP/configuration field helpers |
| [`streammaster_ui.h`](../psp-client/streammaster_ui.h) | PSPStreamer-specific setup UI; reference, not a standalone widget |

The adapter header currently includes `../streammaster/protocol.h` and
`tls_transport.h`. Preserve that layout or update the include paths when copying
it. The kernel bridge Makefile also references the shared protocol by relative
path. Keep app, kernel driver and firmware sources versioned together.

`streammaster_transport.c` calls the native `tls_open/send/recv/close` functions
when passed real PSP descriptors. Supply an equivalent backend or reuse
[`tls_transport.c`](../psp-client/tls_transport.c). Even a USB-only build must
resolve those symbols; disabling the native option does not remove link-time
dependencies. When reusing PSPStreamer's TLS backend, review its certificate
storage paths and trust policy for your own app. Do not stub successful TLS
operations or accidentally turn an HTTPS request into plaintext.

## Build and installation

Build the bridge separately with an activated PSP toolchain:

```sh
make -C psp-client/streammaster_usb
```

In your application Makefile, add `streammaster_transport.o`, the required
native TLS backend and its libraries. PSPStreamer's relevant additions are
`-lpspkubridge`, `-lmbedtls`, `-lmbedx509`, `-lmbedcrypto` and
`-lpspsystemctrl_user`; this is not a complete Makefile for every homebrew.
Retain your SDK's normal network imports. Do not duplicate SDK-supplied import
libraries blindly: PSP import-table layout matters.

Put `StreamMasterUSB.prx` beside your application's EBOOT and make that directory
the working directory before initialization. The loader uses `getcwd()`; its
error fallback is hardcoded to PSPStreamer's directory and must be adapted if
your app cannot guarantee a valid working directory. Do not register this driver
globally in ARK's plugin list. USB mass-storage mode and this driver must not own
the PSP USB controller simultaneously.

## Startup and route selection

1. Initialize your existing native network/TLS runtime once. PSPStreamer keeps
   its established COMMON/INET, `sceNetInit`, `sceNetInetInit` and APCTL setup
   even in USB mode; it does **not** associate PSP WLAN in that mode. See
   [`wifi_connection.h`](../psp-client/wifi_connection.h) and
   [`wifi_worker.h`](../psp-client/wifi_worker.h). Do not copy their globals/UI
   hooks into another app unchanged or initialize the same runtime twice.
2. Call `stm_init(use_usb, host, port, use_tls)` and check its result. `host` is
   the hostname only, not an URL; it must fit the adapter's 128-byte field with a
   terminator. Validate port 1–65535 and your input lengths before calling it.
3. Set `stm_diagnostic_enable(debug_enabled)` if desired.
4. For USB, run `stm_associate(&running, 0)` on a worker. It loads the driver,
   waits for USB attachment, negotiates compact framing and associates Onju
   Wi-Fi. `running` must remain alive throughout the call. Keep UI input active.
5. For native Wi-Fi, use your existing association path instead.

The route is selected at initialization. PSPStreamer's setting deliberately
requires an application restart; calling `stm_server()` only updates future
socket destinations, **not** the selected route.

**Important limitation:** `stm_socket()` snapshots the globally configured
host/port/TLS mode. `stm_connect()` does not take the USB destination from the
supplied `sockaddr`. Do not mix several destinations by racing `stm_server()`
against socket creation. Serialize destination selection plus socket allocation,
or extend the adapter with an explicit per-socket destination API first.
Redirects to another host need the same treatment and an authentication policy.

## Connecting and doing I/O

Prefer explicit `stm_*` calls while porting. If using `streammaster_redirect.h`,
include all SDK headers first, then the redirect header in each application
translation unit that should use the adapter. Never force-include it into the
adapter implementation or the native TLS backend: that would redirect their
fallback calls back into themselves. It does not intercept third-party binary
libraries, Sony HTTP APIs or DNS calls.

For USB, skip native `sceNetResolver*` calls entirely. A zero-initialized IPv4
`sockaddr_in` is sufficient for the current adapter because the real destination
was supplied to `stm_init`/`stm_server`. Continue resolving normally for native
Wi-Fi. Do not use the placeholder address for a native socket.

Use this sequence in the connection owner worker:

1. `stm_socket(AF_INET, SOCK_STREAM, 0)`; check for a negative result.
2. Enable `SO_NONBLOCK` with `stm_setsockopt`.
3. Call `stm_connect`. The USB nonblocking path normally returns `-1` with
   `stm_errno()==119` (in progress), not an immediate failure.
4. Poll `SCE_NET_INET_POLLOUT` in bounded intervals; honor cancellation and an
   overall deadline. Check poll error bits and `stm_getsockopt(..., SO_ERROR, ...)`
   before treating it as connected. Never wait indefinitely on the UI thread.
5. For USB HTTPS, TLS is already established by the ESP before the socket becomes
   ready. `stm_tls_open` is only a dispatch/check wrapper for virtual descriptors;
   it does not change an already-open plaintext socket into a TLS socket.
6. Send and receive with partial-I/O handling. `stm_send` accepts at most one
   wire payload per call; advance by the returned count, not the requested count.
   `stm_recv` may return cached data in smaller chunks. Neither preserves HTTP
   message boundaries. Parse HTTP status, headers and body framing yourself.
7. On every exit path, the owner calls `stm_close(fd)`, then
   `stm_thread_finished()` before exiting the thread.

`stm_send` and `stm_recv` currently return `size_t`, matching the existing
adapter. Convert to `int` before checking for negative results as PSPStreamer
does; otherwise `(size_t)-1` looks like a huge successful transfer.

| Result | Meaning / handling |
| --- | --- |
| Receive `>0` | Bytes delivered |
| Receive `0` | End of stream, not temporarily empty |
| Receive/send negative, `stm_errno()==35` | Would block; poll/retry within your deadline |
| Other negative result | Fail/recover the connection; do not silently discard it |
| `stm_tls_recv()==-2` | Wrapper timeout/would-block; decide whether to retry |

USB error state is stored per PSP thread; read `stm_errno()`, not libc `errno`.
The current option shim implements `SO_NONBLOCK` and `SO_ERROR`, **not** arbitrary
POSIX socket options. Unsupported setters currently return success without
applying the option. Explicitly implement any other option your program relies
on. Do not pass a virtual descriptor to native `close`, `shutdown`, `select` or
an unadapted library. The redirect header does not magically cover those APIs.

## Ownership, disconnects and shutdown

- One owner per socket. Different sockets may run concurrently; simultaneous
  `recv` and `close` on the same descriptor are not supported.
- Request cancellation, let bounded I/O unwind, join the worker, and only then
  reclaim its state. Do not forcibly kill a thread owning USB requests or DMA.
- `stm_driver_cancel()` is a **global** transport cancellation, not a per-socket
  shutdown. It invalidates all active virtual connections.
- After all owners have stopped and closed, `stm_associate(&running, 1)` can reset
  stale ESP channels and reconnect. Open new sockets afterward. Replaying HTTP,
  retrying non-idempotent requests, Range resume or seeking a media stream is the
  responsibility of your application; the adapter cannot infer a safe policy.
- At app exit, join all workers, release their thread-error slots, then call
  `stm_driver_stop()`. Native Wi-Fi workers also need their normal PSP network
  bookkeeping cleanup; `stm_thread_finished()` does not replace that.
- Keep USB waits out of audio callbacks and render loops. Use producer/consumer
  buffers and avoid high-frequency remote/status polling that competes with data.

## Wi-Fi setup without PSPStreamer's UI

Provision Onju once using PSPStreamer, or implement your own setup screen using
`stm_driver_start()` and `stm_rpc()`. These operations are worker tasks too.
`SM_CONFIG_GET/SET` exchange `SmConfig`; `SM_SCAN` returns `SmScan`;
`SM_NETWORK_INFO` returns the current `SmNetworkInfo`; `SM_CONNECT` starts
association. Check both the result and exact response length before decoding.
Validate with `sm_config_valid`, use `SM_CFG_KEEP_PASSWORD` only when intended,
and wipe temporary password buffers after use. A successful SET is not proof
that DHCP/association has finished. Refresh network status separately.

Configuration lives in Onju NVS. Display the live DHCP lease instead of stale
static configuration fields. Firmware security caveats, including unencrypted
NVS and the trusted TLS certificate bundle, are in the [firmware guide](README.md).
USB TLS does not inherit PSPStreamer's native PSP certificate-pinning database.

## Diagnosis and fair speed comparisons

### Bulk protocol and advanced comparisons

Firmware 0.2.4 adds `SM_CAP_BULK_PAIR`. The app enables it only when the kernel
bridge also advertises `SM_DEV_BULK_CAPS`. Two independent socket-read requests
are queued, each allowing an 8192-byte frame (8160 payload bytes), with consecutive
sequence numbers. The ESP processes commands FIFO and accepts request B while
response A is in flight. A control RPC drains the group into its owning socket's
mailbox first; tokens/generations keep connections isolated. The kernel validates
both wire lengths and checksums before the app accepts the result. Successful bytes
precede EOF/socket errors, which remain observable on the next read.

Requests and DMA buffers remain kernel-owned until callbacks complete. No user
pointer is retained asynchronously. The result is copied directly into the socket
mailbox and the app swaps buffer indices, avoiding another payload copy. Each
local socket slot now reserves two 16328-byte result buffers (about 383 KiB total
for 12 slots). Reads below 1024 bytes do not initiate look-ahead. Old firmware uses
the previous 4 KiB asynchronous route; old kernel bridges fall back to synchronous
operation. Update firmware and all three PSP binaries for the new pair mode.

Firmware 0.2.5 adds `SM_CAP_BULK_EXT` and `SM_CAP_USB_METRICS` (total capabilities
15). Check `SM_DEV_BULK_EXT_CAPS` as well before sending `SM_SOCKET_READ_BULK_EXT`.
For the local `SM_DEV_BULK_BEGIN` ioctl, the request header's `result` supplies the
group depth (1/2/4); the driver clears it before sending consecutive requests.
`SmSocketRequest.length` supplies each payload limit. Maximum frame size is 32 KiB;
groups may total no more than `SM_MAX_GROUP_PAYLOAD`. The driver waits for each
32 KiB receive or a short packet; use `sm_bulk_wire_size_op` (not the legacy helper)
to terminate replies at 8/16 KiB boundaries too. Checksums cover only header/payload.
`SM_SOCKET_READ_BULK` and `SM_LEGACY_RESULT_SIZE` retain the old 8 KiB/two ABI.
`SM_DEV_BULK_FINISH` must receive the appropriate old or extended output capacity.

The new app reserves two extended result buffers per slot (~1.5 MiB across 12
slots) and the kernel has four fixed send/receive pairs (~144 KiB). USB DMA never
targets an asynchronously retained user pointer. The ESP uses a four-entry command
queue, two reply slots and an initially 8 KiB host transfer buffer. No DMA buffers
are freed/reused before completion on cancellation/detach. Larger experimental
allocations may cost throughput; measure instead of assuming bigger is faster.

Use firmware 0.2.7 or newer. 0.2.5 allocated large queues/workspaces internally
and failed USB attachment; 0.2.6 moved them to PSRAM but regressed throughput.
0.2.7 uses two fixed internal 8 KiB reply workspaces (with 64 bytes of padding
capacity) and pointer-only queues. The single network worker obtains a free slot,
publishes it, and must not touch it again until returned. The USB owner copies
only wire bytes into its separate DMA buffer before returning the slot. Detach
drains ready pointers, never resets the ownership/free queue, and leaves worker-
owned slots alone; stale worker results release their own slots.

Larger extended requests allocate/grow scratch storage in PSRAM only on demand;
it is bounded to two slots and retained for reuse until reboot. The DMA transfer
buffer starts at 8 KiB and grows only with a larger reply, after the preceding
transfer has completed. A later small request always selects its internal
workspace, even if extended scratch storage exists. Default startup requires
no extended reply storage. The command queue remains a bounded internal FIFO;
network receive rings remain in PSRAM. Never assume `xQueueCreate` selects PSRAM.

`stm_tuning(kib, depth)` before initialization selects the comparison profile;
do not change tuning concurrently with I/O. Defaults remain 8×2 legacy bulk.
`stm_usb_metrics()` retrieves boot-cumulative bulk timing counters only when
debugging and the capability are enabled. The app brackets a download with two
queries; see [counter interpretation](../docs/TRANSPORT_DOWNLOAD_COMPARISON.md).

`stm_diagnostic_snapshot(buffer, capacity, 0)` formats USB timing/rate counters;
passing `1` formats sampled ESP receive-buffer counters. A zero return means no
snapshot is available. Write these infrequently from your own diagnostic/UI
worker, outside I/O locks. Counters are cumulative and aggregate all sockets;
the displayed session-average KiB/s includes idle time. See
[measurement details](README.md#transport-measurements).

`bulk=1` indicates negotiated pairs. Diagnostics also report `caps`, `probe`,
`len` and `bridge`: firmware 0.2.4/driver should return `caps=3 probe=0 len=4
bridge=1`. `caps=1` indicates compact-only firmware; a negative bridge result
indicates an unsupported or failed local driver query. A September 27 flash audit
found 0.2.3 still installed despite a 0.2.4 factory file on the PC: verify the
actual flashed image rather than inferring firmware version from the file name.
`ahead` counts collected speculative groups
(including empty replies; a group has two requests in bulk mode). For these
reads, USB timing records the blocking finish wait only: DMA can already have
completed while the caller was working. Do not compare that timing with the old
fully synchronous USB duration as if it measured physical bus occupancy. Use file
byte counts and elapsed download time to evaluate the speedup.

The setup UI's echo test is an integrity/USB round-trip test, **not** a file-download
benchmark. Compare O2/O3, direct cable/hub and native PSP Wi-Fi with the same
server, HTTP or HTTPS choice, file and storage card. Keep power and radio
conditions comparable; record repeated runs, bytes and complete elapsed time.
A direct cable may remove a source of overhead or power/connection trouble, but
no speedup has been established yet. Keep a powered-host arrangement that safely
supplies VBUS even without the hub.

Minimum release checklist: startup with/without adapter, DNS hostname access,
partial reads/writes, six-channel contention, long playback/download, user cancel,
USB unplug/replug, AP loss, bad TLS certificate, Stop/app exit, then native Wi-Fi
fallback. Host harnesses in `tests/test_streammaster.py` exercise protocol and
ownership logic but cannot certify real USB scheduling or hardware stability.

## Redistribution

The StreamMaster transport/driver sources are marked **GPL-2.0-or-later**. Preserve
copyright, SPDX identifiers and notices, and comply with the applicable source
distribution requirements when distributing derived binaries. This is not a
BSD-licensed drop-in. Review compatibility with your application's license and
retain third-party notices, including the bridge's PSPLINK attribution and the
ESP-IDF/vendor license bundle. See [LICENSE](../LICENSE),
[PSPLINK-license.txt](PSPLINK-license.txt) and the packaged `licenses/` directory.
