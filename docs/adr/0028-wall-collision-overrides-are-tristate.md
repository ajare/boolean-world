# ADR-0028: Wall collision overrides are tri-state

**Status:** Accepted
**Date:** 2026-08-29

## Context

ADR-0022 made every eligible Mesh edge default to an authored
`collides = true`. Converting a procedural Primitive to Mesh therefore changed
its collision semantics: generated Step walls that were physically traversable
became blocking merely because the new Mesh edges carried the default flag.
A boolean cannot distinguish that untouched default from an author's explicit
choice.

Collision also has generated truth that authoring must not erase. Border walls
block by default, while Step walls block when their floor step exceeds the
World's threshold or their shared clearance is below player height.

## Decision

Supersede ADR-0022's boolean override with a per-External-edge tri-state:
**Unset**, **Collides**, or **Doesn't collide**.

Unset delegates to generation. Generated collision is true for Border walls
and false for Step walls before Step-specific physical constraints are applied.
An explicit value replaces that generated Border/Step default. A Step wall
nevertheless blocks when its floor step is too tall or its clearance is
insufficient; all authored and physical reasons must be false for passage.

When collinear edges contribute to one generated edge, Unset contributes no
override and Doesn't collide dominates Collides. Structural Mesh edits inherit
the complete tri-state in the same places that inherited the former boolean.

Persist a format marker with Mesh edge flags. Legacy `collides = true` is
migrated to Unset because it was also the untouched default; legacy
`collides = false` remains an explicit Doesn't collide.

## Consequences

- Converting an ordinary Primitive to Mesh no longer makes traversable Step
  walls block.
- Authors can explicitly open a Border or block an otherwise traversable Step.
- Authors cannot bypass maximum floor-step height or minimum-clearance safety.
- A legacy explicit Collides choice is indistinguishable from the old default
  and therefore migrates to Unset.
- ADR-0006's directional maximum-step rule remains in force.
