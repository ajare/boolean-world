# Liquid rendering

Liquid is a separate, blended world surface. `arr::ComputeLiquidLevels` derives
one **Liquid depth** for each Arrangement face; `WorldRenderer` emits the wet
face at `floorZ + liquidDepth` into its Liquid scene model. The ordinary
Horizontal model contains floors and ceilings, while only the Liquid model is
deferred to MPP's water pass. This lets Liquid reflect the final opaque world
without moving opaque geometry out of its normal rendering path.

## Interface model

The canonical authored optical controls are per-Liquid-type **Liquid
reflectance** and **Liquid F0** (`core::LiquidProperties`):

```text
Fresnel = F0 + (1 - F0) * (1 - nDotV)^5
interface reflection = Liquid reflectance * Fresnel
```

Liquid reflectance grades the whole angle-dependent interface response. It does
not replace Liquid F0:

- Liquid reflectance `0` suppresses the reflected interface entirely.
- Liquid reflectance `1` leaves the response implied by Liquid F0 unchanged.
- Liquid F0 is the normal-incidence Fresnel value. At `0`, normal-incidence
  reflection disappears but the grazing response remains.

The fragment shader combines the Fresnel-weighted reflection with the existing
Liquid absorption/tint contribution. The reflection source is screen-space:
two scrolling ripple octaves perturb the surface normal, then a dithered ray
march projects the reflected view ray into the opaque scene depth. A refined,
point-sampled depth hit supplies scene colour; a miss, unstable silhouette hit,
screen edge, or grazing view fades to the Liquid ambient fallback. This keeps
movement and ripple motion stable while avoiding depth-filtered false hits.

## Render paths

Gameplay and the editor's real 3D preview both use MPP's generated-water graph:

1. Opaque floors, ceilings, walls, and world geometry render first.
2. Ambient occlusion, when enabled, composites that opaque scene.
3. `SceneColourCopy` creates the resolved-scene image and its mip chain.
4. `WaterScene` draws only the deferred Liquid model over that final opaque
   image and publishes `WaterComposite`.

Both paths present `WaterComposite`. This is important at every gameplay render
scale and anti-aliasing mode: the water pass samples the same resolved scene and
camera frame that rasterized the opaque scene. Resizing rebuilds the resolved
scene at the current viewport dimensions, including its mip chain. The editor
preview has one native-resolution, AA-off configuration but otherwise follows
the same topology and output selection.

Fragment-overdraw diagnostics deliberately use their own scene-and-resolve
pipeline. They do not enable generated water and continue drawing all world
geometry through the diagnostic material path.

## Screen-space limits

SSR can reflect only the already-rendered opaque scene. It cannot find
geometry outside the screen, behind another visible surface, or transparent
geometry. Such rays use the ambient fallback; reflection therefore cannot show
a true off-screen world or a complete environment map. Depth discontinuities
can still leave a softened silhouette fringe despite hit refinement and local
depth-stability checks.

Liquid is blended and the water pass does not write depth. Multiple Liquid
surfaces along one view ray therefore have ordinary transparent draw-order
limits. The resolved scene is LDR in the legacy world graph, so very bright
reflections may clip. These are rendering limitations, not changes to Liquid
depth, Pool equilibrium, or authored Liquid properties.

## Manual acceptance

Check a wet world from overhead and grazing angles, while moving and while the
ripples animate. At screen edges and SSR misses, reflection should transition
to the ambient fallback rather than jump. View the Liquid surface from below as
well. Test the property extremes: zero Liquid reflectance removes the interface;
zero Liquid F0 leaves only the grazing-angle response. Repeat gameplay at full,
half, quarter, and eighth render scales; with every anti-aliasing mode; and with
ambient occlusion both enabled and disabled.
