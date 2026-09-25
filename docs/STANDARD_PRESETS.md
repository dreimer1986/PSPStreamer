# Shaderless starter pack

The 2026-09-25 local release selects 51 lightweight candidates from the supplied
`favorite_presets_2021_01_03` collection. The limit is 128, not a quota: filling
the remaining slots with expensive/shader-dependent presets defeats the aim.

The builder rejects HLSL, external textures, EEL memory/loops, large custom waves,
heavy per-pixel code and excessive shape instances. Static formula cost is only
a heuristic, not measured PSP frame time. `fShader` fixed-function shading is
allowed. All selected files are unchanged, including author names. `active.milk`
is one original under the app's default filename; its original name and digest
are in the manifest. There are no extra copies or test/demo presets in this pack.

```sh
python3 tools/build_standard_presets.py /path/to/your/collection /empty/output
python3 tools/check_milkdrop_collection.py /empty/output --frames 120 --expanded-shapes
```

Result for the selected pack: **51/51 load, 51/51 complete 120 evaluation frames,
0 resource-limit flags**. This checks formulas/geometry, not GE rendering, visual
appeal or 333 MHz FPS. Test `Geiss - Cosmic Dust 2`, `Geiss - Swirlie 4`,
`Rovastar - Hyperspace`, `Krash - Vinyl Disk` and `Geiss - Trampoline` on hardware.
Higher feedback resolution and live transitions remain user choices.

`standard-presets.json` records the exact local selection and SHA-256 provenance.
The original files have no verified blanket redistribution grant in the supplied
collection. Therefore Git contains the builder/manifest rather than republishing
third-party presets under this project's GPL. The locally generated release pack
is ready to copy to your PSP; verify permission before publishing those assets.

Old test presets remain in source control and the previous release preset folders
are moved to an external archive. To avoid merging the old tests back in, move
the PSP's existing `presets` folder aside before copying the replacement. Do not
delete personal presets. If the configured selection is now missing, the player
falls back to `active.milk`. No configuration or memory-stick contents are deleted
by this fallback.
