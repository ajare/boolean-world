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
