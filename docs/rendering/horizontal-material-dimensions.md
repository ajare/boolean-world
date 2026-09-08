# Surface-up-vector coordinates for 2D procedural materials

Boolean World's world renderer separates floor and ceiling surfaces from walls
so those surfaces, including slopes, can use cheaper two-dimensional procedural
materials. Walls always use the original three-dimensional material shader.

## Configuration

`Configuration/Video/HorizontalMaterials` in `Game.yaml` selects the horizontal
path at startup:

```yaml
Configuration:
  Video:
    HorizontalMaterials: 2d
```

The accepted values are `2d` and `3d` (case-insensitive). The default is `2d`
when the field is absent. The option is read by the Launcher, passed across the
DLL boundary in `VideoOptions`, and stored in `BooleanWorldModel`. It is not a
runtime toggle: a map renderer instantiates only the selected horizontal path.

- `2d`: floors and ceilings use `World/Material.Horizontal2d`.
- `3d`: floors and ceilings use `World/Material.Default`.
- Walls always use `World/Material.Default`.

## Rendering architecture

`WorldRenderer` owns two `WorldRenderer3d` instances and two independent
`WorldTriangle3dDataProvider`s:

1. a horizontal renderer containing floor and ceiling meshes;
2. a wall renderer containing wall meshes.

`WorldSurfaceSet` tells `WorldBatch` which material definitions to turn into
meshes. Horizontal batches create distinct floor and non-floor meshes so that
floor-only embossing can be enabled without affecting ceilings. Wall batches
contain only wall materials. Distinct renderer resource names prevent the two
batches from colliding when both use the 3D material.

`WorldRenderer::updateDataProviders` routes arrangement triangles to the
horizontal provider and wall quads to the wall provider. Both renderers are
added to the same scene and therefore share the existing render-target,
anti-aliasing, ambient-occlusion, and compositing pipeline.

## Shader resources

The paths share `world.vert` but have separate fragment shaders and programs:

| Surface/path | Material | Program | Fragment shader |
| --- | --- | --- | --- |
| Walls | `World/Material.Default` | `World/WorldProgram` | `shaders/world_pbr.frag` |
| Horizontal 3D | `World/Material.Default` | `World/WorldProgram` | `shaders/world_pbr.frag` |
| Horizontal 2D | `World/Material.Horizontal2d` | `World/WorldHorizontal2dProgram` | `shaders/world_pbr_2d.frag` |

The 3D shader remains the authoritative wall implementation. The 2D shader has
native `vec2` noise, FBM, Voronoi, material fields, procedural-normal sampling,
and supernatural emission implementations for every procedural Technique.

The 2D path evaluates in surface-up-vector coordinates: a stable **Surface
frame** derived from the surface's unperturbed up-vector. Its U axis is World X
projected into the surface plane (with a World Z fallback near vertical), and
its V axis completes the
right-handed orthonormal frame. Coordinates are World-origin anchored dot
products against those axes. Consequently a horizontal surface still produces
exactly World X/Z, while a sloped surface has undistorted in-plane distances and
separate Arrangement fragments of one plane cannot acquire separate origins.
Floors and ceilings canonicalize to the same up-vector, so outward-facing
ceiling normals do not mirror the field.

Material scale is applied after this projection. Procedural field derivatives
are lifted back to World space through the Surface frame's U/V axes before they
perturb the shading normal. Secondary-material selection, supernatural
emission, and Embossing all consume coordinates from the same captured frame.

Both shaders expose the same renderer-facing controls: material index and
parameters, material scale, global time, player position, and the batch's
embossing. This keeps map material definitions and F5 material controls usable
with either horizontal mode.

## Embossing

Embossing is assigned independently through a surface's Emboss preset, not a
global render option, so its uniforms arrive per batch alongside
`MATERIAL_INDEX` and `MATERIAL_PARAMS` - and it applies to whatever surface the
preset was assigned to, floor, ceiling or wall alike.

`EMBOSS_PATTERN` takes six values:

- `0`: none;
- `1`: square;
- `2`: hexagon;
- `3`: running bond;
- `4`: modular opus;
- `5`: Voronoi.

`EMBOSS_RADIUS` and `EMBOSS_DEPTH` are the shared size and depth uniforms for
all patterns. `EMBOSS_DEPTH_VARIATION` adds a deterministic random downward
offset to each tile, scaled by pattern depth; `0` keeps all tile surfaces
level and `1` allows an offset up to the full configured depth. The size is a
radius for square and hexagon patterns, the full tile length for running bond,
the large square tile size for modular opus, and the nominal cell size for
Voronoi. `EMBOSS_RUNNING_BOND_WIDTH` sets tile width as a percentage of length
and `EMBOSS_RUNNING_BOND_OFFSET` offsets alternate rows by a percentage of
length. `EMBOSS_VORONOI_ROUNDING` smoothly rounds Voronoi cell junctions from
`0` (sharp) to `1` (maximum rounding).

The pattern is laid out in the plane of the surface being shaded, not in the
World ground plane. On the horizontal 2D path it reuses the coordinates and
tangent axes captured from the unperturbed geometric Surface frame. It does not
derive a new frame from the normal after the Technique has perturbed that
normal.

Square/grid, hexagon, and modular-opus patterns can optionally select a
secondary procedural material through one global debug enable setting
(`USE_SECONDARY_MATERIAL`/`SECONDARY_MATERIAL_INDEX`); the pattern a material
already embosses chooses the layout. Voronoi and running bond do not support
secondary materials. Grid tiles alternate as a checkerboard. Modular opus uses
primary material for its offset large squares and secondary material for the
half-size squares between them. Hexagons use an axial three-colour class: one
class remains primary and every primary hexagon is surrounded by six secondary
hexagons. A secondary index of `-1` means "same as primary" and is the
default.

## Maintaining material parity

When adding or changing a procedural material:

1. keep its numeric material index stable;
2. update the 3D implementation in `world_pbr.frag`;
3. update the corresponding genuine `vec2` field, albedo/PBR properties,
   procedural normal, and any emission in `world_pbr_2d.frag`;
4. preserve the public uniforms and the semantic range of existing controls;
5. verify both startup modes, because `3d` creates two batches using the same
   material resource while `2d` uses separate material resources.

The 2D version should reproduce the material's character rather than evaluate
a fixed slice of a 3D noise field. Avoid introducing Y/elevation into its
procedural coordinates.
