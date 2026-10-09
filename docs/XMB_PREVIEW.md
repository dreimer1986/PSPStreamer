# Cyber-Stream XMB preview

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
