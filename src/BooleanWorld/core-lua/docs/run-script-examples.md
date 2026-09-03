# RunScript example scripts

These scripts run as the Lua resource selected by a `RunScript` LayerBuildStep. The complete API is documented in [RunScript Lua API](api.md).

## Place one rectangle

Creates an additive 64×32 rectangle centred at `(128, 64)`.

```lua
local room = context:create_primitive("Rectangle")
room:set_size(64, 32)
room:set_position(128, 64)
room:set_operation("union")
room:set_priority(0)
context:place_primitive(room)
```

## Create and edit a MeshPrimitive

Creates one MeshPrimitive from a single Ring, splits Edge `0`, moves the new Vertex, and adds a Hole to Polygon `0`.

```lua
local room = context:create_mesh_primitive({
    {0, 0}, {64, 0}, {64, 64}, {0, 64}
})

local new_vertex = room:split_edge(0)
room:move_vertex(new_vertex, 0, -8)
room:add_hole(0, {
    {16, 16}, {48, 16}, {48, 48}, {16, 48}
})

context:place_primitive(room)
```

Vertex, Edge, and Polygon ids are zero-based topology ids. See the API reference for every available geometry operation and its return value.

## Build a doorway with boolean operations

Places a room and then subtracts a doorway. Priorities order the two Primitives within this `RunScript` step.

```lua
local room = context:create_primitive("Rectangle")
room:set_size(128, 96)
room:set_position(0, 0)
room:set_operation("union")
room:set_priority(0)
context:place_primitive(room)

local doorway = context:create_primitive("Rectangle")
doorway:set_size(24, 16)
doorway:set_position(0, -40)
doorway:set_operation("difference")
doorway:set_priority(1)
context:place_primitive(doorway)
```

## Deterministic random scatter

Set the `RunScript` step's seed to reroll this scatter. Rebuilding with the same seed reproduces the same positions.

```lua
local x, y, width, height = context:get_extents()

for i = 1, 40 do
    local rock = context:create_primitive("Circle")
    rock:set_size(math.random(4, 12), math.random(4, 12))
    rock:set_position(
        x + math.random() * width,
        y + math.random() * height
    )
    context:place_primitive(rock)
end
```

## Scatter without overlapping existing geometry

The overlap query sees Primitives from preceding steps and objects already placed by this script. Bounds tests are conservative, so spacing should allow some margin.

```lua
local x, y, width, height = context:get_extents()
local spacing = 64
local half = spacing / 2

for cy = y + half, y + height - half, spacing do
    for cx = x + half, x + width - half, spacing do
        local occupied = context:find_build_primitives_overlapping(
            cx - half, cy - half, spacing, spacing)

        if #occupied == 0 and math.random() < 0.35 then
            local pillar = context:create_primitive("Circle")
            pillar:set_size(16, 16)
            pillar:set_position(cx, cy)
            context:place_primitive(pillar)
        end
    end
end
```

## React to preceding steps

Places one marker for every build-participating Primitive produced by earlier enabled steps. The prior handles are read-only.

```lua
local priors = context:get_build_primitives()

for index, prior in ipairs(priors) do
    local x, y = prior:get_position()
    local width, height = prior:get_size()

    print(index, prior:get_type(), prior:get_operation(), x, y)

    local marker = context:create_primitive("Circle")
    marker:set_size(math.min(width, height) * 0.1,
                    math.min(width, height) * 0.1)
    marker:set_position(x, y)
    marker:set_priority(255)
    context:place_primitive(marker)
end
```

## Read a named PrimitiveField

Name a `PrimitiveField` step `source shapes` before using this script. This accesses the field's authored Primitives through read-only handles.

```lua
local source = context:find_primitive_field("source shapes")
local source_primitives = source:get_primitives()

for _, primitive in ipairs(source_primitives) do
    local x, y = primitive:get_position()

    local copy_marker = context:create_primitive("Rectangle")
    copy_marker:set_size(4, 4)
    copy_marker:set_position(x + 16, y)
    context:place_primitive(copy_marker)
end
```

This example places markers; it does not clone the source Primitives. The current API exposes no general Primitive clone operation.

## Stamp a named Prefab

Create a `DefinePrefabs` step named `environment prefabs` containing a Prefab named `arch`. The script makes independent transformed copies and does not change the source Prefab.

```lua
local definitions = context:find_define_prefabs("environment prefabs")
local arch = definitions:get_prefab("arch")

context:place_prefab_instance(arch,   0, 0,   0)
context:place_prefab_instance(arch, 128, 0,  90)
context:place_prefab_instance(arch, 256, 0, 180)
context:place_prefab_instance(arch, 384, 0, 270)
```

## Deterministically scatter Prefab instances

Create a `DefinePrefabs` step named `nature prefabs` with a Prefab named `rock`. Change the `RunScript` seed to reroll the result.

```lua
local definitions = context:find_define_prefabs("nature prefabs")
local rock = definitions:get_prefab("rock")
local rotations = { 0, 90, 180, 270 }
local x, y, width, height = context:get_extents()

for i = 1, 24 do
    local px = x + math.random() * width
    local py = y + math.random() * height
    local angle = rotations[math.random(1, #rotations)]
    context:place_prefab_instance(rock, px, py, angle)
end
```

## Stable iteration over keyed data

Lua does not guarantee useful ordering for `pairs`. Sort keys before traversal whenever order changes placement or fold output.

```lua
local positions = {
    west = { -64, 0 },
    centre = { 0, 0 },
    east = { 64, 0 },
}

local names = {}
for name in pairs(positions) do
    names[#names + 1] = name
end
table.sort(names)

for _, name in ipairs(names) do
    local position = positions[name]
    local marker = context:create_primitive("Rectangle")
    marker:set_size(8, 8)
    marker:set_position(position[1], position[2])
    context:place_primitive(marker)
end
```

## Diagnosing a script

`print` writes a tab-separated line to the host's script log.

```lua
local x, y, width, height = context:get_extents()
local priors = context:get_build_primitives()

print("extents", x, y, width, height)
print("prior primitive count", #priors)

for index, primitive in ipairs(priors) do
    local px, py = primitive:get_position()
    print(index, primitive:get_type(), px, py)
end
```

A syntax error, runtime error, missing named step or Prefab, invalid operation, or instruction-budget overrun fails the `RunScript` step. Its partial output is discarded and later LayerBuildSteps do not run until the error is fixed.
