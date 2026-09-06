# RunScript Lua API

A `RunScript` LayerBuildStep executes one Lua script while its Layer is rebuilt. The script can inspect build-participating Primitives produced by earlier enabled steps and append new Primitives. It cannot modify earlier output or define Prefabs.

See [RunScript examples](run-script-examples.md) for complete scripts.

## Execution model

- Steps execute in Layer recipe order. Output from a `RunScript` is inserted at that step's position in the fold.
- Each execution starts with a fresh environment. Globals and included-module tables do not survive a rebuild and are not shared by two `RunScript` steps.
- The step's serialized seed initializes `math.random` before every execution. The same recipe, script, and seed therefore produce the same random sequence.
- A script has an instruction budget of 1,000,000 Lua instructions. Exceeding it fails the step.
- An error discards all output from the failed step and stops the Layer build. Output from preceding steps remains; later steps do not run. The attempted execution and its error are sent to the host's log under the LayerBuildStep and script names.
- Handles returned to Lua are borrowed and are valid only during the current execution. Lua never owns their C++ objects.
- Lua arrays returned by this API use normal one-based Lua indexing.

A `RunScript` step stores four authored values:

| Value | Meaning |
|---|---|
| Script | Name of the Lua script resource to execute. |
| Seed | Integer used to seed `math.random` at the start of execution. |
| Extra resources | Resource names used by the script but not discoverable from its source at serialization time. Add every such dependency here. Empty names and duplicates are removed from the World's dependency projection. |
| Step name | Optional label through which another script can find the step, where supported. Names need not be unique. |

## Execution context

Every execution receives one borrowed `RunScriptContext` as the global
`context`. It is valid only for that execution and exposes the capabilities of
the currently executing `RunScript` step without exposing the step's authored
configuration. Operations scoped to a build are methods on this object rather
than global functions.

### `context:create_primitive(type)`

Creates a mutable Primitive owned by the current `RunScript` step.

```lua
local primitive = context:create_primitive("Rectangle")
```

`type` is case-sensitive. Registered types are:

- `"Rectangle"`
- `"Regular"`
- `"Circle"`
- `"CircleSegment"`
- `"Torus"`
- `"TorusSegment"`
- `"Superformula"`
- `"Mesh"`

Only the common Primitive properties documented below are exposed. Use `context:create_mesh_primitive` rather than `create_primitive("Mesh")` to author Mesh geometry; other type-specific parameters are not exposed.

A created Primitive contributes nothing until passed to `context:place_primitive`. An unplaced Primitive is discarded after execution.

**Returns:** mutable `Primitive`.

**Errors:** fails if `type` is not registered.

### `context:create_mesh_primitive(points)`

Creates a mutable `MeshPrimitive` from one Ring. `points` is a Lua array of `{x, y}` World-plane coordinates. The Ring must contain at least three finite points and must be simple and non-degenerate; winding is normalized automatically.

```lua
local mesh = context:create_mesh_primitive({
    {0, 0}, {64, 0}, {64, 32}, {0, 32}
})
```

The new MeshPrimitive has a single Shell and defaults to the `"union"` operation. It supports all common Primitive methods plus the Mesh geometry methods documented below.

**Returns:** mutable `MeshPrimitive`.

**Errors:** fails for malformed point entries or invalid Ring geometry.

### `context:place_primitive(primitive)`

Appends a Primitive returned by `context:create_primitive` or `context:create_mesh_primitive` to the current step's output.

```lua
context:place_primitive(primitive)
```

A Primitive may be placed only once and must have been created by the currently executing step. Placement order is retained, subject to the Primitives' step-local priorities during the fold.

**Errors:** fails for `nil`, a foreign Primitive, or a Primitive already placed by this execution.

### `context:get_build_primitives()`

Returns an array of read-only `PrimitiveView` handles for build-participating output from preceding enabled steps.

```lua
for _, primitive in ipairs(context:get_build_primitives()) do
    print(primitive:get_type())
end
```

The array does **not** include Primitives already placed by the current script. Use `context:find_build_primitives_overlapping` when current-step placements must be included.

**Returns:** `PrimitiveView[]`, possibly empty.

### `context:find_build_primitives_overlapping(x, y, width, height)`

Returns read-only handles for Primitives whose bounds overlap the specified axis-aligned box.

```lua
local occupied = context:find_build_primitives_overlapping(x, y, width, height)
```

The query includes build-participating output from preceding steps and Primitives already placed by the current execution. It excludes the editor-only ghost Primitive. This makes it suitable for collision-avoiding placement. It is a bounds-only test, not an exact polygon intersection, and currently performs a linear scan.

**Returns:** `PrimitiveView[]`, possibly empty.

### `context:get_extents()`

Returns the owning Layer's axis-aligned extents as four values.

```lua
local x, y, width, height = context:get_extents()
```

Coordinates use the World plane: +X is right and +Y is up.

**Returns:** `x, y, width, height`.

### `context:find_define_prefabs(step_name)`

Finds the first LayerBuildStep with `step_name` and requires it to be a `DefinePrefabs` step.

```lua
local definitions = context:find_define_prefabs("environment prefabs")
```

Step names are optional and non-unique; the first matching step in recipe order wins.

**Returns:** read-only `DefinePrefabsStep`.

**Errors:** fails if no step has that name or the first match has another type.

### `context:find_primitive_field(step_name)`

Finds the first LayerBuildStep with `step_name` and requires it to be a `PrimitiveField` step.

```lua
local field = context:find_primitive_field("authored terrain")
```

Step names are optional and non-unique; the first matching step in recipe order wins. The returned view exposes the field's authored Primitives, whether or not those Primitives participate as prior build output at the current recipe position.

**Returns:** read-only `PrimitiveFieldStep`.

**Errors:** fails if no step has that name or the first match has another type.

### `context:get_tile(grid_size, x, y)`

Returns the integer coordinates of the Tile containing World position `(x, y)` on the requested grid. `grid_size` must be `32`, `64`, `128`, or `256`. All grids use the World origin as an intersection and half-open Tiles, matching `PrefabField`.

```lua
local tile_x, tile_y = context:get_tile(64, world_x, world_y)
```

**Returns:** `tile_x, tile_y`.

**Errors:** fails for an unsupported grid size, a non-finite World position, or coordinates outside the supported integer range.

### `context:place_prefab_instance(prefab, tile_x, tile_y, angle)`

Copies every Primitive in a `Prefab` into the current step's output and places the copies at the centre of Tile `(tile_x, tile_y)`. The Prefab's tile size automatically selects the 32, 64, 128, or 256 grid.

```lua
local definitions = context:find_define_prefabs("environment prefabs")
local arch = definitions:get_prefab("arch")
local tile_x, tile_y = context:get_tile(arch:get_tile_size(), 128, 64)
context:place_prefab_instance(arch, tile_x, tile_y, 90)
```

Tile coordinates must be integers. `angle` is a clockwise angle in degrees and must be exactly `0`, `90`, `180`, or `270`. Each call makes independent copies, rotates them about the Prefab origin, and leaves the source Prefab unchanged. Parent relationships between copied Prefab Primitives are preserved.

**Errors:** fails if the value is not a valid Prefab handle, either Tile coordinate is not an integer, or the angle is not an allowed quarter turn.

### `include(resource_name)`

Executes a manifest-declared `LuaScript` dependency and returns the table that
it returns. `resource_name` is the dependency's canonical qualified resource
name, such as `"World/Foo"`; it is not a file path or dependency id.

```lua
local geometry = include("World/Geometry")
local primitive = geometry.make_rectangle(context, 0, 0, 64, 32)
```

An included script must return exactly one table:

```lua
local M = {}

function M.make_rectangle(context, x, y, width, height)
    local primitive = context:create_primitive("Rectangle")
    primitive:set_position(x, y)
    primitive:set_size(width, height)
    return primitive
end

return M
```

The root `LuaScript` must declare the included `LuaScript` through Willpower's
resource dependency graph. Because a Willpower resource with dependencies is
composite, its own source is supplied as a named `TextFile` dependency:

```yaml
- type: "TextFile"
  name: "GenerateWorldSource"
  location: "generate-world.lua"
- type: "LuaScript"
  name: "Geometry"
  location: "geometry.lua"
- type: "LuaScript"
  name: "GenerateWorld"
  DependentResources:
    DependentResource:
      - id: "Source"
        ref: "GenerateWorldSource"
      - ref: "Geometry"
```

Only transitive `LuaScript` dependencies of the root may be included. An
undeclared name, an include cycle, or a script that does not return a table
fails the step. Repeatedly including one resource during an execution returns
the same table; the cache is discarded at the end of that execution. Included
chunks use the same Restricted environment and instruction budget as the root.

### `print(...)`

Converts arguments with `tostring`, joins them with tabs, and sends one completed line to the host's log for the executing script.

```lua
print("placed", count, "primitives")
```

### `dprint(message)`

Sends the string `message` to a host-provided debug log. The default sink is
a no-op, so `dprint` produces no output in the game. The editor supplies a
sink which appends the message to the executing LayerBuildStep's Script Log
tab.

```lua
dprint("candidate count: " .. #candidates)
```

Exactly one string argument is required.

## Mutable `Primitive`

Returned only by `context:create_primitive`.

| Method | Description |
|---|---|
| `get_type()` | Returns the case-sensitive registered type name. |
| `add_audio_emitter()` | Adds and returns an `AudioEmitter` with zero offsets and an empty sound id. |
| `add_audio_emitter(x, y, height_offset, sound_id)` | Adds and returns an `AudioEmitter` with the supplied local offset, floor-relative height offset, and opaque sound id. |
| `get_audio_emitters()` | Returns the mutable `AudioEmitter[]` owned by this Primitive, in creation order. |
| `remove_audio_emitter(emitter)` | Removes an emitter returned by this Primitive and returns whether it was still present. |
| `set_position(x, y)` | Sets the Primitive's position in the World plane. |
| `get_position()` | Returns `x, y`. |
| `set_transform_offset(x, y)` | Sets the local transform origin relative to the Primitive's position. |
| `get_transform_offset()` | Returns the transform-origin offset as `x, y`. |
| `set_orientation(angle)` | Sets the Primitive's orientation in degrees. |
| `get_orientation()` | Returns the orientation in degrees. |
| `set_follow_orbit_angle(follow)` | Chooses whether the Primitive's local orientation follows its orbit angle. |
| `get_follow_orbit_angle()` | Returns whether local orientation follows orbit angle. |
| `set_influence_eye_origin_offset(x, y)` | Sets the influence eye's offset from the Primitive's position. |
| `get_influence_eye_origin_offset()` | Returns the influence eye's offset as `x, y`. |
| `get_influence_eye_origin_position()` | Returns the influence eye's World-plane position as `x, y`. |
| `set_influence_eye_angle_offset(angle)` | Sets the influence eye's angle offset in degrees. |
| `get_influence_eye_angle_offset()` | Returns the influence eye's angle offset in degrees. |
| `is_static()` | Returns whether the Primitive's vertex transformation is static. |
| `set_size(width, height)` | Sets the common Primitive size. Its exact geometric meaning depends on the type. |
| `get_size()` | Returns `width, height`. |
| `set_priority(priority)` | Sets the step-local integer priority. Use the authored range `0`–`255`; lower values fold earlier within this step. Layer and step order take precedence. |
| `get_priority()` | Returns the priority as an integer. |
| `set_operation(operation)` | Sets `"union"`, `"intersection"`, `"difference"`, or `"xor"`. Values are case-sensitive. |
| `get_operation()` | Returns the operation as one of those lowercase strings. |

Only these common properties and inherited spatial-transform properties are currently scriptable. Animation curves and transform flows, fill rule, materials, surface properties, type-specific shape parameters, and parentage are not exposed. Mutable Mesh geometry is available only on a `MeshPrimitive` returned by `context:create_mesh_primitive`; Prefabs separately expose read-only annotated vertices and edges.

## Mutable `MeshPrimitive`

Returned only by `context:create_mesh_primitive`. It supports every mutable `Primitive` method above. Geometry uses the editor's Mesh topology and World-plane coordinates. Vertex, Edge, and Polygon arguments are zero-based topology ids, not Lua array indices; scripts are expected to know the ids for the topology they construct.

Every operation validates the complete Ring and containment hierarchy. A refused operation returns `false` or `nil` and leaves the Mesh unchanged. Supplying malformed or geometrically invalid Ring points raises an error.

| Method | Description |
|---|---|
| `move_vertex_to(vertex_id, x, y)` | Moves one Vertex to an absolute World-plane position. Returns whether accepted. |
| `move_vertex(vertex_id, dx, dy)` | Translates one Vertex. Returns whether accepted. |
| `move_edge(edge_id, dx, dy)` | Translates both vertices of one Edge. Returns whether accepted. |
| `move_polygon(polygon_id, dx, dy)` | Translates one Ring, without implicitly moving descendants. Returns whether accepted. |
| `split_edge(edge_id)` | Splits an Edge at its midpoint and returns the new Vertex id, or `nil`. |
| `split_edge(edge_id, t)` | Splits an Edge at the fraction `t`, strictly between zero and one, and returns the new Vertex id, or `nil`. |
| `slice_polygon(polygon_id, first_vertex_id, second_vertex_id)` | Divides a Shell or Island along a valid chord between two non-adjacent vertices. Returns whether accepted. |
| `remove_vertex(vertex_id)` | Removes a Vertex and heals its Ring. Returns whether accepted. |
| `remove_edge(edge_id)` | Welds a one-sided Edge's endpoints or merges compatible sibling Rings across a two-sided Edge. Returns whether accepted. |
| `remove_polygon(polygon_id)` | Removes a Ring and its structurally contained descendants. Returns whether accepted. |
| `add_shell(points)` | Adds a root Shell Ring and returns its Polygon id. |
| `add_hole(filled_polygon_id, points)` | Adds a Hole to a Shell or Island and returns its Polygon id, or `nil` if the parent id is unsuitable. |
| `add_island(hole_polygon_id, points)` | Adds an Island to a Hole and returns its Polygon id, or `nil` if the parent id is unsuitable. |
| `fill_hole(hole_polygon_id)` | Retains a Hole and fills it with a welded Island, wrapping existing immediate Islands as Holes. Returns the new Island's Polygon id, or `nil`. |

`points` in the add methods has the same `{{x, y}, ...}` form as `create_mesh_primitive`.

## Mutable `AudioEmitter`

Returned by `add_audio_emitter` and `get_audio_emitters` on a mutable
`Primitive` or `MeshPrimitive`. Its GUID is derived deterministically from the
RunScript step's serialized seed and the emitter's creation order. Lua cannot
supply, read, or change that identity.

| Method | Description |
|---|---|
| `get_offset()` | Returns the Primitive-local World-plane offset as `x, y`. |
| `set_offset(x, y)` | Sets the Primitive-local World-plane offset. |
| `get_height_offset()` | Returns the offset above the generated floor. |
| `set_height_offset(height)` | Sets the offset above the generated floor. |
| `get_sound_id()` | Returns the opaque sound id. |
| `set_sound_id(sound_id)` | Sets the opaque sound id. |

An emitter handle remains usable when more emitters are added to its Primitive.
After that emitter is removed, using the stale handle fails the script.

## Read-only `PrimitiveView`

Returned by `context:get_build_primitives`, `context:find_build_primitives_overlapping`, and `PrimitiveFieldStep:get_primitives`. It has no setters.

| Method | Returns |
|---|---|
| `get_type()` | Primitive type name. |
| `get_position()` | `x, y`. |
| `get_transform_offset()` | Transform-origin offset as `x, y`. |
| `get_orientation()` | Orientation in degrees. |
| `get_follow_orbit_angle()` | Whether local orientation follows orbit angle. |
| `get_influence_eye_origin_offset()` | Influence-eye offset as `x, y`. |
| `get_influence_eye_origin_position()` | Influence-eye World-plane position as `x, y`. |
| `get_influence_eye_angle_offset()` | Influence-eye angle offset in degrees. |
| `is_static()` | Whether the Primitive's vertex transformation is static. |
| `get_size()` | `width, height`. |
| `get_priority()` | Step-local integer priority. |
| `get_operation()` | `"union"`, `"intersection"`, `"difference"`, or `"xor"`. |

Calling a mutable `Primitive` method such as `set_position` on a `PrimitiveView` fails the script.

## `DefinePrefabsStep`

A read-only view returned by `context:find_define_prefabs`.

### `get_prefab(name)`

Returns the first Prefab in the definition step with the given name.

```lua
local prefab = definitions:get_prefab("rock")
```

Prefab names need not be unique; the first match wins.

**Returns:** read-only `Prefab`.

**Errors:** fails if no Prefab has that name.

### `get_prefabs()`

Returns every Prefab in authored collection order as an array of read-only
`Prefab` handles.

```lua
local prefabs = definitions:get_prefabs()
```

### `get_prefabs_with_tags(tags)`

Returns the Prefabs containing every requested tag, preserving authored
collection order. Matching is case-insensitive. An empty tag array returns all
Prefabs.

```lua
local outdoor_rocks =
    definitions:get_prefabs_with_tags({"outdoor", "rock"})
```

`tags` must be a dense array of strings. Each tag may contain only ASCII
letters, digits, underscores, and hyphens. Invalid tags or malformed tables
fail the script.

## `Prefab`

A read-only handle returned by `DefinePrefabsStep:get_prefab`.

### `get_name()`

Returns the Prefab's authored name.

### `get_tile_size()`

Returns the Prefab's authored tile size as `32`, `64`, `128`, or `256`.

### `get_tags()`

Returns the Prefab's canonical lowercase tags as a sorted array.

```lua
local tags = prefab:get_tags()
```

### `get_metadata_vertices()`

Returns every annotated Prefab vertex as an array of read-only `PrefabVertex`
values. Vertices are ordered by Prefab Primitive, Ring, and vertex order, with
welded Ring occurrences returned once per Primitive. Positions are in Prefab
space.

### `get_vertices_with_metadata(metadata)`

Returns annotated vertices whose metadata contains every supplied key with
exactly the supplied string value. An empty table returns every annotated
vertex.

```lua
local spawns = prefab:get_vertices_with_metadata({
    kind = "spawn",
    team = "blue"
})
```

Both keys and values must be strings, and keys cannot be empty. Metadata
matching is case-sensitive.

### `get_metadata_edges()`

Returns every annotated Prefab edge as an array of read-only `PrefabEdge`
values. Edges are ordered by Prefab Primitive, Ring, and edge order, with
welded Ring occurrences returned once per Primitive. Endpoints are in Prefab
space.

### `get_edges_with_metadata(metadata)`

Returns annotated edges whose metadata contains every supplied key with
exactly the supplied string value. An empty table returns every annotated
edge. The filter follows the same validation and case-sensitive matching rules
as `get_vertices_with_metadata`.

```lua
local entrances = prefab:get_edges_with_metadata({kind = "entrance"})
```

A script cannot inspect or mutate the Prefab's source Primitives, tags, or
vertex or edge metadata. It can pass the handle to
`context:place_prefab_instance`.

## `PrefabVertex`

A read-only value returned by a Prefab vertex-metadata query.

### `get_position()`

Returns the vertex's Prefab-space position as `x, y`.

### `get_metadata()`

Returns a detached copy of the vertex's string key/value metadata as a Lua
table. Changing that table does not modify the Prefab.

## `PrefabEdge`

A read-only value returned by a Prefab edge-metadata query.

### `get_endpoints()`

Returns the edge's Prefab-space endpoints as `x1, y1, x2, y2`.

### `get_metadata()`

Returns a detached copy of the edge's string key/value metadata as a Lua table.
Changing that table does not modify the Prefab.

## `PrimitiveFieldStep`

A read-only view returned by `context:find_primitive_field`.

### `get_primitives()`

Returns the field's authored Primitives as an array of read-only `PrimitiveView` handles.

```lua
local primitives = field:get_primitives()
```

## Available Lua libraries

Build scripts receive Lua's base functions and the `table`, `string`, and `math` libraries. `print` is replaced with the host-logged version described above.

The available base names are:

`_VERSION`, `assert`, `error`, `getmetatable`, `ipairs`, `next`, `pairs`, `pcall`, `rawequal`, `rawget`, `rawlen`, `rawset`, `select`, `setmetatable`, `tonumber`, `tostring`, `type`, `warn`, and `xpcall`.

The following are deliberately unavailable to keep builds independent of files, modules, the clock, and persistent process state:

- `load`, `loadfile`, and `dofile`
- `collectgarbage`
- `io`, `os`, and `debug`
- `package` and `require` (use the resource-backed `include()` instead)
- the `coroutine` library

`math.random` is seeded automatically from the `RunScript` step. Scripts normally should not call `math.randomseed` themselves.

Do not rely on `pairs` order for hash-keyed tables. Sort keys explicitly when their traversal order affects output.
