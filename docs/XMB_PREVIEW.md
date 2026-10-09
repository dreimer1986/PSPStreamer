# Cyber-Stream XMB preview

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
