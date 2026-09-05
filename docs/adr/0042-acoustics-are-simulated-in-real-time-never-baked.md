# ADR-0042: Acoustics are simulated in real time, never baked

**Status:** Accepted
**Date:** 2026-09-05
**Relates to:** ADR-0041 (AudioEmitters are captured at generation), ADR-0005 (`WorldData` is an immutable snapshot)

## Context

Steam Audio offers four largely independent capabilities: binaural direct sound,
distance and air attenuation, occlusion and transmission, and indirect sound —
reflections and pathing. The indirect tier is conventionally driven by *probes*:
points scattered through open space, with visibility and reflection responses
computed offline and stored as an asset tied to one specific piece of geometry.

That model assumes hand-modelled levels that change rarely. This project's
worlds are derived from Layer recipes and regenerated constantly:
`DynamicWorldDataGenerator` runs generation on a repeating asynchronous
schedule, and the schedule is started by the *play* state, not only by the
editor. Any probe batch would be invalidated every few seconds, during
gameplay.

The Wayfinder mesh is the closest precedent and it is a warning rather than a
template: a derived spatial structure built inside `ArrangementWorldData`'s
constructor behind a flag, and disabled by default because building it on that
loop was already too expensive. Reflection baking is not in the same order of
magnitude as a navmesh build.

## Decision

No acoustic data is ever baked. There are no probes, no probe batches, and no
generation-time acoustic computation beyond emitter capture (ADR-0041).

Reflections are simulated in real time: rays are traced from the live source and
listener positions each simulation tick, producing a genuine impulse response,
rendered with the **Hybrid** reflection effect type — traced early reflections
with a parametric late tail.

**Pathing is consequently out of scope.** Pathing is a graph search and the
graph is probes; there is no probe-free pathing. Sounds around a corner are
correctly occluded and correctly reverberant, but arrive from the direction of
the wall rather than the doorway.

Simulation cost is bounded by a player-facing quality preset, configured in
`Game.yaml` and adjustable live through an audio debug view, which bundles ray
count, bounce count, impulse-response duration, ambisonic order, the concurrent
reflection source cap, and the simulation update rate.

## Considered alternatives

**Baked reverb per static source, computed at World generation.** This was the
original intent and it is what ADR-0041's capture rules were designed to serve.
Rejected once the generation cadence was understood: a bake measured in seconds
to minutes cannot ride a loop that runs every few seconds during play, and
gating it behind a toggle produces a switch between "too slow to author with"
and "a feature nobody turns on".

**Listener-centric baked reverb.** One dataset over a probe batch, far cheaper
than per-source baking. Rejected with the rest of baking, for the same
lifecycle reason.

**Incremental baking** — rebaking only probes near changed geometry. Rejected
as premature: substantially the most complex option, justified only by
measurements nobody has taken.

**Analytic reverb parameters derived from the Arrangement** — room area,
perimeter, absorption — driving an ordinary FMOD reverb. Trivially cheap and a
reasonable fit for a 2.5D world, but it discards real impulse responses, which
is the thing real-time simulation was wanted for.

**Convolution reflections.** Rejected in favour of Hybrid: under a real-time ray
budget the late tail is short and noisy anyway, so paying convolution's price to
render a poor tail accurately is the worst of both. Hybrid also removes the need
for a Mixer Return bus, which applies only to Convolution and TrueAudio Next.

## Consequences

- World generation carries no acoustic cost beyond emitter capture, so the
  authoring loop is unaffected by audio.
- Reverb quality is bounded by a per-tick ray budget rather than by an offline
  one. Early reflections — which carry the room's character — are fine; the late
  tail is approximated.
- Real-time reflections partly recover what pathing would have given: rays do
  find nearby openings, so a sound in the next room contributes early
  reflections arriving through the doorway. The case that stays unsolved is a
  distant opening reached only after several bounces.
- The CPU cost is per-frame and must be budgeted. Distance culling alone does
  not bound it — a hard cap on concurrently simulated reflection sources is
  required, and sources are ranked by distance normalised against each event's
  authored maximum distance, with hysteresis and a short fade so entering and
  leaving the set is not audible.
- A source past the cap keeps its direct path and occlusion and loses only its
  reverb, which makes budget enforcement a graceful-degradation problem rather
  than a correctness one.
