# ForeverTAS replacement textures

The physical materials are resized from photo-scanned CC0 Poly Haven PBR sets
and an ambientCG foliage atlas. The game-specific materials are built-in
ImageGen outputs created expressly for this renderer. No texture pixels are
extracted from TrackMania.

Every shipped texture is a 512 x 512 PNG. The CC0 materials retain their paired
OpenGL normal maps; the foliage base also retains its authored opacity map.
Normal maps for the ImageGen albedos are derived with the tile-safe
central-difference tool in `tools/derive_normal_map.py`.

Concrete is the single deliberate exception to the detailed material set: its
base color and normal are uniform by art direction, without white aggregate
spots. See `PROVENANCE.md` for the source and prompt used for every material.
