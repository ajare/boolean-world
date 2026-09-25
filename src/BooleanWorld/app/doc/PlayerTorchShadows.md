# Player Torch shadows

The **Player Torch** is the point light permanently carried by the player. It is
the World's direct light and casts a finite, omnidirectional shadow through one
shared MPP cubemap domain: `BooleanWorld.PlayerTorch`. It is not a directional,
spot, cascade, or area-light feature.

## Game configuration

Every Launcher configuration (`Debug`, `Release`, and `MemCheck` `Game.yaml`)
sets `Configuration.Video.Shadows`:

```yaml
Shadows:
  Enabled: true
  FaceResolution: 1024 # pixels per cubemap face
  Range: 192
  NearPlane: 0.25
  ConstantBias: 0.0008
  NormalBias: 0.0025
  Filter: pcf          # hard or pcf (3x3)
  FilterRadius: 1
  FadeStart: 0.9       # normalized fraction of Range
```

`Range` is both the direct-light range and the shadow-domain far plane.
Shadowing starts fading at `FadeStart * Range` and is fully lit at `Range`.
`FaceResolution` changes allocation and is deliberately configured at launch;
F1 does not change it. `ConstantBias` and `NormalBias` trade acne against
peter-panning. PCF radius is in cubemap-face texels and preserves continuity
across face seams.

All World render-scale and AA pipelines join the same domain, so changing the
active render scale or AA selection never creates a second cubemap. An empty or
disabled domain leaves direct Player Torch illumination intact.

## Diagnostics and invalidation

F1 opens **Player Torch** diagnostics. The controls are session-only:

- enabled override (configured / force enabled / force disabled);
- range, constant and normal bias;
- hard versus PCF filter, PCF radius, and fade start; and
- read-only configured cubemap resolution, hardware-fallback status, and the
  number of casters the Torch's Range sphere currently selects.

They never write `Game.yaml`; edit its `Video/Shadows` block to persist a value.
The caster count is the World's own surface models plus any other scene model
the sphere retains; zero means the cubemap holds no occluders at all, so every
lit surface stays fully lit however the biases and filter are set.
The Game recalculates the Torch position from the player's eye and yaw/debug
offset each frame. That offset is a maximum: the Torch is a light in the World,
so it stops `BW_PLAYER_TORCH_WALL_CLEARANCE` short of the first surface between
it and the player rather than passing through into rock. The check is made at
the Torch's own height, so a floor step it clears and a ceiling step it passes
under do not shorten it - see
`ArrangementWorldData::distanceToFirstWallCrossing`, which considers what a
wall draws rather than what it collides with. MPP reuses the cubemap while the light, caster state, and
options are unchanged, and redraws all six faces when they change. A committed
World generation explicitly invalidates the domain because dynamic world buffers
do not expose a model revision to MPP.

A fallback status means an enabled cubemap request was unsupported or could not
be allocated. The domain is disabled after one warning, while direct Player
Torch lighting remains available; it does not mean a deliberately disabled
configuration failed.

## Portal paths

The Torch can contribute through up to three Portal hops. Planning has three
independent fixed limits: `PortalLightHopLimit` (3),
`PortalLightAttachmentLimit` (2 retained virtual lights), and
`PortalLightShadowPassLimit` (8 folded-segment cubemap passes). A path with N
hops costs N+1 shadow passes. Setting `PortalLightLimits::maxHops` to zero
disables transmitted lighting; setting it to one preserves one-hop-only
behaviour.

A path records both endpoints at every crossing and may not revisit either one.
Candidates are ranked by descending estimated attenuated strength, descending
aperture visibility, and then stable Layer/pair/endpoint identity. This makes a
fixed World and Torch state produce the same retained list every frame. Every
candidate is reported by `WorldRenderer::getPortalLightDiagnostics()` as
retained, rejected for geometry or an endpoint cycle, deduplicated, cut by a
named budget, or unavailable because its complete shadow allocation failed.

Equivalent paths are deduplicated only when they have the same hop count and
folded geometry: renderer-space positions and aperture dimensions are compared
within `1e-4` World units, unit axes within `1e-5`, and inverse-transform matrix
elements within `1e-5`. These tolerances are also declared beside the planner
contract in `PortalLight.h`.

Each retained path folds the receiver ray backwards through every resolved
aperture. Its N+1 point-shadow cubemaps cover the real-light-to-first-source
segment, every destination-to-next-source segment, and the final destination-
to-receiver segment. Publication is atomic: a missing map rejects the whole
path rather than leaving an unshadowed recursive contribution.

## Manual validation

1. Move, then stop: Player Torch shadows follow movement; stationary frames
   reuse the cubemap. Turn in place and exercise the F1 debug offset.
2. Check floor, ceiling, and walls close to the player for acne and
   peter-panning. Repeat in 2D and 3D horizontal material modes.
3. Cross all directions/seams and the range-fade boundary. Verify opaque and
   masked surfaces cast; blended/transparent surfaces receive but do not cast.
4. In the editor player-view preview, the Player proxy stands in for the player
   carrying the Player Torch. Confirm its lighting, invalidation after geometry
   edits, and fallback match Game.
5. Capture a dirty and stationary frame in RenderDoc. A dirty frame has exactly
   `+X`, `-X`, `+Y`, `-Y`, `+Z`, and `-Z` Player Torch depth passes; a reused
   frame has none. Participating World pipelines share those six passes.

MPP setup, face inspection, cache diagnostics, and the matching RenderDoc event
names are documented in `ext/willpower/ext/massive-poly-pusher/doc/SHADOW_SETUP.md`
and `SHADOW_VALIDATION.md`.
