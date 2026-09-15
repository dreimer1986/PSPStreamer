# On-device settings, deployment parity and HTTPS review

Status: design review, not an implemented settings screen or TLS client.

## Settings without a PC

The config reader supports server, port, server_password, audio, subtitle,
quality, video_fps, volume, shuffle, language, tv_ui, music_preset,
preset_auto, preset_seconds and preset_fade_ms.
Playback menus already expose track/subtitle/quality/frame-rate preferences,
volume/shuffle and preset selection. Automation exposes a subset of intervals.
There is no general settings page or on-screen keyboard path for connection
credentials. Server/port/password, language, startup TV behavior, arbitrary
allowed preset intervals and fade duration need explicit settings controls.

Proposed implementation: a main-menu Settings entry, grouped Connection /
Display and language / Playback / Visualization pages, PSP on-screen keyboard
for host/password, masked password display, and staged Apply/Cancel changes.
Connection changes should stop the current session, invalidate remote commands
and reconnect deliberately, not change a live socket's credentials. Validate
lengths, port/ranges and URL scheme before saving; use checked temporary-file
writes and a recoverable replacement instead of truncating the only config.

## Web password and deployment parity

Today both servers read PSP_STREAMER_PASSWORD at startup, and use HTTP Basic
with username psp. Neither has a password-editing endpoint or persistent
settings volume. A Docker web editor needs persistent configuration, explicit
precedence relative to environment variables, authentication with the current
password, CSRF/origin protection, bounded requests and atomic storage of a
salted password verifier. Never return the password in the settings response.
An unconfigured public server must not allow an arbitrary visitor to claim it:
initial setup needs an out-of-band setup token or explicit local provisioning.
Password changes invalidate existing credentials, including the PSP's saved
password; make this clear before confirmation. Protect setup and changes with
TLS (or a trusted private path) rather than suggesting Basic is encryption.

HA should continue managing its password through the Supervisor option, with
the WebUI displaying that it is externally managed. Silently changing an
in-memory password would be lost on restart and conflict with HA configuration.
Both deployments should share the media/API implementation; configuration
ownership can intentionally differ.

The source review found equal server logic and only comment/no-op differences
in PGS code and an unused JavaScript argument. Those copies are now identical,
with a parity regression test. Docker lacked mkvtoolnix, used for MKV PGS
extraction; it now installs it like HA. Both install FFmpeg/fontconfig/fonts.
Different distributions/package versions can still affect encoder/font output;
no newly built container image or live HA deployment was tested in this review.
HA currently targets amd64 and has Supervisor-managed media/config; ordinary
Docker uses host bind mounts. These are deployment differences, not media UI
features. No HA release bump is needed for comment/no-op alignment alone.

## Optional HTTPS

Current PSP requests use plaintext sockets across metadata, remote polling,
subtitles and media workers. Merely accepting an https:// prefix is unsafe and
insufficient. Introduce one cancellable HTTP/TLS transport shared by all these
paths; retain explicit HTTP mode for trusted LAN use, never silently downgrade
HTTPS after an error. TLS memory, entropy, PSP clock accuracy, handshake CPU
cost and concurrent connections need measurement on hardware. The installed
PSPSDK pspssl.h exposes initialization/memory calls, not proof of a usable
modern verified transport. A maintained portable TLS library is a candidate,
not yet a tested dependency for this project.

At each new full TLS connection the server already supplies its certificate
chain. Validate hostname, validity period and trust chain before sending the
password or accepting media. A small bundled CA set avoids relying on unknown
firmware roots. A correctly CA-signed renewal should work without manual leaf
certificate replacement. Session resumption needs bounded trust lifetime and
invalidation after a trust/config change.

For self-signed/private deployments, offer explicit first-use fingerprint
verification against a separately trusted server display, then store a
host-bound trust anchor/pin. A changed untrusted key must stop with a clear
confirmation path; do not download and automatically trust its replacement.
Fetching a certificate from the same untrusted connection cannot authenticate
that connection. Root/key rotation requires deliberate trust provisioning.
First-use acceptance without comparison remains vulnerable to interception.

Server-side TLS can terminate at a reverse proxy with certificate renewal;
the backend HTTP port must remain private. This secures PSP traffic only once
the PSP itself connects via TLS, not while it still uses a public HTTP port.
Reference: [Mbed TLS verification and hostname API](https://mbed-tls.readthedocs.io/projects/api/en/v3.6.3/api/file/ssl_8h/).
