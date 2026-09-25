# Phantom views Euclidean space through hidden Border apertures

**Status:** Accepted
**Partially supersedes:** ADR-0048 (global hidden-wall eligibility and shared collision behavior)

Phantom is a third stable built-in Zone (`phantom`, persistence identity 3).
It deliberately makes hidden, non-colliding Phantom/Euclidean Borders into
one-sided windows, not wall surfaces or Portal endpoints. Only an eye on the
non-solid side sees them; facing uses the unperturbed normal and eye position,
not look direction. Their exact unchipped outlines reveal normally rendered
Euclidean space behind the wall plane, without transforming the camera.
Everything outside is black. The nearest aperture owns overlapping pixels.

The aperture-only primary scene composites complete, plane-clipped Euclidean
views, including lighting, shadows, water and reflections. Ordinary Portals
inside those views retain Euclidean appearance. The primary scene has no World
surfaces, liquid, or world-space markers; camera underwater effects do not
apply to a Phantom player. Snapshot changes own aperture geometry; changing
Zone or camera does not rebuild the World or its buffers. Hidden walls remain
absent from Euclidean/Negative Space surfaces and shadow casting.

Phantom preserves feet elevation, discards vertical momentum, and allows basic
horizontal walking and looking. Geometry, steps, gravity, liquid, ordinary
Portals, and WorldTriggerLines do not interact with the Phantom player. The
engine-extent safety boundary remains. Rebuilding geometry preserves position,
elevation and Zone, even if every return aperture disappears.

Only an inward centre crossing of an eligible aperture can return to Euclidean.
The standing body must fit the aperture's vertical span and have a valid
Euclidean placement without snapping or teleporting; otherwise passage does
nothing. Reverse crossings, tangencies and isolated endpoint touches do
nothing. Collision strategy changes at the crossing before remaining movement,
so fast returns cannot tunnel through a wall beyond the aperture. Ordinary
physics resumes on return, including falling or swimming where appropriate.

An effective Phantom Border must be hidden and non-colliding. Invalid visible
openings receive authoring diagnostics and fail load/generation; a Phantom
Other Zone remains legal while dormant on a colliding or non-Border edge.
Phantom is available in Other Zone and preview selectors. Preview picking
selects the aperture rather than the Euclidean geometry behind it. Existing
map-load, map-entry and respawn initialization remains Euclidean.
