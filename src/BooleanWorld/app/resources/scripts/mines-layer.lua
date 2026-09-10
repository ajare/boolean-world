local utilities = include("World/UtilityFunctions")

local GRID_SIZE = 256
local FLUSH_CONNECTOR_KEY = "flush-connector"
local STOPE_CONNECTOR_KEY = "stope-connector"
local STOPE_MAX_DISTANCE = 128
local CONNECTOR_KEYS = {FLUSH_CONNECTOR_KEY, STOPE_CONNECTOR_KEY}
local DIRECTIONS = {"north", "east", "south", "west"}
local DIRECTION_SET = {north = true, east = true, south = true, west = true}
local OPPOSITE = {
    north = "south",
    east = "west",
    south = "north",
    west = "east"
}
local ROTATED_DIRECTION = {
    [0] = {north = "north", east = "east", south = "south", west = "west"},
    [90] = {north = "east", east = "south", south = "west", west = "north"},
    [180] = {north = "south", east = "west", south = "north", west = "east"},
    [270] = {north = "west", east = "north", south = "east", west = "south"}
}
local ANGLES = {0, 90, 180, 270}
local EPSILON = 0.001

local function cell_key(x, y)
    return x .. "," .. y
end

local function rotate_point(x, y, angle)
    if angle == 0 then
        return x, y
    elseif angle == 90 then
        return y, -x
    elseif angle == 180 then
        return -x, -y
    else
        return -y, x
    end
end

local function make_directional_connectors()
    return {north = {}, east = {}, south = {}, west = {}}
end

local function make_option(prefab, angle, id)
    local connectors = {
        [FLUSH_CONNECTOR_KEY] = make_directional_connectors(),
        [STOPE_CONNECTOR_KEY] = make_directional_connectors()
    }

    for _, edge in ipairs(prefab:get_metadata_edges()) do
        local metadata = edge:get_metadata()
        for _, connector_key in ipairs(CONNECTOR_KEYS) do
            local direction = metadata[connector_key]
            if DIRECTION_SET[direction] then
                local x1, y1, x2, y2 = edge:get_endpoints()
                x1, y1 = rotate_point(x1, y1, angle)
                x2, y2 = rotate_point(x2, y2, angle)
                direction = ROTATED_DIRECTION[angle][direction]
                local directional_connectors = connectors[connector_key]
                directional_connectors[direction][
                    #directional_connectors[direction] + 1] = {
                    x1 = x1,
                    y1 = y1,
                    x2 = x2,
                    y2 = y2
                }
            end
        end
    end

    return {
        id = id,
        prefab = prefab,
        angle = angle,
        connectors = connectors
    }
end

local function close(first, second)
    return math.abs(first - second) <= EPSILON
end

local function world_segment(segment, cell_x, cell_y)
    local offset_x = (cell_x + 0.5) * GRID_SIZE
    local offset_y = (cell_y + 0.5) * GRID_SIZE
    return segment.x1 + offset_x, segment.y1 + offset_y,
           segment.x2 + offset_x, segment.y2 + offset_y
end

local function segments_match(first, first_x, first_y, second, second_x,
                              second_y)
    local ax1, ay1, ax2, ay2 = world_segment(first, first_x, first_y)
    local bx1, by1, bx2, by2 = world_segment(second, second_x, second_y)

    return (close(ax1, bx1) and close(ay1, by1) and close(ax2, bx2) and
               close(ay2, by2)) or
               (close(ax1, bx2) and close(ay1, by2) and close(ax2, bx1) and
                   close(ay2, by1))
end

local function connector_sets_match(first, first_x, first_y, second,
                                    second_x, second_y)
    if #first == 0 or #first ~= #second then
        return false
    end

    local matched = {}
    for _, first_segment in ipairs(first) do
        local found = false
        for index, second_segment in ipairs(second) do
            if not matched[index] and
                segments_match(first_segment, first_x, first_y,
                               second_segment, second_x, second_y) then
                matched[index] = true
                found = true
                break
            end
        end
        if not found then
            return false
        end
    end

    return true
end

local function stope_matching_cells(candidate, existing, existing_x,
                                    existing_y, direction)
    local ex1, ey1, ex2, ey2 = world_segment(existing, existing_x,
                                              existing_y)
    local parallel_first, parallel_second, candidate_parallel_first,
          candidate_parallel_second, existing_perpendicular,
          candidate_perpendicular

    if direction == "north" or direction == "south" then
        parallel_first, parallel_second = ex1, ex2
        candidate_parallel_first, candidate_parallel_second = candidate.x1,
                                                                    candidate.x2
        existing_perpendicular = (ey1 + ey2) / 2
        candidate_perpendicular = (candidate.y1 + candidate.y2) / 2
    else
        parallel_first, parallel_second = ey1, ey2
        candidate_parallel_first, candidate_parallel_second = candidate.y1,
                                                                    candidate.y2
        existing_perpendicular = (ex1 + ex2) / 2
        candidate_perpendicular = (candidate.x1 + candidate.x2) / 2
    end

    -- North/south connector endpoints must share x coordinates; east/west
    -- endpoints must share y coordinates. Reversed endpoint order is allowed.
    local function aligned_axis(first, second)
        local offset = parallel_first - first
        if not close(parallel_second - second, offset) then
            return nil
        end

        local cell = math.floor(offset / GRID_SIZE)
        if not close(offset, (cell + 0.5) * GRID_SIZE) then
            return nil
        end
        return cell
    end

    local parallel_cell = aligned_axis(candidate_parallel_first,
                                       candidate_parallel_second)
    if parallel_cell == nil then
        parallel_cell = aligned_axis(candidate_parallel_second,
                                     candidate_parallel_first)
    end
    if parallel_cell == nil then
        return {}
    end

    -- Unlike flush connectors, stopes may bridge a gap along the axis
    -- perpendicular to their edges.
    local minimum = (existing_perpendicular - STOPE_MAX_DISTANCE -
                        candidate_perpendicular) / GRID_SIZE - 0.5
    local maximum = (existing_perpendicular + STOPE_MAX_DISTANCE -
                        candidate_perpendicular) / GRID_SIZE - 0.5
    local cell_epsilon = EPSILON / GRID_SIZE
    local first_cell = math.ceil(minimum - cell_epsilon)
    local last_cell = math.floor(maximum + cell_epsilon)
    local cells = {}
    for perpendicular_cell = first_cell, last_cell do
        if direction == "north" or direction == "south" then
            cells[#cells + 1] = {x = parallel_cell, y = perpendicular_cell}
        else
            cells[#cells + 1] = {x = perpendicular_cell, y = parallel_cell}
        end
    end
    return cells
end

-- Returns the grid cell which would make the two segments overlap exactly,
-- or nil when their required translation is not on the 256-unit grid.
local function matching_cell(candidate, existing, existing_x, existing_y)
    local ex1, ey1, ex2, ey2 = world_segment(existing, existing_x,
                                              existing_y)

    local function try_endpoints(cx1, cy1, cx2, cy2)
        local offset_x = ex1 - cx1
        local offset_y = ey1 - cy1
        if not close(ex2 - cx2, offset_x) or
            not close(ey2 - cy2, offset_y) then
            return nil
        end

        local cell_x = math.floor(offset_x / GRID_SIZE)
        local cell_y = math.floor(offset_y / GRID_SIZE)
        if not close(offset_x, (cell_x + 0.5) * GRID_SIZE) or
            not close(offset_y, (cell_y + 0.5) * GRID_SIZE) then
            return nil
        end
        return cell_x, cell_y
    end

    local cell_x, cell_y = try_endpoints(candidate.x1, candidate.y1,
                                          candidate.x2, candidate.y2)
    if cell_x ~= nil then
        return cell_x, cell_y
    end
    return try_endpoints(candidate.x2, candidate.y2,
                         candidate.x1, candidate.y1)
end

-- Query just inside the Tile so geometry flush with another Tile does not make
-- this Tile appear occupied merely because the two bounds touch.
local function cell_is_empty(x, y)
    return #context:find_build_primitives_overlapping(
        x * GRID_SIZE + EPSILON,
        y * GRID_SIZE + EPSILON,
        GRID_SIZE - EPSILON * 2,
        GRID_SIZE - EPSILON * 2
    ) == 0
end

local function create_tunnels_section_primitive(step_name, index,
                                                corridor_width)
    local tile_map = context:find_tile_map(step_name, index)
    local width = tile_map:get_width()
    local height = tile_map:get_height()
    local cell_size = tile_map:get_cell_size()
    local map_size = tile_map:get_map_size()
    assert(type(corridor_width) == "number" and corridor_width > 0 and
               corridor_width <= cell_size,
           "corridor width must be greater than zero and no larger than the TileMap cell size")
    local inset = (cell_size - corridor_width) / 2
    local edges = {}
    local outgoing = {}

    local function add_edge(x1, y1, x2, y2, direction)
        local edge = {
            x1 = x1,
            y1 = y1,
            x2 = x2,
            y2 = y2,
            direction = direction,
            used = false
        }
        edges[#edges + 1] = edge
        local key = cell_key(x1, y1)
        if outgoing[key] == nil then
            outgoing[key] = {}
        end
        outgoing[key][#outgoing[key] + 1] = edge
    end

    -- Give every exposed cell side a counter-clockwise directed edge. Shared
    -- sides are omitted, leaving only the outlines of the set cells.
    for y = 0, height - 1 do
        for x = 0, width - 1 do
            if tile_map:get_cell(x, y) == 1 then
                if y == 0 or tile_map:get_cell(x, y - 1) == 0 then
                    add_edge(x, y, x + 1, y, 0)
                end
                if x == width - 1 or tile_map:get_cell(x + 1, y) == 0 then
                    add_edge(x + 1, y, x + 1, y + 1, 1)
                end
                if y == height - 1 or tile_map:get_cell(x, y + 1) == 0 then
                    add_edge(x + 1, y + 1, x, y + 1, 2)
                end
                if x == 0 or tile_map:get_cell(x - 1, y) == 0 then
                    add_edge(x, y + 1, x, y, 3)
                end
            end
        end
    end

    local function next_edge(edge)
        local candidates = outgoing[cell_key(edge.x2, edge.y2)] or {}
        local best = nil
        local best_rank = math.huge
        for _, candidate in ipairs(candidates) do
            if not candidate.used then
                local turn = (candidate.direction - edge.direction) % 4
                -- At a corner shared only diagonally, keep the occupied cells
                -- on the left instead of joining two otherwise separate Rings.
                local rank = ({[0] = 1, [1] = 0, [2] = 3, [3] = 2})[turn]
                if rank < best_rank then
                    best = candidate
                    best_rank = rank
                end
            end
        end
        return best
    end

    local rings = {}
    for _, first_edge in ipairs(edges) do
        if not first_edge.used then
            local ring_edges = {}
            local edge = first_edge
            while true do
                edge.used = true
                ring_edges[#ring_edges + 1] = edge
                if edge.x2 == first_edge.x1 and edge.y2 == first_edge.y1 then
                    break
                end
                edge = assert(next_edge(edge), "TileMap cells have an open outline")
            end

            -- Intersect each pair of inset edge lines at a corner. Edges on
            -- the Map boundary remain flush so adjacent TileMaps can meet.
            -- Collinear unit edges contribute no additional point.
            local corners = {}
            for edge_index, current in ipairs(ring_edges) do
                local previous = ring_edges[
                                     (edge_index - 2) % #ring_edges + 1]
                if previous.direction ~= current.direction then
                    local x = current.x1 * cell_size
                    local y = current.y1 * cell_size
                    for _, adjacent in ipairs({previous, current}) do
                        if adjacent.direction == 0 and adjacent.y1 ~= 0 then
                            y = adjacent.y1 * cell_size + inset
                        elseif adjacent.direction == 1 and
                            adjacent.x1 ~= width then
                            x = adjacent.x1 * cell_size - inset
                        elseif adjacent.direction == 2 and
                            adjacent.y1 ~= height then
                            y = adjacent.y1 * cell_size - inset
                        elseif adjacent.direction == 3 and adjacent.x1 ~= 0 then
                            x = adjacent.x1 * cell_size + inset
                        end
                    end
                    corners[#corners + 1] = {
                        x - map_size / 2,
                        y - map_size / 2
                    }
                end
            end

            local area = 0
            for point_index, point in ipairs(corners) do
                local following = corners[point_index % #corners + 1]
                area = area + point[1] * following[2] -
                           following[1] * point[2]
            end
            rings[#rings + 1] = {
                points = corners,
                area = math.abs(area),
                children = {}
            }
        end
    end

    if #rings == 0 then
        return nil, map_size
    end

    local function point_is_inside(point, ring)
        local inside = false
        local previous = ring[#ring]
        for _, current in ipairs(ring) do
            if (current[2] > point[2]) ~= (previous[2] > point[2]) and
                point[1] < (previous[1] - current[1]) *
                    (point[2] - current[2]) /
                    (previous[2] - current[2]) + current[1] then
                inside = not inside
            end
            previous = current
        end
        return inside
    end

    -- Build the alternating Shell/Hole/Island hierarchy from geometric
    -- containment so disconnected regions and enclosed unset cells survive.
    local roots = {}
    for _, ring in ipairs(rings) do
        local parent = nil
        for _, candidate in ipairs(rings) do
            if candidate ~= ring and candidate.area > ring.area and
                point_is_inside(ring.points[1], candidate.points) and
                (parent == nil or candidate.area < parent.area) then
                parent = candidate
            end
        end
        ring.parent = parent
        if parent == nil then
            roots[#roots + 1] = ring
        else
            parent.children[#parent.children + 1] = ring
        end
    end

    local mesh = nil
    local function add_ring(ring, parent_id, depth)
        local polygon_id
        if depth == 0 then
            if mesh == nil then
                mesh = context:create_mesh_primitive(ring.points)
                polygon_id = 0
            else
                polygon_id = assert(mesh:add_shell(ring.points))
            end
        elseif depth % 2 == 1 then
            polygon_id = assert(mesh:add_hole(parent_id, ring.points))
        else
            polygon_id = assert(mesh:add_island(parent_id, ring.points))
        end

        for _, child in ipairs(ring.children) do
            add_ring(child, polygon_id, depth + 1)
        end
    end

    for _, root in ipairs(roots) do
        add_ring(root, nil, 0)
    end

    -- Horizontal surfaces use the catalog's 2D material program; walls use
    -- its 3D program. Both variants share the basalt Sub-material id.
    mesh:set_floor_material("builtin.basalt")
    mesh:set_ceiling_material("builtin.basalt")
    mesh:set_wall_material("builtin.basalt")
    return mesh, map_size
end

local tile_map_mesh, tile_map_size = create_tunnels_section_primitive(
    "TileMaps", 0, layer.vars.corridor_width)
assert(tile_map_mesh ~= nil, "TileMaps[0] has no set cells")
local tile_map_x, tile_map_y =
    utilities.find_closest_empty_grid_cell(0, 0, tile_map_size)
tile_map_mesh:set_position((tile_map_x + 0.5) * tile_map_size,
                           (tile_map_y + 0.5) * tile_map_size)
context:place_primitive(tile_map_mesh)

dprint("Finding prefabs")
local definitions = context:find_define_prefabs("Main")
local size_256_prefabs = utilities.get_prefabs_with_grid_size(definitions,
                                                              GRID_SIZE)
local tunnel_prefabs = {}

for _, prefab in ipairs(size_256_prefabs) do
    if string.sub(prefab:get_name(), 1, #"Tunnels") == "Tunnels" then
        tunnel_prefabs[#tunnel_prefabs + 1] = prefab
    end
end

assert(#tunnel_prefabs > 0, "Main has no size 256 Tunnels Prefabs")

local options = {}
local unrotated_options = {}
for _, prefab in ipairs(tunnel_prefabs) do
    for _, angle in ipairs(ANGLES) do
        local option = make_option(prefab, angle, #options + 1)
        options[#options + 1] = option
        if angle == 0 then
            unrotated_options[#unrotated_options + 1] = option
        end
    end
end

-- Connector geometry is local to a Prefab, so calculate each possible
-- option-to-option cell offset once rather than repeating the segment work for
-- every placed instance.
for _, existing_option in ipairs(options) do
    local transitions = {}
    local transitions_by_key = {}
    for _, existing_direction in ipairs(DIRECTIONS) do
        local candidate_direction = OPPOSITE[existing_direction]

        -- Match each connector type independently: flush connectors can only
        -- join flush connectors, and stope connectors can only join stopes.
        for _, connector_key in ipairs(CONNECTOR_KEYS) do
            local existing_connectors = existing_option.connectors[
                                            connector_key][existing_direction]
            for _, candidate_option in ipairs(options) do
                local candidate_connectors = candidate_option.connectors[
                                                 connector_key][
                                                 candidate_direction]
                for _, candidate_segment in ipairs(candidate_connectors) do
                    for _, existing_segment in ipairs(existing_connectors) do
                        local matching_cells
                        if connector_key == FLUSH_CONNECTOR_KEY then
                            local x, y = matching_cell(candidate_segment,
                                                       existing_segment, 0, 0)
                            matching_cells = {}
                            if x ~= nil and
                                (#candidate_connectors == 1 and
                                     #existing_connectors == 1 or
                                 connector_sets_match(candidate_connectors, x,
                                                      y, existing_connectors,
                                                      0, 0)) then
                                matching_cells[1] = {x = x, y = y}
                            end
                        else
                            matching_cells = stope_matching_cells(
                                candidate_segment, existing_segment, 0, 0,
                                existing_direction)
                        end

                        for _, cell in ipairs(matching_cells) do
                            local key = cell_key(cell.x, cell.y) .. ":" ..
                                            candidate_option.id
                            if transitions_by_key[key] == nil then
                                local transition = {
                                    x = cell.x,
                                    y = cell.y,
                                    option = candidate_option
                                }
                                transitions[#transitions + 1] = transition
                                transitions_by_key[key] = transition
                            end
                        end
                    end
                end
            end
        end
    end
    existing_option.transitions = transitions
end

-- Seed the layout so the first loop iteration has an occupied neighbour.
dprint("Placing initial prefab")
local placements_by_cell = {}
local seed = unrotated_options[math.random(#unrotated_options)]
local seed_x, seed_y = utilities.find_closest_empty_grid_cell(0, 0, GRID_SIZE)
context:place_prefab_instance(seed.prefab, seed_x, seed_y, seed.angle)
local seed_placement = {x = seed_x, y = seed_y, option = seed}
placements_by_cell[cell_key(seed_x, seed_y)] = seed_placement

local candidates = {}
local candidates_by_key = {}
local empty_cells = {}

local function cell_is_available(x, y)
    local key = cell_key(x, y)
    if placements_by_cell[key] ~= nil then
        return false
    end
    if empty_cells[key] == nil then
        empty_cells[key] = cell_is_empty(x, y)
    end
    return empty_cells[key]
end

-- Add only the frontier contributed by a new placement. A candidate remembers
-- every placement it joins, so later iterations do not have to rediscover and
-- recount all previous placement/candidate pairs.
local function add_candidates_for_placement(placement)
    for _, transition in ipairs(placement.option.transitions) do
        local x = placement.x + transition.x
        local y = placement.y + transition.y
        if cell_is_available(x, y) then
            local key = cell_key(x, y) .. ":" .. transition.option.id
            local candidate = candidates_by_key[key]
            if candidate == nil then
                candidate = {
                    x = x,
                    y = y,
                    option = transition.option,
                    neighbours = 0,
                    matching_placements = {}
                }
                candidates[#candidates + 1] = candidate
                candidates_by_key[key] = candidate
            end
            if not candidate.matching_placements[placement] then
                candidate.matching_placements[placement] = true
                candidate.neighbours = candidate.neighbours + 1
            end
        end
    end
end

add_candidates_for_placement(seed_placement)

dprint(string.format("Placing %d prefabs", step.vars.iterations))
for _ = 1, step.vars.iterations do
    local best_neighbours = 0
    local best_distance = math.huge
    local best_candidates = {}
    local available_candidates = {}

    for _, candidate in ipairs(candidates) do
        if cell_is_available(candidate.x, candidate.y) then
            available_candidates[#available_candidates + 1] = candidate
            local neighbours = candidate.neighbours
            local distance = candidate.x * candidate.x +
                                 candidate.y * candidate.y

            if neighbours > best_neighbours or
                (neighbours == best_neighbours and distance < best_distance) then
                best_neighbours = neighbours
                best_distance = distance
                best_candidates = {candidate}
            elseif neighbours == best_neighbours and
                distance == best_distance then
                best_candidates[#best_candidates + 1] = candidate
            end
        end
    end
    candidates = available_candidates

    if #best_candidates == 0 then
        break
    end

    -- Choose the Prefab uniformly first, then choose one of that Prefab's
    -- equally ranked cells and rotations. This avoids biasing the result
    -- toward whichever Prefab contributed the first (or the most) options.
    local prefab_choices = {}
    local choices_by_prefab = {}
    for _, choice in ipairs(best_candidates) do
        local prefab = choice.option.prefab
        local prefab_choice = choices_by_prefab[prefab]
        if prefab_choice == nil then
            prefab_choice = {candidates = {}}
            prefab_choices[#prefab_choices + 1] = prefab_choice
            choices_by_prefab[prefab] = prefab_choice
        end
        prefab_choice.candidates[#prefab_choice.candidates + 1] = choice
    end

    local prefab_choice = prefab_choices[math.random(#prefab_choices)]
    local candidate = prefab_choice.candidates[
        math.random(#prefab_choice.candidates)]
    context:place_prefab_instance(candidate.option.prefab, candidate.x,
                                  candidate.y, candidate.option.angle)
    local placement = {
        x = candidate.x,
        y = candidate.y,
        option = candidate.option
    }
    placements_by_cell[cell_key(candidate.x, candidate.y)] = placement
    add_candidates_for_placement(placement)
end
