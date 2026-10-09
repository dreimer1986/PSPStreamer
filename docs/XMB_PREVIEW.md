# Cyber-Stream XMB preview

## Native XMB package — 2026-10-09

The user approved revision 3. The current EBOOT includes the new static ICON0
and PIC1, animated ICON1.PMF and stereo SND0.AT3. No plugin is required.
**PSP XMB hardware validation remains pending.** The following older preview
sections record the design process; their earlier "not installed" statements
no longer describe the current package.

- ICON1: 144×80, 240 frames at 30000/1001, Main/CABAC, one reference, no B-frames.
- PSMF: 362,496 bytes; AU/index round-trip and all 240 decoded frames checked.
- SND0: ATRAC3 LP4, 44.1 kHz stereo, 66,960 bytes, RIFF fact/smpl loop metadata.
- Combined media: 429,456 bytes (419.4 KiB), below the 500 KiB packaging budget.
- Repack compares executable, SFO, PIC0 and PSAR byte-for-byte with the donor.
- Static PIC1 stays usable without Consolizer. The separate animated-background
  request remains [an open rendering-hook investigation](XMB_BACKGROUND.md).

Reproduce with the MIT toolkit snapshot in `tools/vendor/psp-media-toolkit/`:

```sh
python3 tools/render_xmb_flyby.py --output /path/to/preview
python3 tools/vendor/psp-media-toolkit/pspmedia.py convert /path/to/preview/Cyberpunk-Icon-Flyby.mp4 -o psp-client/assets/icon1.pmf --budget-kb 360
python3 tools/vendor/psp-media-toolkit/pspmedia.py snd0 /path/to/preview/Cyberpunk-Flyby-Sound.wav -o psp-client/assets/snd0.at3 --encoder atracdenc
python3 tools/package_xmb.py /path/to/previous/EBOOT.PBP /path/to/new/EBOOT.PBP
```

The native encoder was built from the user-provided atracdenc 0.2.3 source
archive with CMake, libsndfile and `-DCMAKE_CXX_FLAGS='-include cstdint'` for GCC
16's stricter header dependencies. Native source compilation required no Wine.
Put its `src/atracdenc` binary on PATH for the sound command. Encoder binaries
are not redistributed. Native artwork is in `psp-client/assets/icon0.png` and
`pic1.png`; the Makefile includes all four assets for future builds.

Only packaging/media checks were run for this change, not the playback suite.
On PSP: highlight the entry, observe two full loops, check sound, then start and
exit the app once. Repeat with Consolizer disabled to confirm independence.

## Cyberpunk revision 3: flyby

`python3 tools/render_xmb_flyby.py --output ~/Bilder/PSPStreamer-XMB/Cyberpunk-v3`
preserves the approved v2 scene and adds a detailed ship emerging from the display
and passing leftwards across the foreground. This is an artistic Easter Egg homage,
not a render of the runtime ship mesh. The high-resolution transparent sprite is
filtered with Lanczos and gently banked during the flyby. The original synthetic
stereo score now has layered bass impacts, an activation arpeggio, a synchronized
right-to-left engine/whoosh with falling pitch, and a reverberant final shimmer.
PCM peaks are limited to 0.88 full scale; start/end fades avoid loop-boundary clicks.
No commercial sound samples are used. V2 files and installed EBOOT remain intact.

Ship asset: `psp-client/assets/xmb-cyberpunk-concept/flyby-ship.png`.
Generated using the built-in imagegen tool (not CLI), with this prompt:

> Use case: stylized-concept. Production asset: transparent spacecraft cutout for
> an animated cyberpunk PSP media-player ident. A single highly detailed sleek
> silver graphite interceptor, swept angular wings, dark glass canopy, intricate
> fine mechanical panels, four small cyan plasma engine outlets with short
> luminous cyan-magenta exhaust. Cinematic premium realistic 3D rendering, smooth
> antialiased silhouette, cyan and magenta city-light reflections, not pixel art
> and not low-poly. Three-quarter top/side view, nose points LEFT and slightly
> down, rear engines visible at RIGHT. Entire ship and short exhaust fully within
> frame with generous transparent margins. No stars, no background, no ground
> shadow, no lettering, no logo, no border. Compact wide silhouette, polished
> futuristic mysterious heroic mood. True transparent RGBA background.

## Cyberpunk revision 2

The first concept was too static. Revision 2 uses built-in image generation for
four matching production assets in `psp-client/assets/xmb-cyberpunk-concept/`:
new icon art, a matching full-screen background, a city panorama and a clean
animation plate with the film removed and a blank PSP display. Original installed
ICON0/PIC1 files are untouched. The icon and backdrop are previews, not approved
replacements for the PBP build yet.

`tools/render_xmb_cyberpunk.py --output ~/Bilder/PSPStreamer-XMB/Cyberpunk-v2`
renders a changing curved film ribbon with traveling frames/perforations and a
PSP screen crossfade from the icon/play motif into a panning cyberpunk city.
The second silent video is a city-wallpaper motion study with camera pan, rain
and light traffic, NOT proof of animated PIC1 support or the finished backdrop
composition. Animation is deterministic compositing, not generative video.

The user explicitly requested an animated XMB background as a later follow-up.
Keep this separate from native EBOOT packaging: PIC1 is a still PNG. Investigate
VSH/plugin/theme feasibility, RAM, boot audio and stability before implementing
any new hook. Background animation source art is prepared now; no VSH code changes.

### Image generation prompt set (built-in tool, not CLI)

1. Reference original PIC1: redesign as cinematic 16:9 cyberpunk key art;
   glossy black PSP bottom right, curved perforated film entering from upper left,
   dense cyan/magenta night city with amber windows and rain reflections on both
   screen and film; dark navy negative space for XMB; no title or watermark.
2. Reference original ICON0 and new PIC1: matching wide thumbnail artwork;
   front-on horizontal PSP on the right, frontal rectangular city screen,
   curved luminous city-film ribbon entering from left; recognizable silhouette,
   polished black hardware and cyan/violet rim light; no text or watermark.
3. New panorama: cinematic elevated cyberpunk riverfront, luminous skyscrapers,
   layered bridges, amber windows, reflective water, distant flying vehicles,
   atmospheric depth and interesting detail across the width; no PSP or film.
4. Edit new icon into animation plate: preserve canvas, camera and PSP geometry;
   remove all film, particles and incoming rays, fill matching navy background;
   replace only the display with near-black; retain hardware and floor lighting.

Generated art was copied into the repository, and master/native-size previews
are in the above Bilder folder. Production rendering code is versioned alongside
the image assets. PMF/ATRAC conversion remains a separate pending step.

Original eight-second cyan/violet animation and synthesized A-minor sound,
based on the project's existing 144x80 `icon0.png`. An energy stream moves
along the film into the PSP screen; a soft power-up accent peaks at two seconds.
The background artwork, text and console silhouette stay intact.

Render the editable concept with:

```sh
python3 tools/render_xmb_preview.py --output ~/Bilder/PSPStreamer-XMB
```

Dependencies: Python, NumPy, Pillow and FFmpeg with libx264/AAC. The MP4 preview
is four times the icon dimensions (576x320), 30000/1001 fps, 240 frames.
The WAV is original 44.1 kHz stereo PCM. No external music or Sony sound samples.
The motion loops periodically except for the deliberate once-per-loop power-up
accent; audio has short edge fades rather than an authored ATRAC loop chunk.

## Not installed in EBOOT yet

MP4/WAV are review files, **not** `ICON1.PMF` / `SND0.AT3`. The current EBOOT
and static icon/background are unchanged. Do not rename the preview files to
PSP formats. A plain H.264 encode is not a complete PSMF mux.

Candidate tools to evaluate when network access is available:

- https://github.com/TotalKommando/psp-media-toolkit — PMF/AT3 tooling and XMB validation.
- https://github.com/dcherednik/atracdenc — open-source ATRAC encoder.

Shell GitHub access failed with DNS resolution in this session. Neither tool
was downloaded or executed. PMF mux, ATRAC loop metadata, size limits and real
XMB playback must be checked before updating the PBP build. Specifications in
the supplied brainstorming text are not treated as verified format guarantees.

Next: approve the appearance/sound, encode at native icon size, validate the
resulting PSP assets, then integrate optional packaging with the static fallback.
