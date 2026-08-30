# ADR-0034: Water reflection techniques are mutually exclusive

**Status:** Accepted

Water interfaces select exactly one Water reflection technique: Screen-space or Planar. Both techniques supply reflected radiance to the same Liquid reflectance, Liquid F0, Fresnel, ripple-normal, absorption, depth-rejection, ambient-fallback, and compositing model; Planar does not silently fall back to Screen-space.

BooleanWorld owns Liquid-surface discovery and selects at most four frustum-visible surface elevations, grouping elevations within 0.01 world units. Selection favours estimated screen coverage and then camera distance, retains an existing selection until a challenger has 20% greater coverage, and uses ambient fallback for unselected elevations. MPP owns the generic rendering mechanism: the generated water graph contains exactly one Planar pass and image per selected elevation, so the common zero-water case allocates and renders none. Each pass mirrors the camera around its plane, keeps geometry on the viewer's side through an oblique clip plane with a fixed 0.05-world-unit bias, excludes water and post-processing, and renders the opaque scene with ordinary materials, lighting, Player Torch shadows, and environment. Liquid volume absorption is disabled inside this virtual-camera pass because applying the ordinary camera-to-surface calculation to the mirrored eye would invent a false path through water.

`Video/WaterReflections` in `Game.yaml` persistently selects `Technique` (`screen-space` or `planar`) and `PlanarResolution` (`full`, `half`, or `quarter`); missing values default to Screen-space and Half. Planar reflection resolution is relative to the active 3D world target. Reduced images use linear clamp-to-edge sampling without mipmaps, store sample validity in alpha, and render every frame. The F5 controls expose session-only technique, planar resolution, and generic reflection enablement; the editor preview remains Screen-space because it does not consume Launcher configuration.

## Considered alternatives

- **One selected planar elevation.** Rejected because every other Pool would receive geometrically incorrect projection or no authored technique.
- **Planar for one elevation and Screen-space for the rest.** Rejected because selecting Planar would become an implicit hybrid with unpredictable visual behaviour.
- **Four fixed graph passes and images.** Rejected because absent water would still retain image memory and, without a separate conditional-execution contract, incur framebuffer setup and clear work.
- **Unlimited visible elevations.** Rejected because reflected scene cost would scale without bound.

## Consequences

The generated graph can change between zero and four Planar branches as selected elevations enter or leave view. Runtime allocation or reflected-render failure disables reflection with a warning rather than changing technique; the fallback graph retains Water absorption, opaque-depth rejection, and compositing. Planar uses technique-specific graph topology and therefore omits the Screen-space scene-colour copy and mip generation.
