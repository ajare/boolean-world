# ADR-0041: AudioEmitters are owned by Primitives and captured at World generation

**Status:** Accepted
**Date:** 2026-09-05
**Extends:** ADR-0014 (a Layer's Primitives are derived from its LayerBuildSteps), ADR-0005 (`WorldData` is an immutable snapshot)

## Context

Spatial audio needs positioned sound sources in a World. The model offered two
obvious homes and neither fitted.

`WorldTriggerLine` is the existing example of a non-Primitive authored thing: it
is added directly to a Layer, outside the LayerBuildStep recipe. It is also
exactly what a Prefab cannot hold — a Prefab holds Primitives only. Anything
modelled on it can be hand-drawn into one World and placed nowhere else, which
is useless for content generated from Layer recipes by PrefabField and
RunScript.

A `PrimitivePropertySet` field is placeable everywhere a Primitive is, and costs
nothing to carry. But the boolean fold exists to split, clip and erase authored
shapes, so "where is the sound" would become a question about surviving faces
rather than about an authored point — and property-transparent Primitives
(ADR-0029) contribute no properties at all, so their emitters would need a rule
invented for them.

## Decision

An **AudioEmitter** is a point owned by exactly one Primitive, positioned by a
two-dimensional offset from that Primitive's position plus a height offset. It
is authored on the Primitive, transforms with it, and rides every carrier a
Primitive already rides: Prefabs hold it, PrefabField places it, RunScript
emits it.

Its world position is settled once, by **emitter capture**, when the World's
geometry is generated, and never changes afterwards. A sound that has to move
through a World is an entity, not an AudioEmitter.

Capture keeps an emitter only where all three hold:

1. the Arrangement has a solid face at the point,
2. the emitter's **parent** Primitive still contributes to the solid there, and
3. its derived height stands below that face's ceiling.

Existence is decided by the parent Primitive; elevation is not. The **derived
emitter height** is the authored offset added to the face's floor *after* floor
Wedges have raised it (ADR-0031) — the surface the player stands on — taken
from whichever Primitive won that face's properties. One authored offset
therefore resolves to different heights on different faces, which is the point:
a water drop authored at offset zero lands on the ground wherever it is placed.

An AudioEmitter carries an authored GUID. Because one authored emitter inside a
Prefab becomes many captured emitters when that Prefab is placed across many
Tiles, a captured emitter is identified by the pair *(emitter GUID, placement
key)*, where the placement key is the Tile coordinates and grid size that
PrefabField and RunScript already use to address a placement. Duplicating a
Prefab copies emitter GUIDs verbatim; the placement key is what separates
instances.

An emitter names its sound with an opaque, stable `soundId` that core never
resolves, exactly as `PrimitivePropertySet` references Sub-materials and
Embossing presets. No FMOD or Steam Audio type appears in core.

## Considered alternatives

**Authored on the Layer, like WorldTriggerLine.** Rejected: cheapest to build
and the one thing a Prefab can never carry, which defeats the purpose.

**A new derived output of LayerBuildSteps, sibling to Primitives.** Rejected:
it would amend ADR-0014's rule that a step's `execute()` may only add
Primitives, and grow every step type, the re-home logic and the serialization a
second output kind — to carry something an existing carrier holds perfectly
well.

**A `PrimitivePropertySet` field.** Rejected: the fold is a machine for
destroying the identity of authored shapes, and an emitter is a point, not a
regional property.

**Prefab vertex metadata.** Rejected: it is Prefab-only and script-facing,
nothing carries it through to `WorldData`, and it cannot express an emitter
outside a Prefab.

**Positional matching instead of a GUID.** Rejected: two identical emitters
close together are indistinguishable, and any legitimate move reads as a death
and a birth — a restart at the moment it is most audible.

## Consequences

- An emitter moves when its Primitive moves, at no cost, because it is
  expressed in the Primitive's frame.
- A Primitive animated by its `VertexTransformer` does **not** drag its emitter
  audibly. Capture happens once; the audible position is the captured one.
- Capture must run after detail geometry, because the derived height depends on
  floor Wedges. That fixes its place in `ArrangementWorldData` construction.
- The World regenerates on a schedule during play, not only while authoring, so
  every commit re-syncs emitters. Identity is what lets a surviving emitter keep
  its playing sound instead of restarting it.
- `core` gains a data definition and no behaviour. Everything that knows what a
  `soundId` means lives in the app.
- An emitter GUID created by a RunScript step must be **derived deterministically**
  from that step's serialized seed, never minted at random. ADR-0040 makes build
  scripts deterministic by construction, and a real GUID generator would break
  its central assertion — rebuild a Layer twice, get identical Primitives — while
  also defeating the identity this ADR establishes, since every rebuild would
  present every scripted emitter as a new one. Lua can neither supply nor set a
  GUID.
