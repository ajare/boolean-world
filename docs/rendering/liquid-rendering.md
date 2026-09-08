# Liquid rendering

Liquid is a separate, blended world surface. `arr::ComputeLiquidState` settles
horizontal Pools across Hydraulic cells, integrating each cell's affine floor
and ceiling capacity. It clips each wet cell against its shoreline and ceiling,
then `WorldRenderer` emits the resulting `LiquidSurfaceTriangle`s at their Pool
elevations into its Liquid scene model. No face-wide scalar floor or Liquid
depth participates in this path. The ordinary Horizontal model contains floors
and ceilings, while only the Liquid model is deferred to MPP's water pass. This
lets Liquid reflect the final opaque world without moving opaque geometry out
of its normal rendering path.

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
Liquid absorption/tint contribution. The selected Water reflection technique
supplies the reflected radiance. Screen-space uses two scrolling ripple octaves
and a dithered ray march through opaque scene depth. Planar projects the same
ripple normal into the rank-matched reflected scene image. Invalid samples and
unselected elevations fade to the Liquid ambient fallback.

## Render paths

Gameplay and the editor's real 3D preview both use MPP's generated-water graph.
Screen-space renders the opaque scene, optional ambient occlusion,
`SceneColourCopy`, and `WaterScene`. Planar instead renders one stable,
rank-named `PlanarReflectionN` pass/image per selected Liquid elevation, then
the ordinary opaque scene, optional ambient occlusion, and `WaterScene`; it has
no `SceneColourCopy`. A Planar frame with no selected elevation creates no
reflection resource, reflection pass, or Water pass. The editor explicitly
stays Screen-space.

Each Planar pass renders opaque non-Water content with a mirrored, clipped
camera and no post-processing. Full, Half, and Quarter Planar resolution are
per-dimension fractions of the active 3D world target and round up to at least
one pixel. Runtime allocation or reflected-render failure logs a warning and
sticks for the play session. The selected technique remains Planar, reflection
passes disappear, and `WaterScene` continues absorption, opaque-depth rejection,
and compositing with reflected radiance disabled.

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
ripples animate. At invalid samples reflection should transition to the ambient
fallback rather than jump. View the Liquid surface from below as well. Test the
property extremes: zero Liquid reflectance removes the interface; zero Liquid
F0 leaves only the grazing-angle response.

For Planar, use F5 to select Full, Half, and Quarter resolution and press F10 at
each setting. In each timestamped directory compare every `PlanarReflectionN`
image with the pre-Water opaque scene and `WaterScene`; `render-graph.txt` must
list each pass's image dimensions and submitted triangles/primitives. Repeat in
Screen-space and in a zero-visible-Water Planar view and confirm there are no
Planar captures. Also repeat gameplay at each render scale and anti-aliasing
mode, with ambient occlusion both enabled and disabled.
