# Optional animated VSH background — investigation, not shipped

The native EBOOT now has ICON0.PNG, ICON1.PMF, PIC1.PNG and SND0.AT3.
It starts normally without PSP Consolizer. ICON1 is the animated icon; PIC1 is
the static full-screen fallback. These are independent of a future extension.

## Required implementation boundary

The requested extension belongs in Consolizer, is optional and loads animation
data from a separate Memory Stick file. Missing/invalid data or a disabled plugin
must leave the ordinary static XMB presentation and app launch untouched.
No flash0 changes, replacement VSH or mandatory custom theme.

The existing Consolizer present hook in `psp-controller/overlay.h` sees the
already-composited framebuffer. It can add status text **over** the XMB, but it
cannot recover the layers behind icons/text. Painting a whole movie here would
obscure the menu. Comparing framebuffer colors with PIC1 is not a reliable mask:
XMB fades, antialiased text, dimming and TV scaling alter those pixels.

A proper implementation needs a verified VSH/PAF background-texture update or
pre-menu composition point, plus identification of the selected PSPStreamer
entry. No verified mapping for the current 6.61/ARK setup has been established.
An independent animated overlay is not proof of an animated PIC1 solution.

## Next hardware-facing step

Identify and observe that texture/lifecycle before writing to it. Only then add
the optional renderer: delayed VSH initialization, bounded non-kernel asset
storage, no I/O/decoding inside a display callback, clean stop on app launch,
USB-storage hand-off and suspend, and coexistence with FuSa/status overlays.
Verify on LCD and TV, including cold boot and return from a game. Do not revive
the previously rejected early VSH initialization path just for visual effects.

The city panorama and silent motion study are prepared in the XMB concept assets
and `~/Bilder/PSPStreamer-XMB/Cyberpunk-v2/`. No new background INI option or
background renderer is shipped yet; the preview MP4 is not a PSP plugin asset.

References inspected:

- [PSP media toolkit](https://github.com/TotalKommando/psp-media-toolkit): native icon and sound packaging.
- [XMBuddy](https://github.com/StefanTsonev/XMBuddy): explicitly a post-composition overlay, not a background replacement.
