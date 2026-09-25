# Editor
## Ghost primitive
The editor uses a "ghost" or "preview" primitive, in order to help create new primitives.  You can treat this as a normal primitive and move/rotate/scale etc
in the viewport.  You can then either hit "Create" in the Create Primitive panel, hit C to create a primitive from it, or Ctrl+C to clone it (with the ghost selected).

The ghost can be selected in the Clip Order view but cannot be manipulated in it: all manipulation must be in the Create Primitive view or directly in the viewport.

## Primitive fields
Choose **Edit > Generate Primitive Field…** to author a reproducible batch of
fitted primitives. Set minimum site spacing, seed, Lloyd relaxation iterations,
requested batch maximum, cell occupancy, hole chance, overlap, and the eligible
primitive types. Cell occupancy is the seeded percentage chance that a Voronoi cell
receives a primitive, allowing reproducible gaps in the field. Every layout has
a pinned site at `[0, 0]`, and that site always receives a primitive regardless
of the occupancy percentage. Hole chance is the seeded percentage chance that
an occupied non-origin cell also receives a half-size Difference Regular
primitive with 3, 4, or 6 sides. Empty cells and the `[0, 0]` origin cell never
receive holes. The live approximate count is uncapped and requires no Poisson/Voronoi generation; use it
and the warning above 2,000 to judge request cost. Circle fields use resolution
`0.5`.

This is a two-stage workflow: **Generate Layout** creates only an editor preview
(sites, clipped cell edges, and translucent fitted primitive outlines), then
**Place Primitives** appends that exact centre-outward ordered batch as one
undoable action. The effective cap is the smaller of the requested maximum and
the remaining engine capacity; existing authored primitives and the ghost
consume that capacity. The dialog reports when sampling was capped. Delete
primitives before generating when no capacity remains.

Spacing, seed, Lloyd iterations, maximum, cell occupancy, hole chance, overlap,
and enabled types remain at their last-used values only for the current editor process. A
fixed seed and the same controls/world extents reproduce the same preview.
Layout inputs require generation again; changing cell occupancy, hole chance,
overlap, or enabled types rebuilds or refits the primitive preview without
rerunning layout generation. Closing the dialog or replacing the world discards all preview
geometry, which is never saved or included in world generation.

## Portals and mirrors
Portals belong permanently to the active Layer. Choose **Create Mirror Portal**
to add a Portal with a default name such as `Portal 1`. A new Portal targets
itself, so it works as a Mirror Portal as soon as its authored aperture resolves
against a visible wall. Mirror Portal views and player movement are true planar
reflections: left and right reverse while elevation and World-up remain
unchanged. Crossing retains the reflected view rather than snapping back to an
ordinary camera. Horizontal mouse and strafe controls follow that view; crossing
another mirror restores ordinary handedness. Mirrors do not transport Liquid.

Names are trimmed, must be non-empty, and are unique within a Layer without
regard to case. A name is only a label: renaming a Portal does not change its
identity or any targets that refer to it. The target list contains only Portals
on the same Layer. Selecting the Portal itself keeps it as a mirror; selecting
another Portal creates a directed target relationship.

For a multi-Portal route, set the targets to one closed cycle. For example,
choose `B` as `A`'s target, `C` as `B`'s target, and `A` as `C`'s target to make
`A → B → C → A`. There is no authored ordering control—the cycle is inferred
from those targets. While editing, chains and branches are allowed and saved,
but their whole connected component is inactive. Every member must have exactly
one incoming target before views, traversal, lighting, shadows, or Liquid can
use that component. An invalid component does not deactivate an unrelated valid
cycle on the Layer.

The selected Portal reports target-graph and aperture diagnostics separately.
After the target graph forms a cycle, every authored aperture in it must resolve
against visible wall coverage, have enough player clearance, and have the same
height. The generated resolved apertures use the cycle's smallest authored
width; the Width control is not changed. Deleting a targeted Portal resets its
surviving incoming references to self-targeting mirrors. Portal creation,
renaming, retargeting, aperture edits, and deletion are undoable.

## Keyboard shortcuts
| Modifier | Key/Mouse     | Action                               |
| -------- | ------------- | ------------------------------------ |
| Ctrl     | N             | New world                            |
| Ctrl     | O             | Open world from file                 |
| Ctrl     | S             | Save world to file                   |
| Ctrl     | Z             | Undo last action                     |
| Ctrl     | Y             | Redo last undone action              |
|          | C             | Create primitive from ghost          |
| Ctrl     | C             | Clone selected primitive             |
|          | Del           | Delete selected primitive            |
|          | G             | Toggle grid                          |
| Ctrl     | G             | Toggle ghost primitive               |
|          | F1            | Show (this) help screen              |
|          | F11           | Toggle export mode (hide panels)     |
|          | Left cursor   | Move view left                       |
|          | Right cursor  | Move view right                      |
|          | Up cursor     | Move view up                         |
|          | Down cursor   | Move view down                       |
| Shift    | Cursor        | Move view fast                       |
| Ctrl     | Cursor        | Move view one screen at a time       |
| Alt      | Mouse X       | Rotate selected primitive            |
| Ctrl+Alt | Mouse Y       | Scale selected primitive             |
## Known issues
- Help (this) screen does not show when selected from the menu, for now only F1 works.
