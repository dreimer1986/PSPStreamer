# Cave flight Easter egg — spaceship asset

“Low Poly Spaceships” by **Samuel Metters**:
https://sketchfab.com/3d-models/low-poly-spaceships-9177fe4356e4451485dc6129c9904eb9

Author: https://sketchfab.com/samuelmetters

License: **Creative Commons Attribution 4.0 International**:
https://creativecommons.org/licenses/by/4.0/

The user supplied `Sketchfab_Scene.glb`, whose embedded asset metadata identifies
the author, work, source and license above. The copy here is `cave_ship.glb`.
The asset retains its CC BY 4.0 license independently of the application's GPL.

Changes for PSP: baked scene transforms, normalized/reoriented coordinates,
simple baked vertex lighting in place of PBR materials, and conversion to a C
triangle array. No texture or animation was present in the supplied file.

Rebuild the generated mesh from the repository root:

```sh
python3 tools/convert_cave_ship.py psp-client/assets/cave_ship.glb psp-client/cave_ship_data.h
```
