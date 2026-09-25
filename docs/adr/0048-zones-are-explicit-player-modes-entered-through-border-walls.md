# Zones are explicit player modes entered through Border walls

**Status:** Partially superseded by [ADR-0049](0049-phantom-views-euclidean-space-through-hidden-border-apertures.md) for Phantom aperture visibility and collision
**Date:** 2026-09-24

## Context

A player can pass through a Border wall whose authored collision override is
`Doesn't collide`. Position containment alone cannot describe what should
happen after that passage: future spaces may apply different rendering and
collision rules, Portals and forced relocation need not imply a mode change,
and the same non-solid Arrangement region may be reached through walls naming
different modes.

Wall collision and visibility are authored on a MeshPrimitive's External edge,
while ArrangementWalls are immutable generated geometry with no stable authored
identity (ADR-0028). Zone authoring must preserve that boundary.

## Decision

A **Zone** is a World-owned, stably identified strategy for player interaction
with World rendering and collision. The player carries exactly one current
`ZoneId`; it is explicit state and is never inferred from Arrangement
containment. The initial built-in Zones are Euclidean and Negative Space, and
loading fails when authored content references an unregistered Zone strategy.

Every External edge carries an authored Other Zone, defaulting to Negative
Space and retained while dormant. When that edge contributes to a non-colliding
Border wall, generation records Euclidean on the solid side and the Other Zone
on the non-solid side; the Other Zone may itself be Euclidean. If contributors
conflict, the highest-precedence property-contributing edge wins. Internal
edges and non-Border walls have no effective Zone relationship.

The player's centre assigns the destination side's Zone when its actual,
collision-resolved swept movement crosses the interior of such a Border
segment. Crossings are processed in travel order. Tangency and endpoint contact
do not cross; successful Portal traversal, teleportation, rebuild recovery,
and other relocation preserve Zone. An invisible Border can still change Zone
because wall visibility remains independent of Zone and collision.

Zone behavior is dispatched at runtime and never rebuilds the Arrangement or
render buffers. Euclidean rendering draws front-facing solid World surfaces
with their authored treatment and omits their back faces. Negative Space draws
front faces with their authored treatment and back faces with the reserved
matte-white treatment. Facing uses each active camera and the unperturbed
geometric normal; Portal and reflection views use the player's Zone. Liquid
remains two-sided in Euclidean so a submerged player retains its interface.
Zone-specific camera appearance does not change shadow casting. The editor's
3D preview exposes an ephemeral Zone selector and makes surface picking match
the selected strategy.

The initial Euclidean and Negative Space collision strategies deliberately use
the same generated collision walls: ordinary Borders block from either side
and non-colliding Borders remain passable. The strategy boundary exists so
future Zones can differ. Negative Space movement remains within the engine's
fixed World coordinate extent through an invisible hard boundary.

Current Zone is Boolean World-specific runtime player state. It survives
geometry rebuilds, starts as Euclidean on map load, respawn, and map entry, and
is not serialized into authored World data. NPCs and other entities do not yet
carry a Zone.

## Consequences

- A player's Zone can intentionally disagree with geometric containment.
- One non-solid region can be experienced under different Zone strategies,
  depending on which Border the player crossed.
- Existing `Doesn't collide` edges migrate with Negative Space as their Other
  Zone, while authors can select Euclidean for an ordinary passable opening.
- Wall visibility remains global: hidden walls render nowhere; visible walls
  are treated according to the current Zone.
- Additional Zones require a registered strategy and stable identity, but do
  not require replacing wall-side references or deriving state from position.
