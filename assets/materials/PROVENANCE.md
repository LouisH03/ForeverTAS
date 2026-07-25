# Replacement material provenance

All Poly Haven assets below are CC0 and were downloaded through the official
Poly Haven API at 1K, then resized to the committed 512 x 512 PNGs. The asset
library publishes photo-scanned, seamless PBR material sets with OpenGL normal
maps.

| ForeverTAS material | Source | Imported maps |
| --- | --- | --- |
| Asphalt | [Asphalt Track](https://polyhaven.com/a/asphalt_track) | Diffuse, `nor_gl` |
| Dirt | [Dirt](https://polyhaven.com/a/dirt) | Diffuse, `nor_gl` |
| Metal | [Metal Plate 02](https://polyhaven.com/a/metal_plate_02) | Diffuse, `nor_gl` |
| Painted metal | [Blue Metal Plate](https://polyhaven.com/a/blue_metal_plate) | Diffuse, `nor_gl` |
| Rubber | [Rubber Tiles](https://polyhaven.com/a/rubber_tiles) | Diffuse, `nor_gl` |

License: [Poly Haven CC0](https://polyhaven.com/license).

Grass ground cover uses ambientCG's CC0
[Grass 001](https://ambientcg.com/view?id=Grass001) Color and NormalGL maps.
Grass blade cards use its CC0
[Foliage 001](https://ambientcg.com/view?id=Foliage001) atlas; the Color,
NormalGL, and Opacity maps are combined into the committed alpha-masked
`grass_foliage_base.png` and `grass_foliage_normal.png`. Keeping these separate
prevents the grass bordering dirt blocks from becoming transparent while
removing the pale rectangles around individual blades.

License: [ambientCG CC0](https://ambientcg.com/license).

## ImageGen prompt set

The remaining albedos were generated with built-in ImageGen in
`stylized-concept` mode. Every prompt requested a square, orthographic,
edge-to-edge, game-ready material texture at 1024 x 1024 or larger, without
perspective, objects, borders, isolated dots, polka dots, text, logos,
watermarks, baked directional light, or cast shadows.

| Material | Primary prompt | Local ImageGen source |
| --- | --- | --- |
| Plastic | Light-gray injection-molded engineering plastic with fine grain, manufacturing variation, and restrained scuffs; explicitly not concrete, paint, or metal. | `call_GJt9oj5qASrcHaHmy2PvpD9C.png` |
| Neutral | Medium-gray molded stadium mineral composite with subtle woven microstructure, broad manufacturing waves, and quiet wear. | `call_blG17INiFU6iKziuuRfRfeFh.png` |
| Unknown | Charcoal diagnostic technical composite with coherent, large-scale magenta angular circuit and hazard motifs. | `call_VXEV99VDz8hqNisySKX18mcy.png` |
| Signage | Weathered off-white stadium sign panel with abstract deep-red and cobalt racing graphics, printed-ink wear, and no readable brands. | `call_MWGaWCRd7WigyrlCSvo0gRRm.png` |
| Emissive | Graphite technical panel with recessed mint-cyan electroluminescent racing-circuit channels and no broad glow halo. | `call_d0GrXrX6HUIuB6IHoBkJkDh0.png` |
| Turbo | Dark-teal anti-slip turbo pad with exactly two unmistakable right-facing chevrons, cyan followed by yellow, and no competing directions. | `call_J1nNFuLNXUWDyhUTSLoBenii.png` |
| Checkpoint | Cobalt industrial checkpoint panel with broad horizontal off-white and vivid-red identification bands, inset seams, and racing wear. | `call_Uz8oulFbXTe63MrqSwQtpPrh.png` |
| Start/finish | Black and warm off-white motorsport checkerboard with a narrow horizontal green timing stripe, tire abrasion, and shallow joins. | `call_yfvvhGGi93KBe0uKLDDm3HwQ.png` |
| Glass | Pale cool-cyan tempered safety glass with broad manufacturing waviness, cleaning arcs, and restrained hairline wear. | `call_O9Lx9cMJ5gYd2tCKyPTwnkzN.png` |
| Water | Clear cyan-teal stadium-pool water viewed from above with layered wind ripples and coherent wave interference. | `call_ZzGO2kv0FwGXRl13BlPGmpsB.png` |

The listed source images remain under
`/home/mikael/.codex/generated_images/019f9782-1b58-71b1-a2ba-8ccd3c0aafe4/`.
The committed files are resized copies under `assets/materials`. Their matching
normal maps are derived from luminance with periodic central differences so the
normal vectors remain continuous at repeated tile boundaries.

## Intentional flat concrete

`concrete_base.png` is a uniform `#a4a69f`; `concrete_normal.png` is a flat
OpenGL normal (`#8080ff`). This preserves the requested clean, spot-free gray
concrete instead of reintroducing aggregate or painted noise.
