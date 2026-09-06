## Problem Statement

The game has no spatial audio and, in practice, no audio at all. FMOD is compiled out of Willpower (`WILLPOWER_ENABLE_FMOD=OFF`), so `AudioSystem` is a no-op stub: the three `AudioBank` resources declared in `Resources.yaml` load their bytes and hand them to a function that discards them. `StatePlayBooleanWorld::updateAudio()` is empty. The only authored content is two music events. Nothing in the World model can express a sound at a place, so even with FMOD switched on there would be nothing to position.

Positioned sound also cannot simply be bolted on. Worlds here are derived from Layer recipes and regenerated on a repeating schedule during play, not only while authoring, so any acoustic data tied to geometry has a lifecycle problem that a hand-modelled level does not.

## Solution

Integrate Steam Audio as an FMOD Studio plugin, providing binaural direct sound, distance and air attenuation, occlusion and transmission, and real-time reflections rendered from traced impulse responses. No probes, no baking, no pathing (ADR-0042).

Sound sources are authored as **AudioEmitters** owned by Primitives, so they ride every carrier a Primitive already rides — Prefabs hold them, PrefabField places them, RunScript emits them — and are resolved to fixed world positions by **emitter capture** during World generation (ADR-0041).

Steam Audio is given a **custom scene**: it calls back into the game for every ray query, answered directly from `ArrangementWorldData`, so no acoustic geometry representation is ever built or maintained (ADR-0043).

## User Stories

1. As a world author, I want to place AudioEmitters on a Primitive with 2D and height offsets, so that a sound sits where the thing making it sits.
2. As a world author, I want an emitter to move with its Primitive, so that repositioning a machine repositions its hum.
3. As a world author, I want emitters inside Prefabs, so that placing a Prefab across a field places its sounds too.
4. As a build-script author, I want to create and edit AudioEmitters from Lua, so that scripted content can carry sound.
5. As a build-script author, I want scripted emitters to be reproducible across rebuilds, so that ADR-0040's determinism guarantee still holds.
6. As a world author, I want to see which emitters failed capture and why, so that a silently discarded sound is visible while authoring rather than missing in the game.
7. As a world author, I want an emitter's gizmo drawn at its derived height, so that what I see is where it will sound from.
8. As a sound designer, I want to open the FMOD Studio project and find the Steam Audio effects already available, so that no per-machine plugin install is needed.
9. As a sound designer, I want music routed away from Steam Audio entirely, so that the score is never spatialised.
10. As a sound designer, I want an event's intermittency authored in Studio, so that a drip's rhythm is tuned where it can be auditioned.
11. As a player, I want sounds to come from where they are, so that I can locate them by ear.
12. As a player, I want a wall between me and a sound to muffle it, so that space reads acoustically.
13. As a player, I want rooms to sound like rooms, so that reverb reflects the space I am in.
14. As a player wearing headphones, I want HRTF spatialisation, so that direction is genuinely audible; and as a player on speakers, I want it off, because it sounds worse there.
15. As a player, I want an audio quality setting, so that I can trade CPU against acoustic fidelity.
16. As a player, I want a sound that is still playing to keep playing when the world regenerates, so that ambience does not restart every few seconds.
17. As a developer, I want each acoustic feature independently switchable, so that a problem can be isolated.
18. As a developer, I want an in-game audio debug view, so that simulation state and budget are inspectable.
19. As a maintainer, I want Willpower's public headers free of FMOD, so that enabling FMOD cannot silently change class layout between the engine build and its consumers.
20. As a maintainer, I want `core` free of FMOD and Steam Audio, so that the World model stays a data definition.

## Implementation Decisions

### Foundation

- Decouple Willpower's public headers from `WP_APPLICATION_USE_FMOD` by forward-declaring FMOD types and holding pointers, so `sizeof(AudioSystem)` and `sizeof(AudioBankResource)` are identical in both configurations. The define becomes a private detail of two `.cpp` files. **(Implemented; awaiting submodule commit.)**
- Replace `WILLPOWER_FMOD_ROOT`'s assumed SDK layout (`api/core/inc`, and so on) with explicit include/lib/dll cache variables, so Willpower can consume the repo's vendored FMOD tree.
- Drive `WILLPOWER_ENABLE_FMOD` from a single BooleanWorld-level option passed through `cmake/Submodules.cmake`, whose configure step currently runs only when Willpower's `CMakeCache.txt` is absent.
- Pin FMOD to **2.03.14**, and pin the vendored Engine API and the Studio authoring tool to the same point release. FMOD banks are compatible with an equal or newer API, never an older one; the current state — Studio 2.03.14 building banks for a 2.03.07 runtime — is that failure waiting to happen.
- Replace the hardcoded `FMOD_SPEAKERMODE_5POINT1` with `FMOD_SPEAKERMODE_DEFAULT` plus a player-facing output setting — Headphones, Speakers, or Surround — where Headphones forces stereo and enables HRTF.
- Vendor Steam Audio binaries once under `vendor/`, and stage `phonon.dll`, `phonon_fmod.dll` and `phonon_fmod.plugin.js` into the FMOD Studio project's `Plugins/` directory at CMake configure time. That directory is gitignored, so the version cannot drift between what designers author against and what players hear.

### World model

- `AudioEmitter` is a `Serializable` in `core` with six fields: 2D offset, height offset, `soundId`, a GUID, and cull radius. `soundId` is opaque and never resolved by `core`, exactly as `PrimitivePropertySet` treats Sub-material and Emboss-preset ids. Cull radius is the authored maximum distance used for broad-phase culling and reflection-source ranking; it is separate because `EventDescription::getMinMaxDistance` does not report the Spatializer curve's maximum distance.
- Emitter capture runs during `ArrangementWorldData` construction, **after** detail geometry, since derived height depends on floor Wedges. An emitter survives only where the Arrangement has a solid face, its **parent** Primitive still contributes to the solid there, and its derived height stands below that face's ceiling.
- Derived emitter height is the authored offset added to the Wedge-raised floor of the containing face, taken from whichever Primitive **won** that face's properties. Existence is decided by the parent; elevation is not.
- A captured emitter is identified by *(emitter GUID, placement key)*, the placement key being the Tile coordinates and grid size that PrefabField and RunScript already use to address a placement.
- GUIDs created by a RunScript step are derived deterministically from that step's serialized seed. Lua can neither supply nor set a GUID.
- Add an Acoustic preset id to `SubMaterial`: an opaque stable reference resolved outside the World, describing absorption, scattering and transmission.

### Runtime

- All Steam Audio code lives in BooleanWorld. Willpower gains only what the foundation requires, plus an accessor for the core FMOD system.
- Initialise in the documented order: `System::loadPlugin("phonon_fmod.dll")` on the core system, create the Steam Audio context, `iplFMODInitialize`, create and set an HRTF, then `iplFMODSetSimulationSettings`.
- Supply Steam Audio a custom scene whose ray callbacks are answered from `ArrangementWorldData` — a 2D march over the rendered wall grid for walls, analytic plane intersections for floors and ceilings.
- Convert world to audio space with the renderer's existing mapping, `(x, elevation, -y)`, extracted into one shared function used by both the camera and the listener, and initialise FMOD with `FMOD_INIT_3D_RIGHTHANDED`. Listener orientation comes from the camera's own basis vectors. Listener velocity is zero; no Doppler.
- Run reflection simulation on a **dedicated thread**, not the concurrencpp executor that world generation uses. The thread takes its own `shared_ptr` to the `WorldData` snapshot per tick, so a commit landing mid-tick finishes against the old world and picks up the new one next tick.
- Re-sync emitters on each generation commit through the existing `registerGenerationCallback` hook. A surviving emitter keeps its `EventInstance`; a vanished one fades rather than cutting.
- Cull by the AudioEmitter's authored cull radius. Rank surviving sources for reflection simulation by distance normalised against that radius, capped, with hysteresis on entry and exit and a short fade on the reflection contribution. A source past the cap keeps direct path and occlusion and loses only reverb.
- An emitter's event runs whenever the listener is within range, starting from the beginning. Intermittency is authored inside the FMOD event.
- Quality presets are named, defined in `Game.yaml`, and bundle ray count, bounce count, impulse-response duration, ambisonic order, the concurrent reflection source cap, and the simulation update rate. Simulator maxima are allocated from the top preset so quality can change live.
- A single `Game.yaml` value decides whether quality may be modified live; when false, the F7 audio debug view is not exposed.
- Feature toggles — occlusion, transmission, reflections, air absorption — are implemented by controlling what the simulator computes and reports, not by changing the DSP graph. The plugin is always loaded: once an event carries a Spatializer, it is a hard dependency.
- Add the missing `requireOnlyChildren` check to the `Audio` section of `Game.yaml` parsing, and reject the meaningless combination of occlusion off with transmission on.

### FMOD Studio authoring

- Rename the `Themes` project to `BooleanWorld` and add a `World.bank` for diegetic events, keeping music in `Themes.bank`.
- Bus tree: `Master` splits into `Music` (no Steam Audio effects) and `World`. Each diegetic event carries **one** Steam Audio Spatializer, replacing FMOD's built-in 3D panner rather than supplementing it, with Occlusion and Transmission set to Simulation-Defined and Distance Attenuation set to Curve-Driven.
- Reflections use the **Hybrid** effect type — traced early reflections with a parametric late tail — which needs no Mixer Return bus.

## Testing Decisions

- Emitter capture is the highest-value test and needs no FMOD: a small World with known Primitives, asserting survival under each limb of the three-part rule and asserting derived height against the winning Primitive's Wedge-raised floor.
- The ray tracer is tested by property assertions — a ray between two points in one face hits nothing; a ray crossing a rendered wall hits it; a ray above `ceilingZ` hits the ceiling — plus a comparison harness against a brute-force reference over the same arrangement.
- Extend ADR-0040's existing "rebuild the same Layer twice and get identical Primitives" assertion to cover emitters and their derived GUIDs.
- Culling, ranking and hysteresis are pure functions over distance and maximum distance, tested table-driven.
- Extend the existing core serialization, run-script step, editor interaction and map-load test families rather than creating new low-level seams.
- No golden impulse-response snapshots: any quality tuning invalidates them, and a diff reports that something changed, never whether it improved.

## Out of Scope

- Probes, baked reverb, baked reflections, and pathing (ADR-0042).
- Moving sound sources. A sound that travels through the world is an entity, not an AudioEmitter, and entity-driven audio is separate work.
- Editor audio playback. The editor gets emitter visualisation and capture-failure feedback only; hearing the world means launching the game.
- Convolution and TrueAudio Next reflections, and therefore the Mixer Return bus.
- Non-Windows platforms. FMOD is Windows-only in this build, and Steam Audio follows it.
- Doppler.
- An Acoustic preset editing UI. The id is authored; the catalog is not editable in this work.
- Event parameters, per-emitter gain, and author-facing emitter names.

## Further Notes

One verification remains unresolved and can change scope: whether per-ray callback overhead outweighs the structural win decides whether ADR-0043 stands or falls back to a triangle-mesh `IPLScene`.

#386 audited the ray-query paths. `ImmutableAccelerationGrid` has no `mutable` state or lazy structures: construction fills its cell offsets and items, while its queries only read those arrays. `getCandidateItemsInBoundingArea` uses a caller-owned candidate vector, not grid scratch storage. `ArrangementWorldData` likewise has no `mutable` members or lazily-built query data; its constructor builds the triangle, floor-Wedge, collision-wall, and rendered-wall grids before publishing the snapshot. `getContainingFaceIndex`/`pointInTriangle`, floor and ceiling height queries, and the rendered-wall lookup used by `distanceToFirstWallCrossing` only read that completed state. Concurrent reads through `ArrangementWorldDataPtr` are therefore safe; no precomputation change or thread-local scratch is needed before the simulation thread is built.

Steam Audio 4.8.1's Windows x64 `phonon_fmod.dll` was loaded with `FMOD::System::loadPlugin` against the pinned FMOD 2.03.14 runtime in #384. It returned `FMOD_OK`, so the FMOD/Studio 2.03.14 pin stands.

#385 authored a Studio 2.03.14 event with the Steam Audio Spatializer as its only spatializer, Curve-Driven distance attenuation, and the Spatializer curve range set to 1–37. After building and loading the bank with FMOD 2.03.14, `EventDescription::getMinMaxDistance` returned 1–20: its default event range, not the Spatializer curve's authored maximum. The value is unusable for culling and reflection ranking, so `AudioEmitter` requires its sixth, authored cull-radius field.

The FMOD-enabled branch of `AudioSystem.cpp` has been edited but never compiled, because no FMOD Engine SDK is installed on this machine. Treat it as unverified until the first FMOD-enabled build.
