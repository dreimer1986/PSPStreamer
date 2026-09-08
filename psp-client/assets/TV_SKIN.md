# Native TV receiver artwork

`menu_skin_tv.png` is the 720×480 production raster. `menu_skin_tv.raw` is its
RGBA byte representation, embedded via `menu_skin.S`. Neither is made by
upscaling the LCD skin, and there is no GUI-image scaler in the PSP runtime.

Created with the built-in image generation tool, using `menu_skin.png` only
as a style reference. The higher-resolution generated master was reduced to
the native TV raster during asset preparation. A targeted image-tool edit
adjusted the volume dial for anamorphic TV presentation. The images are not
imported from Winamp, MilkDrop or PMPlayer Advance.

Generation brief:

> New production TV receiver background, warm wooden cabinet and photoreal
> black brushed metal. Straight-on view, no perspective. Blank main display,
> separate right information display, thin help strip, two amber analogue
> meter windows without needles, five black buttons with cyan indicator
> slits, and a black concentric volume dial without a position marker. No
> text, logos or watermarks. Preserve blank areas for native code-rendered
> text. Create a spacious new design, not an upscale of the LCD image.

Final edit brief:

> Keep all panels and controls fixed. Narrow only the lower-right dial to
> approximately 84.375% of its height for anamorphic 720×480 displayed at
> 16:9; retain the original centre and blend exposed brushed metal edges.

The actual UI labels, selection highlights, meter needles, colour indicators,
volume marker and music spectrum are rendered by `tv_gui.h`, not baked text.
Artwork anchors are native TV pixel coordinates. Physical TV/OSSC validation
is still required before declaring their final calibration complete.
