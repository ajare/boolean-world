# Horizontal procedural-material dimensions

Boolean World's world renderer separates horizontal surfaces from walls so that
floors and ceilings can use cheaper two-dimensional procedural materials. Walls
always use the original three-dimensional material shader.

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
and supernatural emission implementations for all 30 material indices. It
samples `worldPos.xz` only, so horizontal material appearance does not change
with floor or ceiling elevation.

Both shaders expose the same renderer-facing controls: material index and
parameters, material scale, global time, player position, and floor-pattern
settings. This keeps map material definitions and F5 material controls usable
with either horizontal mode.

## Floor embossing

Floor meshes support three `FLOOR_PATTERN` values:

- `0`: none;
- `1`: square;
- `2`: hexagon.

`HEXAGON_RADIUS` and `HEXAGON_DEPTH` remain the shared radius and depth
uniforms for both square and hexagon patterns. Despite their legacy names,
they control either pattern. `WorldRenderer3d` sends a nonzero pattern only to
meshes tagged as floors; ceilings and walls always receive `0`.

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
