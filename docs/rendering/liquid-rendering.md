# Liquid rendering

Today a liquid surface is a flat blue quad. This design gives every liquid three
authored optical properties — **surface reflectivity**, **opacity** and
**refractive index** — and makes them the sole controls over what the surface
looks like:

- reflectivity and the refractive index together decide **how much of the world
  is reflected**, via screen-space reflections in MassivePolyPusher;
- opacity decides **how much of the surface behind the liquid is visible**;
- the refractive index decides **how far that surface is displaced** as you look
  through it.

The three are not independent dials bolted onto a shader. The refractive index
drives both the Fresnel reflect/transmit split and Snell's law, so it is the one
value that ties reflection, transmission and refraction to each other; the other
two grade the result.

## Where we are starting from

`arr::ComputeLiquidLevels` produces a per-face liquid depth. For every wet face
`WorldRenderer::updateDataProviders` emits one extra triangle at
`floorZ + liquidDepth`, into the mesh bucket reserved for that liquid type's
`LiquidMaterialIndex` (`BW_WATER_MATERIAL_INDEX`, 40). That bucket lives in the
**Horizontal** `SceneModel3d`, alongside every floor and ceiling. It is marked
`setMeshBlend(..., true)` and its vertices carry a fixed `0x99` alpha
(`waterVertexColour`), so fixed-function blending composites it at 60% opacity.
`world_pbr_2d.frag`'s `waterMaterial2d` returns a constant albedo
`(0.1, 0.35, 0.6)`, roughness 0, metallic 0.

So: opacity is a hardcoded constant, reflectivity is whatever the flat `vec3(0.12)`
ambient term in `shadePbr` happens to give, and there is no refraction at all.

MassivePolyPusher already has the hard part built — see its `doc/WATER_SSR.md`.
A screen-space march with prefiltered-cubemap fallback, binary hit refinement,
post-refinement thickness rejection, edge and grazing fades, and the
`MPP.SceneColourCopy` / `MPP.WaterScene` graph passes that make it safe to read
the shaded scene while drawing into it. None of it reaches Boolean World today,
for three specific reasons:

1. **The feature is bound to `PbrMaterial`.** `usesWaterMaterial` looks for
   `PbrMaterialFeature::Water` on an `mpp::PbrMaterial`. Boolean World's world
   surfaces are legacy `Material` resources with hand-written programs
   (`world_pbr_2d.frag`).
2. **The passes are not in our graph.** We run
   `RenderPipelineMode::GraphLegacyForward` with no `graphTemplate`, so
   `RenderPipeline::renderGraphForward` builds the graph programmatically —
   shadow, scene, AO, bloom, tone-map. There is no colour copy and no water
   pass, and `graphHasWaterPass` therefore reports false.
3. **The split is per model, not per mesh.** `selectModels` partitions
   `SceneModel3d`s. Our liquid meshes are inside the Horizontal model, so they
   cannot be deferred without deferring every floor and ceiling with them.

Refraction and the underwater view are listed as *not covered* by
`WATER_SSR.md`, so that part is new work in either repo.

## The model

Everything below is per liquid type, evaluated per fragment of the liquid
surface.

### Fresnel is the split, not a dial

The refractive index `n` gives the normal-incidence reflectance

```
F0 = ((n1 - n2) / (n1 + n2))^2
```

with `n1 = 1, n2 = n` looking down into the liquid and the two swapped looking up
from inside it. Water's 1.333 gives the familiar 0.02. Schlick then grades it by
view angle, and the transmitted fraction is `1 - F`, which is what makes a pool
read as glassy at the horizon and clear at your feet without any authoring.

**`surfaceReflectivity` is a multiplier on `F`, not a replacement for it.**
1.0 is physical; 0.0 is a liquid that reflects nothing (mud, blood, lava viewed
as an emitter). Lowering it does not create energy — the transmitted fraction
becomes `1 - reflectivity * F`, so the two still sum to one. Replacing Fresnel
with a constant, which is the obvious reading of "surface reflectivity", is what
makes CG water look like tinted glass: it loses the grazing-angle behaviour that
is the strongest single cue that a surface is liquid.

### Opacity is extinction over a path, not an alpha

A flat alpha is wrong in a way that is visible immediately: the shallow edge of a
pool is exactly as murky as its deepest point, which kills the read of depth that
liquid gives a space for free. Opacity should be Beer–Lambert absorption over the
distance the view ray actually travels through the liquid:

```
transmittance = exp(-extinction * pathLength)
```

Authored `opacity` in `[0,1]` maps to an extinction coefficient against a
reference depth `d_ref` (a world-unit constant, one liquid-body depth that reads
as "this liquid's characteristic murk"):

```
extinction = -ln(1 - clamp(opacity, 0, 0.999)) / d_ref
```

so opacity 0.5 means "half the light is gone after `d_ref` units", opacity 0 is
perfectly clear, and 1 is opaque at any depth.

`pathLength` is available for free in the pass we are adding: the liquid pass
already samples the scene depth buffer, so the length is the difference between
the liquid fragment's view depth and the view depth of the **refracted**
background sample. That makes the murk follow the bent ray, and it is correct for
a view along the surface as well as straight down — which the per-face
`liquidDepth` is not.

Opacity needs something to fade *toward*. That is the liquid's own colour, which
already exists as `waterMaterial2d`'s hardcoded albedo; this design relocates it
into `LiquidProperties` as a tint rather than adding a fourth knob.

The tint then carries two distinct jobs, and both are needed — either alone looks
wrong:

- **In-scattered colour**, the `(1 - T)` half of `colour * T + (1 - T) * tint`.
  This is the dirt, silt or algae suspended in the liquid, scattering light into
  the sightline. Without it an opaque liquid fades to black rather than to a
  colour, and reads as a hole rather than as a substance.
- **A per-channel absorption bias.** A liquid absorbs the colours it is *not*, so
  extinction is scaled per channel against the tint — a blue tint absorbs red
  fastest. This is what makes depth shift the hue of what is behind it rather than
  only dimming it, and it is why clear water goes blue-green with depth with no
  in-scattering at all.

Authoring a tint plus a scalar opacity, and deriving the per-channel extinction
from the pair, is deliberate. An absorption coefficient per channel is the more
physical parameterisation and is close to unauthorable: nobody can picture what
`(0.9, 0.65, 0.4)` looks like in a pool.

One consequence is worth stating plainly, because it decides whether phase 1 looks
finished. **In phase 1 a liquid's entire visibility comes from its opacity and
tint.** With reflectance still to come, a liquid authored at opacity 0 is
invisible — the floor is seen undistorted, with no surface at all. That is correct
rather than a defect, but it means Water's authored opacity has to be meaningfully
above zero, or liquid will look like it has disappeared.

### Refraction is a screen-space offset

Snell's law with `eta = n1 / n2` bends the view ray at the surface:

```
refracted = refract(-V, N, eta)
```

Step a short distance along it in view space, project to screen UV, and sample
the resolved scene colour there instead of at the fragment's own pixel. The step
distance scales with `pathLength`, so a deep pool displaces its floor more than a
puddle, and total internal reflection (looking up steeply from underwater) falls
out of `refract` returning zero — at which point the surface is a pure mirror,
which is the correct and rather nice underwater ceiling effect.

Two guards, both mandatory:

- **Foreground rejection.** If the refracted sample's depth is *in front of* the
  liquid surface, the offset has pulled in a pixel of something standing between
  the camera and the pool. Fall back to the unrefracted UV for that fragment.
  Without this, the player's own silhouette smears into the water at their feet.
- **Edge clamp.** Offsets that leave the viewport fall back the same way, faded
  over a margin as SSR already does with `edgeFade`.

The same ripple normal that distorts the SSR march must distort the refraction,
or the reflection ripples while the bottom of the pool sits still.

### Why this forces blending into the shader

Fixed-function blending composites a fragment against the framebuffer *at its own
pixel*. Refraction needs a different pixel. So the liquid surface can no longer
be `setMeshBlend(true)`; it becomes a non-blended draw that composites in the
fragment shader:

```
background = sampleResolvedScene(refractedUv)      // roughness-mipped
reflection = mix(fallbackAmbient, ssrColour, confidence)
F          = reflectivity * fresnelSchlick(nDotV, F0)
transmitted = background * exp(-extinction * pathLength * tintAbsorption)
colour     = mix(transmitted, reflection, F) + directSpecular
```

which is the same energy split as before, now with both halves sourced from real
scene data. This is a strict improvement, not a workaround: it is also what lets
opacity and reflectivity interact correctly (a highly reflective, highly opaque
liquid is a mirror; a clear, non-reflective one is nearly invisible), which no
amount of `GL_SRC_ALPHA` blending can express.

## Phase 1 — opacity, with no upstream change

Opacity can ship on its own, entirely inside `bw`, and it is the largest single
step away from the flat blue quad. The move that makes it possible is to stop
asking the liquid surface to do the absorbing.

### Absorption belongs to the volume, not the interface

Beer–Lambert describes what happens to light *along a path through a medium*.
Fresnel describes what happens *at a boundary*. Those are two different places, and
the current design put both on the liquid quad only because that is where a water
shader conventionally lives.

Split them along the physics instead. **Every submerged surface absorbs itself**,
because it is the thing at the far end of the path and it knows where it is. The
liquid quad keeps only the interface term, which is reflection, which is phase 2.

This is not a workaround for lacking a depth buffer, though it does avoid needing
one. It is why the phases compose without rework, and it is what makes the
underwater camera expressible at all — see below.

### Path length

A fragment needs the length of the segment between it and the eye that lies below
the liquid surface. With a horizontal surface at `surfaceZ`, that is a vertical
span converted by the ray's inclination:

```glsl
float eyeY  = @ViewPos.y;
float yLow  = min(worldPos.y, eyeY);
float yHigh = max(worldPos.y, eyeY);
float total = yHigh - yLow;

// The submerged fraction of the eye-to-fragment segment.
float fraction = total > 0.001
    ? clamp((min(yHigh, surfaceZ) - yLow) / total, 0.0, 1.0)
    // A near-horizontal ray has no vertical extent to divide by: it is either
    // wholly under the surface or wholly over it.
    : step(0.5 * (yLow + yHigh), surfaceZ);

float path = length(@ViewPos - worldPos) * fraction;
```

All four cases fall out of the same expression: eye above and fragment below (a
pool seen from the bank), both below (swimming), eye below and fragment above
(looking up at a dry ledge from underwater, where only the part up to the surface
absorbs), and both above (fraction 0, no attenuation, no branch needed).

### Looking out of the liquid: bound the path, don't extrapolate it

The fourth case above needs one more thing, because it is the only one where the
model's blind spot becomes visible. A liquid body has a height but no horizontal
extent as far as this shader is concerned — and a submerged eye looking out at a
grazing angle is exactly the view where that matters.

Standing in a small pool looking at the shore, the expression above puts the ray's
exit at the surface *plane*, so the submerged length is the eye's depth over the
cosine of a near-horizontal ray: tens of units, and the shore fogs out as if you
were in an ocean. The ray really left the water a few units away, through the side
of the pool.

There is no data in the shader to say where it left. But there is a bound. The ray
starts submerged and ends dry, so its submerged length is *at least* the vertical
climb from the eye to the surface, and that minimum is achieved by a ray going
straight up. Take the bound rather than the extrapolation:

```glsl
// The far end's own face, from the vertex attribute - not a height test against
// the governing plane, which a dry fragment in an adjacent room can pass by
// accident when the eye's plane is the one in force.
bool  farEndWet = fragmentSurfaceZ > worldPos.y;
float eyeDepth  = max(surfaceZ - eyeY, 0.0);

float path = farEndWet
    ? length(@ViewPos - worldPos) * fraction  // both ends in liquid: the real chord
    : eyeDepth;                               // far end dry: the lower bound
```

The wet test has to come from the fragment's own attribute rather than from
`worldPos.y <= surfaceZ`. Under the submerged-eye rule the governing plane is the
eye's, and a dry floor in the next room can easily sit below *your* water line
while having no liquid over it at all; a height test would fog it as though the
whole sightline were submerged. Keeping the two apart — the eye supplies the
plane, the fragment still supplies its own wet flag — costs nothing, since the
attribute is already there.

The choice is between being wrong about small pools and being wrong about oceans,
and Boolean World's liquid is authored into rooms. The `/cos` form is the right one
for water that reaches the horizon; nothing in the arrangement does.

Note this only changes fragments the ray *leaves* the liquid to reach. Submerged
fragments keep the full chord, so underwater distance fog still works — and it
still fogs correctly toward the horizon, since a submerged fragment seen at a
grazing angle is genuinely far away through liquid.

The transition stays continuous in both directions. Wading in, `eyeDepth` grows
from zero, so above-water geometry tints in smoothly; and at the moment of
crossing, a submerged fragment's chord is the same whether it was computed from the
eye's height or its own face's, because at that instant they are the same plane.

The degenerate guard is not decoration. Written as a plain divide it produces
`0/0` for a horizontal view, and the tint vanishes exactly when you look *across*
a pool — the view where the path is longest and the absorption should be at its
most obvious.

### Submerged, looking at submerged geometry

This is the case the model gets exactly right, which is a pleasing inversion —
underwater is usually where cheap water shaders fall apart. The whole sightline is
inside the medium, so there is no boundary to locate and nothing to approximate:
the fraction is 1 and the path is the plain eye-to-fragment distance. What you get
is true exponential distance fog in the liquid's colour, saturating to the tint at
range, which is what being underwater actually looks like.

Two things about it are worth knowing before it is built.

**The degenerate guard earns its keep here.** Swimming and looking level is the
archetypal underwater view, and it is precisely the case where the vertical span
collapses to zero. Without the `step` fallback the fog vanishes exactly when the
player is most immersed — and it will look like the effect simply does not work,
rather than like a division problem.

**The light is absorbed once, but it travels twice.** Absorption is applied over
the eye-to-fragment path, so the leg from the light *to* the fragment is not
attenuated. With a distant sun that would be a mild error; with a head-mounted
player torch the two legs nearly coincide, so the round trip is close to twice the
path and lit surfaces underwater will read about twice as clear as they should.

Tuning `opacity` to compensate is the wrong fix, because it would then over-absorb
the ambient term and everything unlit. The right one is to absorb the direct term
over twice the path and the ambient over once, which needs `shadePbr` to return its
direct and ambient contributions separately instead of pre-summed. That is a small
refactor and it is worth doing at the same time, because it is the difference
between water that reads as murky and water that reads as murky *until you shine a
light at it*.

### Where the surface height comes from

- **Floors and ceilings.** Per-vertex, `properties.floorZ + liquidDepths[triangle.face]`,
  which is already in scope where the floor triangle is emitted
  (`WorldRenderer.cpp:378-396`). A dry face passes a sentinel far below the world,
  so `min(yHigh, surfaceZ) - yLow` clamps to zero with no branch.
- **Walls.** Per wall, one `ArrangementWorldData::getLiquidSurfaceHeight` query just
  off the midpoint on the normal side. The wall loop already runs per frame to make
  the `facesPlayer` decision, and the visible side is exactly the side whose liquid
  level applies — so the wall holding a pool in reads as submerged from the water
  and dry from the room next door, for free. This reuses the point query this branch
  just added rather than plumbing face adjacency through to the renderer.
- **The liquid quad itself**, with its own height, so that seen from below it is
  tinted by how far under it you are.
- **The eye**, as a uniform from `getLiquidSurfaceHeight(cameraPosition)`.

The channel should be one new float on `WorldTriangle3dDataProvider::DrawVert`,
used by all three. The horizontal uv looks spare — `TEXCOORDS` feeds only
`applyWallNormalMap`, which horizontals disable — but walls genuinely use theirs
for the physical wall uv, and the shader's own comment says horizontals have no
normal-map authoring owner *yet*. Quietly spending a channel someone is going to
want back, in exchange for four bytes a vertex, is a bad trade.

### Which liquid governs

Two different bodies can be involved: the one the fragment sits under and the one
the eye sits in. **A submerged eye wins, and it supplies the surface height as well
as the properties** — every fragment in the frame uses `LIQUID_EYE_SURFACE_Z`,
including fragments whose own face is dry.

That second half is not a detail. Underwater, looking up at a dry ledge, the part
of the ray below the surface still absorbs; but the ledge's own face carries the
dry sentinel, so a rule that only swapped the *properties* would leave the ledge
untinted and the effect would end at the waterline instead of at the eye. The eye's
height is what makes everything above the surface tint correctly from below.

When the eye is dry, the fragment's own liquid governs. The seam — standing in one
pool looking into another at a different level — is visible only where two bodies
with different properties are in frame at once, and is not worth tracing the ray
through the liquid geometry to resolve.

### Where it goes in the shader

Absorption attenuates radiance, so it belongs in linear space: after the emissive
term, before the tone map and the gamma, and before the view-distance fade, which
is a separate stylistic effect.

```glsl
value += supernaturalEmission(texturePosition, materialIndex);
value = applyLiquidAbsorption(value, worldPos);   // <-- here
value = value / (value + vec3(1.0));
value = pow(value, vec3(1.0 / 2.2));
```

Walls always shade through `world_pbr.frag` and horizontals through whichever the
`HorizontalMaterials` option selects, so this is a shared helper both programs
include, not a change to one of them.

### The quad in phase 1

Its alpha goes to roughly zero. It must **not** apply Beer–Lambert to what is
behind it — the surface behind has already absorbed itself, and doing it twice
doubles the murk. That is correct rather than merely convenient: looking down into
clear water, the surface plane really does contribute almost nothing but
reflection, and reflection is what phase 2 adds.

### How phases 2 and 3 compose on top

Nothing here is rework:

- **Phase 2** gives the quad `alpha = reflectivity * F` and a reflection colour.
  Plain source-alpha blending then yields
  `F·reflection + (1 − F)·(surface·T + (1 − T)·tint)`, which is the full model from
  *The model* above, exactly. The absorption half is already sitting in the
  framebuffer.
- **Phase 3** samples the scene colour at a refracted offset — and that scene colour
  has *already been absorbed*, which is what a refracted background should look
  like. Doing opacity this way makes refraction simpler later, not harder.

### What phase 1 does not cover

- **Entities.** They need the same helper in their own shader, fed by a per-entity
  `getLiquidSurfaceHeight` uniform. The per-fragment formula is unchanged, so a body
  half in and half out of the water still gets the right answer. This is the one
  genuinely additive piece of work, and it matters here: the player wading is the
  case that motivated measuring absorption to the visible surface in the first
  place.
- **A ray crossing between bodies at different levels**, per the rule above.
- **Snell's window.** Looking up from underwater, the whole above-water hemisphere
  should compress into a cone of about 97°, with total internal reflection outside
  it turning the rest of the surface into a mirror of the pool floor. Phase 1 shows
  the world above undistorted, tinted by depth. That is the most distinctive real
  feature of this view and it arrives with refraction in phase 3, where the TIR case
  is free — `refract` already returns zero there.
- **Liquid with no geometry behind it** absorbs nothing, since there is nothing to
  absorb. The arrangement is enclosed, so in practice there is always a surface.

## What has to change

Four pieces, in dependency order. Step 1 and the absorption half of step 4 are
phase 1 above, and ship without touching MassivePolyPusher. Steps 2 and 3, and the
reflection half of step 4, are phase 2 — everything that needs the liquid to read
the scene.

### 1. `LiquidProperties` carries the optical properties (bw/core)

`LiquidProperties` already states its own charter: *what a liquid is made of, as
opposed to what the world puts it in.* Optical properties are exactly that, so
they go in the existing struct and the existing `liquidProperties` table rather
than a parallel render-side one.

```cpp
struct LiquidProperties {
  float density;                 // existing
  float viscosity;               // existing

  // How much of the world the surface reflects, as a multiplier on the
  // Fresnel reflectance the refractive index already implies. 1 is
  // physical; 0 is a liquid that reflects nothing.
  float surfaceReflectivity;

  // How much light this liquid absorbs over referenceDepth world units.
  // 0 is perfectly clear at any depth; 1 hides its own floor immediately.
  float opacity;
  float referenceDepth;

  // What the absorbed light leaves behind, and the liquid's own colour.
  wp::Vector3 tint;

  // Absolute refractive index, air being 1. Decides both the Fresnel split
  // above and how far Snell's law displaces what is seen through the
  // surface. Water is 1.333.
  float refractiveIndex;
};
```

Water: `{1.0, 12.0, 1.0, 0.55, 40.0, {0.10, 0.35, 0.60}, 1.333}` — the existing
albedo preserved as the tint, and an opacity chosen to land near today's look at
a typical pool depth so the change reads as a refinement rather than a reskin.

Nothing in core consumes the new fields; `bw::render` does. No serialization
change — these are properties of the *type*, and the type is already what YAML
stores.

### 2. Liquid geometry becomes its own scene model (bw/render)

`WorldRenderer` gains a third `MaterialRenderer` entry beside Horizontal and
Walls: `WorldSurfaceSet::Liquid`, with its own `WorldRenderer3d`,
`WorldTriangle3dDataProvider`, `WorldBatch` and `SceneModel3d`. It holds only the
per-`LiquidType` reserved material buckets, which `WorldBatch::createModelStream`
already creates separately and unconditionally; the loop moves out of the
Horizontal branch into the Liquid one, and the liquid triangle emission in
`updateDataProviders` routes to the new provider.

This is the same shape as the existing Horizontal/Walls split, and it is what
makes the model-granular `selectModels` partition able to defer the liquid
without deferring the floors.

The per-type uniform loop in `WorldRenderer3d::createUniforms` is already the
right place for the new values — it runs once per liquid type and owns that
mesh's `UniformCollection`:

```cpp
auto const& liquid = bw::core::GetLiquidProperties(type);
uniforms->setUniform("LIQUID_REFLECTIVITY", liquid.surfaceReflectivity);
uniforms->setUniform("LIQUID_EXTINCTION", extinctionFrom(liquid));
uniforms->setUniform("LIQUID_TINT", liquid.tint);
uniforms->setUniform("LIQUID_IOR", liquid.refractiveIndex);
uniforms->setUniform("LIQUID_F0", fresnelF0(liquid.refractiveIndex));
```

`setMeshBlend` for these meshes becomes `false` — see *Why this forces blending
into the shader*.

### 3. The liquid gets its own pass

This is the only step that reaches outside `bw`, and the reach is smaller than it
first looks. The two passes already exist as registered built-in factories
(`MPP.SceneColourCopy`, `MPP.WaterScene`), and `RenderPipelineOptions::graphTemplate`
is a plain `ResourcePtr` that `renderGraphForward` honours for *any* graph-forward
mode, not only the PBR one its comment names. So the passes themselves are
reachable from Boolean World without touching MassivePolyPusher.

**The one thing that is not reachable is model classification.**
`WaterScenePass` selects its models through `usesWaterMaterial`, which returns true
only for an `mpp::PbrMaterial` carrying `PbrMaterialFeature::Water`. Our liquid
model is a legacy `Material`, so it classifies as non-water: the liquid pass draws
nothing, and — because `graphHasWaterPass` is now true — the opaque pass draws the
liquid itself, without the resolved colour or depth bound. The failure is silent
and looks exactly like today's build.

There is no hook for this. `RenderPipeline::mGraphPassFactories` is a private
member with no accessor, so Boolean World cannot register a pass factory of its own
that selects by a different rule. The minimal upstream change is therefore a flag,
not a feature:

```cpp
// SceneModel3d
void setDeferredToWaterPass(bool deferred);
bool isDeferredToWaterPass() const;

// RenderGraphBuiltInPasses.cpp, usesWaterMaterial
if (sceneModel->isDeferredToWaterPass()) return true;
```

That keeps legacy materials out of the PBR feature vocabulary — which is the reason
the current check cannot see them — and it is the whole of the required upstream
delta. Everything else below is a choice about where the graph topology lives.

#### Option A — extend the programmatic graph (recommended)

Add the passes to `RenderPipeline::renderGraphForward`'s programmatic branch,
gated on a new `RenderPipelineOptions::water.enabled` (defaulted off, so no
existing pipeline changes), which:

- forces `GraphImageUsage::Sampled` and `GraphStoreOp::Store` on `SceneDepth`,
  which today is only sampled when TAA or AO is on;
- creates `SceneColourResolved` — same format as the scene colour, with a
  declared mip chain, since the roughness blur samples it;
- adds `MPP.SceneColourCopy` then `MPP.WaterScene`, **after the AO composite**,
  reading and writing `presentationTexture` and re-pointing it at the water
  pass's output version.

The AO placement matters and is not arbitrary. AO composite writes a different
image (`AmbientOcclusionComposite`) which is what Boolean World names as its
pipeline output when AO is on. Putting the liquid pass before it would both
darken the liquid with screen-space occlusion it should not have and leave the
liquid reflecting a pre-AO scene; putting it after, and moving the output to the
new version, gets both right and keeps bloom (when enabled) downstream of the
liquid's bright highlights.

This is about forty lines upstream, and Boolean World keeps the generated pipeline
matrix it has today — the AA, render-scale and depth-prepass variants stay a
function of `RenderPipelineOptions`, as they are now.

#### Option B — author a graph template in Boolean World

Set `graphTemplate` to a Boolean-World-authored `.rendergraph.yaml` containing the
two passes. Upstream then needs only the classification flag. The cost is that
Boolean World takes ownership of graph topology that MassivePolyPusher currently
generates and maintains for it: the shipped templates are all HDR/PBR, so the LDR
legacy graph would be written fresh, and ambient occlusion on/off becomes two
authored templates rather than an option. Depth prepass and the `graphPasses`
debug toggles are read at execute time, so those still work from options.

One point in its favour: template images are resolved by name, which would retire
the graph-image-id arithmetic in `StatePlayBooleanWorld` (`outputImage` is
currently computed as `(usesMrtNormals ? 6u : 4u) + …`, which silently depends on
MassivePolyPusher's internal image creation order).

#### Option C — a second pipeline, with no upstream change at all

`setActivePipelineSamplerOverrides`, `setCameraFrame` and `renderFullscreenQuad`
are all public on `RenderSystem`, so Boolean World could render the world, copy
its colour, bind the copy and the depth as sampler overrides, and draw a
liquid-only scene through a second pipeline into the same target — reimplementing
`MPP.SceneColourCopy` and `MPP.WaterScene` on our side of the boundary.

Two things make this the worst of the three despite needing nothing upstream. The
programmatic graph stores and samples `SceneDepth` only when TAA or ambient
occlusion is on, so liquid would silently lose its occlusion reject and its path
length whenever the player turns AO off; and fetching that depth means a second
piece of image-id arithmetic of the kind above. It trades a small, reviewable
upstream diff for a larger, more fragile local one that duplicates code
MassivePolyPusher already owns. Worth knowing it exists; not worth doing.

### 4. The shader implements the split (`world_pbr_2d.frag`)

`waterMaterial2d` is replaced by a liquid path taken when `MATERIAL_INDEX == 40`,
which:

1. **discards against sampled depth**, exactly as `PBR_SPEC_WATER` does — the
   liquid pass has no depth attachment, deliberately, because attaching a depth
   image the pass also samples is both a version-aliasing bug in the graph and a
   sampler feedback loop;
2. distorts the normal with two scrolling octaves;
3. marches for the reflection, and blends to a fallback by confidence;
4. refracts, guards, and samples the resolved scene colour for the background;
5. computes `pathLength` from the two depths and applies Beer–Lambert with the
   tint;
6. mixes by `reflectivity * F` and adds the direct specular term.

Steps 2–3 are a port of MPP's `waterMarch` / `waterHitStability` /
`waterProject` helpers. **Port them, do not reimplement them.** Every non-obvious
line in that code is load-bearing — the sign-change-only coarse loop, the
thickness test applied *after* binary refinement rather than during the march,
`texelFetch` rather than filtered depth taps, the interleaved-gradient dither.
`WATER_SSR.md` documents what each one is defending against, and each one has a
specific ugly failure mode when dropped. If those helpers can be lifted into a
shared include that both `PbrShaders.h` and `world_pbr_2d.frag` pull from, that
is strictly better than a copy; the `@@Uniform` program-builder syntax is common
to both.

**The fallback is the honest weak point.** MPP falls back to the scene's
prefiltered IBL cubemap. Boolean World has no IBL environment — `GraphLegacyForward`
never sets `setActivePbrEnvironment`, and `shadePbr` uses a flat `vec3(0.12)`
ambient. So on a miss we fall back to that same flat ambient, tinted, which is
close to today's appearance. That is acceptable (a miss degrades to what we
already ship) but it means off-screen reflections read as flat, and it is the
first thing to revisit if liquid ends up prominent in a level.

## Cost and limits

`ssrSteps` is the dial, cost is linear in it and paid per liquid fragment, so it
scales with how much of the screen liquid covers. Liquid fully off-screen costs
only the `SceneColourCopy` blit. Start at MPP's default 32 steps and expose the
march tuning as pipeline options rather than per-liquid properties — it is a
quality setting, not a material property, and pinning it per liquid type would
make one pool cheaper than another for no authored reason.

Known limits, all inherited or structural:

- **Stacked liquid surfaces composite wrongly.** The pass writes no depth, so two
  liquid bodies at different heights along one view ray blend in draw order.
  Mitigation is a back-to-front sort by surface height, which
  `WorldRenderer3d`'s existing geometry sort can be pointed at. Rare in practice
  — one liquid level per face — but real.
- **Only opaque geometry reflects and refracts.** SSR and the background sample
  both read the resolved *opaque* colour. Other transparent surfaces are absent
  from both.
- **The scene colour is `Rgba8` here.** The legacy graph's scene image is LDR, so
  reflections are LDR and clip on bright highlights. Not worth widening for this
  alone.
- **Residual SSR silhouette fringe**, as `WATER_SSR.md` describes; softened by the
  roughness blur and by any non-zero distortion.

## Verification

- A core test pinning each liquid type's optical properties and the
  `opacity → extinction` and `n → F0` conversions, so a retune is a deliberate
  edit rather than a drift.
- A test of the submerged-fraction expression over all four eye/fragment
  arrangements plus the horizontal-ray degenerate case — the one that fails
  silently, and only when looking across a pool.
- For phase 1 by eye: the tint deepens with distance across a pool floor and
  vanishes at its shallow edge; wading in tints the world continuously as the
  camera crosses the surface, with no pop; a wall between a flooded and a dry room
  is tinted from one side only.
- A render test that the Liquid scene model contains only liquid meshes and that
  the Horizontal model contains none — the partition the whole pass depends on.
- In MPP, extend `runRenderGraphGpuTests`' existing water-topology coverage to
  the programmatic graph path, including the depth `Sampled`/`Store` promotion
  and the mip chain surviving a viewport change.
- By eye, against the three properties independently: opacity 0 with a patterned
  floor (refraction visible, no murk), opacity 1 (floor gone, reflection only),
  reflectivity 0 (no sky, murk and refraction only), and IOR swept from 1.0 (no
  bend, no Fresnel) to 2.4 (heavy bend, strong edge reflection). Each should move
  exactly one thing.
