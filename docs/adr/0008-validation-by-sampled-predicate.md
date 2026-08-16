# ADR-0008: Validate by sampled predicate, not by diffing polygons

**Status:** Accepted
**Date:** 2026-08-16

## Context

ADR-0001 promises the new engine reproduces the old engine's solid region
exactly, and that promise is the rewrite's main safety net. ADR-0007 makes the
primitive set position-independent, so the two engines can be run on the same
world and compared — but the comparison method matters.

Diffing border polygon sets does not work. Two engines can describe an
identical region with different decompositions: extra collinear vertices at
T-junctions, a region emitted as two polygons instead of one, holes associated
differently, a different starting vertex. The old engine demonstrably does some
of this — `ClipperUtils::canonicalisePolygon` exists because polygon rotation
was producing spurious differences. A polygon diff would spend the port
chasing non-defects.

The two engines also cannot be run side by side behind one interface:
ADR-0004 gives them different output contracts, so neither can substitute for
the other at `WorldDataGenerator::getWorldData`. Comparison must be
harness-based.

## Decision

Compare the **solid predicate**, sampled.

Both engines can answer, for a point: is it solid, and which primitive owns it.
Sample that across a uniform grid over the world extents, plus random points,
plus points clustered near edges where disagreement is most likely. Report the
disagreement count, and the disagreeing positions so they can be opened
directly in the editor. Carry total solid area as a cheap scalar check.

Run with `setAlwaysUpdateVertices(true)` so both engines see identical
geometry (see ADR-0007).

**Sequence:**

1. **Arrangement core standalone**, with direct unit tests on degeneracies:
   coincident vertices, collinear overlapping edges, many edges through one
   point, and snap-rounding creating new incidences. All the new risk lives
   here (ADR-0002) and must not be validated only end to end.
2. **Comparison harness** in `experiments`, run over every world in the repo:
   `stress-test-1`, `gen-3`, `basic-test`, `bug-1`, `collision-issue-repro`,
   `duplicate-test`, `int-xor-test`, `z-optimisation`. Runs with
   `setAlwaysUpdateVertices(true)` and a pinned layer mask (ADR-0009).

   The 20-of-24 pre-existing failures in `experiments` — PSLG hierarchy
   assertions inherited from Willpower, recorded in the README — are
   **quarantined** before the harness lands. A harness living in a mostly-red
   target is a harness nobody reads.
3. **Integrate** as the generator; port consumers to the new contract.
4. **Delete** the old engine, `ZCallback`, and the z-bitfield.

**Pass criterion:** zero disagreements on all repo worlds, except those that
are known-bad repros — the `bug-*` and `*-issue-*` files — where the change is
reviewed manually and the new behaviour recorded.

## Consequences

- The comparison is tolerant of tessellation differences and tests the property
  actually promised, rather than its incidental encoding.
- Failure modes stay legible by scale: a semantic error lights up thousands of
  samples, a snap-rounding difference lights up a handful right at an edge.
- The repro worlds become the real result. If the new engine gets
  `bug-1.yaml` and `collision-issue-repro.yaml` right, that is the outcome the
  rewrite exists for.
- The harness is a deliverable, not a convenience — step 2 gates step 3.
- Sampling can miss features thinner than the sample spacing. Edge-clustered
  samples mitigate this but do not eliminate it; slivers narrower than the grid
  may go unnoticed. Accepted, since such features are below the resolution the
  game renders or collides at anyway.
