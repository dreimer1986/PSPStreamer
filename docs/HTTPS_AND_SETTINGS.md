# On-device settings, Docker passwords and optional HTTPS

## PSP controls

Press **Select in the library** (not during playback) to open Settings.
Up/Down selects a row; hold to repeat. Left/Right adjusts values. Cross opens
the text/number editor. Start saves; Circle cancels every pending change.
The editor uses arrows to select a character, Cross to append it, L to delete
the last character, R to clear, Start to accept the field, Circle to discard
that field. It includes printable ASCII and äöüÄÖÜß. Password fields are masked.
Fields support the same limits as the config; password limit is 128 UTF-8
bytes, not 128 arbitrary Unicode characters. Space appears as `_` in the
keyboard grid; the actual underscore key is separate.

Available fields cover every current config key: hostname, port, password,
HTTP/HTTPS, English/German, TV UI at startup, audio/subtitle track indices,
quality, fps, volume, shuffle, preset filename, automation mode, interval and
fade duration. Use the existing Circle preset browser to select filenames
without typing. TV startup behavior takes effect at the next launch; changing
it does not reconfigure an active display. Settings are never edited while
media workers are running, so live clocks/codec state remain untouched.

Saving uses a checked temporary file and retains the previous config as
`ms0:/PSP/SYSTEM/PSPStreamer.cfg.bak`. A failed replacement attempts rollback
and cancels the in-memory changes. Both current and backup configs can contain
plaintext passwords: do not share them. Connection changes clear DNS, old
library/resume state and remote sequence state before reloading the library.

## Docker WebUI password

Rebuild/recreate the container, then expand **Server settings** below the
remote controls. Enter the current password (empty for initial setup), the
new password twice, and Save. The next authenticated request may require a new
browser login: username `psp`, new password. Update the PSP's Password field too.
Existing streams need not stop, but subsequent requests use the new password.

Compose mounts a named `streamer-settings` volume at `/data` and sets
`PSP_STREAMER_SETTINGS_DIR=/data`. A salted PBKDF2 verifier is stored as
`/data/password.json`; the plaintext password is not returned or persisted by
the server. A saved password overrides `PSP_STREAMER_PASSWORD`, which is the
bootstrap value only. Do not use `docker compose down -v` if you want to keep it.
Without a settings directory, password management remains environment-only.
To recover a forgotten password, stop the server, back up/remove that specific
verifier file and set a new bootstrap password before restarting.

Initial setup is intentionally simple: an unprotected instance allows setting
its first password. Do this on a trusted network **before** public exposure.
Changes require same-origin JSON and the current password once configured.
HTTP sends the password unencrypted; use browser HTTPS or a trusted tunnel.

Home Assistant uses the same server/WebUI but continues to manage its password
in the HA app options. The WebUI says so instead of offering a conflicting
password editor. Version 0.1.26 includes the matching UI and optional TLS paths.

## HTTPS endpoint

HTTP remains the default. Choose one protocol per server listener. For native
HTTPS set both `PSP_STREAMER_TLS_CERT` and `PSP_STREAMER_TLS_KEY` to readable PEM
files inside the container. They may be a normal CA-issued certificate/chain
and private key or a self-signed pair. Incomplete/invalid TLS configuration
fails startup rather than opening an unexpected plaintext service.

Example Compose environment (mount the certificate directory separately):

```yaml
services:
  psp-streamer:
    environment:
      PSP_STREAMER_TLS_CERT: /certificates/fullchain.pem
      PSP_STREAMER_TLS_KEY: /certificates/privkey.pem
    volumes:
      - /absolute/path/to/certificates:/certificates:ro
```

The existing port can remain 8091. Open `https://your-host:8091/`, enable HTTPS
in PSP Settings, and enter the same host/port. For a self-signed browser endpoint
you must handle the browser's own trust warning; the PSP's policy does not alter
browser trust. Alternatively terminate TLS at a reverse proxy and keep the
HTTP backend private; point the PSP at the proxy's HTTPS host/port. Preserve
Authorization/Host, avoid response buffering and allow long-running streams.

HA exposes `/ssl` read-only. Its new `tls_cert` and `tls_key` options can be
`/ssl/fullchain.pem` and `/ssl/privkey.pem`; leave **both empty** for HTTP.
Restart the server/HA app after native certificate renewal; the next new PSP
connection sees the newly loaded certificate. Proxy renewal is managed by the
proxy. No automatic certificate issuance is included.

## Certificate behavior chosen for this project

Every new HTTPS connection performs a TLS handshake. The actual leaf certificate
is compared with the previously stored certificate for that hostname and port.
First use is saved automatically; a replacement is also saved automatically,
without confirmation. The previous DER is retained as `.bak` in the app's
`certificates/` directory. Notices for first use/change/storage failure remain
visible in the library until Square reload/acknowledgment. A change during
playback remains pending until you return to the library. A RAM fingerprint
cache avoids rereading/writing the Memory Stick for every identical certificate.

This deliberately **does not authenticate the server**: root trust, hostname
and expiry checks are not enforced. An active intermediary can provide its own
certificate, trigger a notice, and receive the password/content. HTTP remains
plaintext. HTTPS errors never cause an automatic fallback to HTTP.

The client uses the installed PSP mbedTLS 2.28.10 libraries (TLS 1.2), with
per-connection RNG/state, nonblocking socket callbacks and cancellable polling.
Entropy uses ARK's KIRK-backed `sctrlKernelRand`, not the toolchain's time-seeded
MT19937 `getentropy`. No codec bridge or MPEG initialization order was changed.
HTTPS remote workers run below the DAC priority; their handshake budget is
separate from media/subtitle preparation. Hardware TLS throughput and audible
behavior still need a real PSP test. This build does not claim a security audit
or production hardening of the old TLS dependency.

## Verification

The regular tests cover password authentication/persistence/validation, failed
writes, CSRF rejection, native server TLS, settings cancel/save failure and UTF-8
backspace. Extra real TLS tests compile the actual PSP transport with host
socket/file shims and mbedTLS 2.28.10: first certificate, unchanged certificate,
rotation, failed certificate storage, decrypted buffered reads, timeout and
cancellation. Set `MBEDTLS_HOST_SOURCE` to that source tree and
`MBEDTLS_HOST_BUILD` to its CMake build to enable those tests. Default runs skip
them when the matching host library is not supplied.

Docker/HA Python sources and WebUI assets are compared byte-for-byte in tests.
Their OS package versions still differ; runtime encoder/font output is not
claimed byte-identical.
