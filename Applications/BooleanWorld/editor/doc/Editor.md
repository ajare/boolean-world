# Editor
## Ghost primitive
The editor uses a "ghost" or "preview" primitive, in order to help create new primitives.  You can treat this as a normal primitive and move/rotate/scale etc
in the viewport.  You can then either hit "Create" in the Create Primitive panel, hit C to create a primitive from it, or Ctrl+C to clone it (with the ghost selected).

The ghost can be selected in the Clip Order view but cannot be manipulated in it: all manipulation must be in the Create Primitive view or directly in the viewport.
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
