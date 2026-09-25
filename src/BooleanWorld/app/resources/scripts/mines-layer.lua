local utilities = include("World/UtilityFunctions")

local GRID_SIZE = 256
local SCAN_CELL_SIZE = 32
local DIAGONAL_COVER_SIZE = 48
local DIAGONAL_COVER_FLOOR_DROP = 50
local FLUSH_CONNECTOR_KEY = "flush-connector"
local STOPE_CONNECTOR_KEY = "stope-connector"
local STOPE_MAX_DISTANCE = 128
local FLOOR_DROP_PER_CELL = 16
-- A corner basin falls three units from its lip. Half a unit of authored
-- Liquid fills its low end without reaching the adjoining tunnel, so Water
-- remains in the basin instead of settling through lower mine sections.
local CORNER_POOL_LIQUID_LEVEL = 0.5
local RAIL_STOP_CHANCE = 0.25
local RAIL_STOP_LENGTH = 8
local RAIL_STOP_WIDTH = 12
local RAIL_STOP_HEIGHT = 8
local CORRIDOR_WIDTH_MIN_VARIATION = 1
local CORRIDOR_WIDTH_MAX_VARIATION = 2
local SLOPE_CUT_CENTER_JITTER = 0.5
local SLOPE_CUT_MIN_SKEW = 0.75
local SLOPE_CUT_MAX_SKEW = 1.5
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
local WINDOW_OFFSETS = {{0, 0}, {-1, 0}, {0, -1}, {-1, -1}}
local EPSILON = 0.001
local split_candidates_by_map = {}
local corner_candidates_by_map = {}
local longest_rail_runs_by_map = {}
local slope_connector_candidates_by_map = {}
local unmatched_slope_connectors = {}
local occupied_scan_cells = {}
local scan_cells = {}

local function cell_key(x, y)
    return x .. "," .. y
end

local function mark_scan_cell(x, y)
    local key = cell_key(x, y)
    if not occupied_scan_cells[key] then
        occupied_scan_cells[key] = true
        scan_cells[#scan_cells + 1] = {x = x, y = y}
    end
end

local function mark_scan_cell_containing(x, y)
    mark_scan_cell(math.floor(x / SCAN_CELL_SIZE),
                   math.floor(y / SCAN_CELL_SIZE))
end

local function base_floor_height_for_cell(x, y)
    return -math.sqrt(x * x + y * y) * FLOOR_DROP_PER_CELL
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

local function create_wooden_supports(tile_map, corridor_width, map_size,
                                       corridor, sliced_cells, floor_regions)
    local cell_size = tile_map:get_cell_size()
    local width = tile_map:get_width()
    local height = tile_map:get_height()
    local neighbour_offsets = {
        {x = 0, y = -1},
        {x = 1, y = 0},
        {x = 0, y = 1},
        {x = -1, y = 0}
    }
    local ceiling_angle, ceiling_lower, ceiling_upper =
        corridor:get_ceiling_elevation()
    local support_frame_pct = layer.vars.support_frame_pct
    assert(type(support_frame_pct) == "number" and support_frame_pct >= 0 and
               support_frame_pct <= 100,
           "layer.vars.support_frame_pct must be a number from 0 to 100")
    local support_frame_chance = support_frame_pct / 100
    local supports = {}

    local function cell_is_set(x, y)
        return x >= 0 and x < width and y >= 0 and y < height and
                   tile_map:get_cell(x, y) == 1
    end

    local function configure_wood(primitive)
        primitive:set_floor_material("builtin.wood2")
        primitive:set_ceiling_material("builtin.wood2")
        primitive:set_wall_material("builtin.wood2")
    end

    local function add_primitive(primitive)
        configure_wood(primitive)
        supports[#supports + 1] = primitive
    end

    -- Copy an Elevation plane rather than its authored span. Span endpoints
    -- are fitted to each Primitive's bounds, so copying them directly onto a
    -- differently sized support changes the slope.
    local function match_floor_plane(target, source)
        local source_angle = source:get_floor_elevation()
        local source_x, source_y = source:get_position()
        local source_orientation = source:get_orientation()
        local target_x, target_y = target:get_position()
        local target_width, target_height = target:get_size()
        local target_orientation = target:get_orientation()

        local function direction(angle)
            local radians = math.rad(angle)
            return -math.sin(radians), math.cos(radians)
        end

        local function evaluate_source(world_x, world_y)
            local radians = math.rad(source_orientation)
            local cosine, sine = math.cos(radians), math.sin(radians)
            local offset_x, offset_y = world_x - source_x, world_y - source_y
            -- Undo the source Primitive's clockwise orientation.
            local local_x = offset_x * cosine - offset_y * sine
            local local_y = offset_x * sine + offset_y * cosine
            return source:get_floor_elevation_at(local_x, local_y)
        end

        local target_angle = source_angle - source_orientation +
                                 target_orientation
        local target_dx, target_dy = direction(target_angle)
        local target_run = math.abs(target_dx) * target_width +
                               math.abs(target_dy) * target_height
        local radians = math.rad(target_orientation)
        local cosine, sine = math.cos(radians), math.sin(radians)
        local function target_world_position(distance)
            local local_x = target_dx * distance
            local local_y = target_dy * distance
            return target_x + local_x * cosine + local_y * sine,
                   target_y - local_x * sine + local_y * cosine
        end
        local lower_x, lower_y = target_world_position(-target_run / 2)
        local upper_x, upper_y = target_world_position(target_run / 2)
        target:set_floor_elevation(
            target_angle, evaluate_source(lower_x, lower_y),
            evaluate_source(upper_x, upper_y))
    end

    for y = 0, height - 1 do
        for x = 0, width - 1 do
            if cell_is_set(x, y) then
                local neighbours = {}
                for _, offset in ipairs(neighbour_offsets) do
                    if cell_is_set(x + offset.x, y + offset.y) then
                        neighbours[#neighbours + 1] = offset
                    end
                end

                local is_straight_corridor = #neighbours == 2 and
                    (neighbours[1].x == neighbours[2].x or
                        neighbours[1].y == neighbours[2].y)
                if is_straight_corridor and
                    not sliced_cells[cell_key(x, y)] and
                    math.random() < support_frame_chance then
                    -- The line between the two neighbours is the corridor's
                    -- local direction. Its normal points toward the two walls
                    -- carrying the frame.
                    local direction_x = neighbours[2].x - neighbours[1].x
                    local direction_y = neighbours[2].y - neighbours[1].y
                    local direction_length = math.sqrt(
                                                 direction_x * direction_x +
                                                     direction_y * direction_y)
                    local normal_x = -direction_y / direction_length
                    local normal_y = direction_x / direction_length
                    local center_x = (x + 0.5) * cell_size - map_size / 2
                    local center_y = (y + 0.5) * cell_size - map_size / 2
                    local frame_floor = corridor
                    for _, region in ipairs(floor_regions) do
                        if region.primitive:contains_point(center_x,
                                                           center_y) then
                            frame_floor = region.primitive
                            break
                        end
                    end

                    for _, side in ipairs({-1, 1}) do
                        local post = context:create_primitive("Rectangle")
                        post:set_size(4, 4)
                        post:set_position(
                            center_x + side * normal_x * corridor_width / 2,
                            center_y + side * normal_y * corridor_width / 2)
                        post:set_operation("difference")
                        -- Keep the posts after the bridge in the Boolean fold
                        -- so its Union cannot fill their wall cut-outs.
                        post:set_priority(3)
                        match_floor_plane(post, frame_floor)
                        post:set_ceiling_elevation(ceiling_angle,
                                                   ceiling_lower,
                                                   ceiling_upper)
                        add_primitive(post)
                    end

                    local bridge = context:create_primitive("Rectangle")
                    -- Each wall-centred post extends 2 units into the
                    -- corridor, so span only the gap between their inner
                    -- edges rather than overlapping either post.
                    bridge:set_size(corridor_width - 4, 4)
                    bridge:set_position(center_x, center_y)
                    -- Primitive orientations are clockwise, while atan is
                    -- anticlockwise in the World plane.
                    bridge:set_orientation(math.deg(math.atan(-normal_y,
                                                              normal_x)))
                    bridge:set_operation("union")
                    -- Override the corridor's ceiling and materials while
                    -- still folding before the higher-priority posts.
                    bridge:set_priority(2)
                    match_floor_plane(bridge, frame_floor)
                    bridge:set_ceiling_elevation(
                        0, layer.vars.corridor_base_height - 2,
                        layer.vars.corridor_base_height - 2)
                    add_primitive(bridge)
                    bridge:set_floor_material(layer.vars.mine_material)
                    bridge:set_ceiling_material("builtin.wood2")
                    bridge:set_wall_material("builtin.wood2")
                end
            end
        end
    end

    return supports
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
    local corridor_width_var_pct = layer.vars.corridor_width_var_pct
    assert(type(corridor_width_var_pct) == "number" and
               corridor_width_var_pct >= 0 and corridor_width_var_pct <= 100,
           "layer.vars.corridor_width_var_pct must be a number from 0 to 100")
    local corridor_width_var_chance = corridor_width_var_pct / 100
    local edges = {}
    local outgoing = {}
    local set_cells = {}

    local function cell_is_set(x, y)
        return x >= 0 and x < width and y >= 0 and y < height and
                   tile_map:get_cell(x, y) == 1
    end

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
                set_cells[#set_cells + 1] = {x = x, y = y}
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

            -- Decide independently for each actual corridor wall whether to
            -- insert a point. Delay insertion until all floor slicing is
            -- complete so those slices can still address
            -- the original straight wall coordinates. Map-boundary segments
            -- are section seams and must remain flush with adjacent sections.
            local wall_variations = {}
            for point_index, point in ipairs(corners) do
                local following = corners[point_index % #corners + 1]
                local on_map_boundary =
                    point[1] == following[1] and
                        (point[1] == -map_size / 2 or
                            point[1] == map_size / 2) or
                        point[2] == following[2] and
                            (point[2] == -map_size / 2 or
                                point[2] == map_size / 2)
                if not on_map_boundary and corridor_width_var_chance > 0 and
                    math.random() < corridor_width_var_chance then
                    local delta_x = following[1] - point[1]
                    local delta_y = following[2] - point[2]
                    local length = math.sqrt(delta_x * delta_x +
                                                 delta_y * delta_y)
                    local displacement = CORRIDOR_WIDTH_MIN_VARIATION +
                                             math.random() *
                                                 (CORRIDOR_WIDTH_MAX_VARIATION -
                                                     CORRIDOR_WIDTH_MIN_VARIATION)
                    if math.random(2) == 1 then
                        displacement = -displacement
                    end
                    wall_variations[#wall_variations + 1] = {
                        x1 = point[1],
                        y1 = point[2],
                        x2 = following[1],
                        y2 = following[2],
                        delta_x = -delta_y / length * displacement,
                        delta_y = delta_x / length * displacement
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
                children = {},
                wall_variations = wall_variations
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

    -- Give each straight corridor cell a chance of receiving a slightly
    -- skewed cross-cut. The cut endpoints lie on the already-inset walls, so
    -- slicing preserves the tunnel footprint while adding another polygon.
    local split_candidates = split_candidates_by_map[index]
    local corner_candidates = corner_candidates_by_map[index]
    local neighbour_offsets = {
        {x = 0, y = -1},
        {x = 1, y = 0},
        {x = 0, y = 1},
        {x = -1, y = 0}
    }
    if split_candidates == nil then
        split_candidates = {}
        corner_candidates = {}
        for y = 0, height - 1 do
            for x = 0, width - 1 do
                if cell_is_set(x, y) then
                    local neighbours = {}
                    for _, offset in ipairs(neighbour_offsets) do
                        if cell_is_set(x + offset.x, y + offset.y) then
                            neighbours[#neighbours + 1] = offset
                        end
                    end
                    if #neighbours == 2 then
                        local is_straight =
                            neighbours[1].x == neighbours[2].x or
                                neighbours[1].y == neighbours[2].y
                        if is_straight then
                            split_candidates[#split_candidates + 1] = {
                                x = x,
                                y = y,
                                direction_x =
                                    (neighbours[2].x - neighbours[1].x) / 2,
                                direction_y =
                                    (neighbours[2].y - neighbours[1].y) / 2
                            }
                        else
                            local function can_extend(direction, other_arm)
                                -- Branches are allowed on the inner side. The
                                -- outer edge continuing from the Corner must
                                -- remain exposed so the pool boundary can
                                -- follow it without crossing a wall.
                                local second_x = x + direction.x * 2
                                local second_y = y + direction.y * 2
                                return cell_is_set(second_x, second_y) and
                                           cell_is_set(
                                               second_x + direction.x,
                                               second_y + direction.y) and
                                           not cell_is_set(
                                               second_x - other_arm.x,
                                               second_y - other_arm.y)
                            end
                            corner_candidates[#corner_candidates + 1] = {
                                x = x,
                                y = y,
                                first = neighbours[1],
                                second = neighbours[2],
                                first_can_extend = can_extend(
                                    neighbours[1], neighbours[2]),
                                second_can_extend = can_extend(
                                    neighbours[2], neighbours[1])
                            }
                        end
                    end
                end
            end
        end
        split_candidates_by_map[index] = split_candidates
        corner_candidates_by_map[index] = corner_candidates
    end

    local slope_connector_candidates = slope_connector_candidates_by_map[index]
    if slope_connector_candidates == nil then
        slope_connector_candidates = {}
        local function add_slope_connector(x, y, outward_x, outward_y)
            local inward_x = -outward_x
            local inward_y = -outward_y
            local side_x = -outward_y
            local side_y = outward_x
            if cell_is_set(x, y) and
                cell_is_set(x + inward_x, y + inward_y) and
                not cell_is_set(x + side_x, y + side_y) and
                not cell_is_set(x - side_x, y - side_y) then
                slope_connector_candidates[#slope_connector_candidates + 1] = {
                    x = x,
                    y = y,
                    outward_x = outward_x,
                    outward_y = outward_y,
                    side_x = side_x,
                    side_y = side_y
                }
            end
        end
        for x = 0, width - 1 do
            add_slope_connector(x, 0, 0, -1)
            add_slope_connector(x, height - 1, 0, 1)
        end
        for y = 0, height - 1 do
            add_slope_connector(0, y, -1, 0)
            add_slope_connector(width - 1, y, 1, 0)
        end
        slope_connector_candidates_by_map[index] = slope_connector_candidates
    end
    local slope_connector_cells = {}
    for _, connector in ipairs(slope_connector_candidates) do
        slope_connector_cells[cell_key(connector.x, connector.y)] = true
    end

    -- Choose at most one rail run before any random floor cuts are made. A
    -- dead end has exactly one set cardinal neighbour; its run continues in
    -- that neighbour's direction until the first unset cell. Prefer the
    -- longest complete run, randomly breaking ties, and retain at most six
    -- cells from its dead-end origin.
    local rails_pct = layer.vars.rails_pct
    assert(type(rails_pct) == "number" and rails_pct >= 0 and rails_pct <= 100,
           "layer.vars.rails_pct must be a number from 0 to 100")
    local selected_rail_run = nil
    local selected_rail_cells = {}
    local longest_runs = longest_rail_runs_by_map[index]
    if longest_runs == nil then
        longest_runs = {}
        local longest_length = 0
        for y = 0, height - 1 do
            for x = 0, width - 1 do
                if cell_is_set(x, y) then
                    local neighbours = {}
                    for _, offset in ipairs(neighbour_offsets) do
                        if cell_is_set(x + offset.x, y + offset.y) then
                            neighbours[#neighbours + 1] = offset
                        end
                    end
                    if #neighbours == 1 then
                        local direction = neighbours[1]
                        local cells = {}
                        local run_x, run_y = x, y
                        while cell_is_set(run_x, run_y) do
                            cells[#cells + 1] = {x = run_x, y = run_y}
                            run_x = run_x + direction.x
                            run_y = run_y + direction.y
                        end
                        if #cells > longest_length then
                            longest_length = #cells
                            longest_runs = {{
                                cells = cells,
                                direction_x = direction.x,
                                direction_y = direction.y
                            }}
                        elseif #cells == longest_length then
                            longest_runs[#longest_runs + 1] = {
                                cells = cells,
                                direction_x = direction.x,
                                direction_y = direction.y
                            }
                        end
                    end
                end
            end
        end
        longest_rail_runs_by_map[index] = longest_runs
    end
    if #longest_runs > 0 and math.random() < rails_pct / 100 then
        local longest_run = longest_runs[math.random(#longest_runs)]
        selected_rail_run = {
            cells = {},
            direction_x = longest_run.direction_x,
            direction_y = longest_run.direction_y
        }
        for cell_index = 1, math.min(#longest_run.cells, 6) do
            local cell = longest_run.cells[cell_index]
            selected_rail_run.cells[cell_index] = cell
            selected_rail_cells[cell_key(cell.x, cell.y)] = true
        end
        local first_cell = selected_rail_run.cells[1]
        if first_cell ~= nil and
            slope_connector_cells[cell_key(first_cell.x, first_cell.y)] then
            table.remove(selected_rail_run.cells, 1)
            selected_rail_cells = {}
            for _, cell in ipairs(selected_rail_run.cells) do
                selected_rail_cells[cell_key(cell.x, cell.y)] = true
            end
            if #selected_rail_run.cells < 2 then
                selected_rail_run = nil
            end
        end
    end

    local cell_floor_split_pct = layer.vars.cell_floor_split_pct
    assert(type(cell_floor_split_pct) == "number" and
               cell_floor_split_pct >= 0 and cell_floor_split_pct <= 100,
           "layer.vars.cell_floor_split_pct must be a number from 0 to 100")
    local cell_floor_split_chance = cell_floor_split_pct / 100
    local floor_step_variance = layer.vars.floor_step_variance
    assert(type(floor_step_variance) == "number" and
               floor_step_variance >= 0,
           "layer.vars.floor_step_variance must be a non-negative number")
    local corner_pool_pct = layer.vars.corner_pool_pct
    assert(type(corner_pool_pct) == "number" and corner_pool_pct >= 0 and
               corner_pool_pct <= 100,
           "layer.vars.corner_pool_pct must be a number from 0 to 100")
    local corner_pool_chance = corner_pool_pct / 100

    local mesh = nil
    local wall_variations = {}
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
        for _, variation in ipairs(ring.wall_variations) do
            wall_variations[#wall_variations + 1] = variation
        end

        for _, child in ipairs(ring.children) do
            add_ring(child, polygon_id, depth + 1)
        end
    end

    for _, root in ipairs(roots) do
        add_ring(root, nil, 0)
    end

    local sliced_cells = {}
    local slope_connector_points = {}
    for _, connector in ipairs(slope_connector_candidates) do
        local center_x = (connector.x + 0.5) * cell_size - map_size / 2
        local center_y = (connector.y + 0.5) * cell_size - map_size / 2
        local inner_offset = -cell_size / 4
        local transition_length = cell_size * 0.75
        local extra_cut_count = math.random(0, 4)
        local cuts_succeeded = true
        for cut_index = 0, extra_cut_count do
            local fraction = cut_index / (extra_cut_count + 1)
            local offset = inner_offset + transition_length * fraction
            -- Skew each transverse cut by moving its wall vertices in
            -- opposite directions along the tunnel. A small independent
            -- centre jitter keeps successive cuts from looking mechanical.
            local center_jitter =
                (math.random() * 2 - 1) * SLOPE_CUT_CENTER_JITTER
            local skew = SLOPE_CUT_MIN_SKEW + math.random() *
                             (SLOPE_CUT_MAX_SKEW - SLOPE_CUT_MIN_SKEW)
            if math.random(2) == 1 then
                skew = -skew
            end
            local first_offset = offset + center_jitter + skew / 2
            local second_offset = offset + center_jitter - skew / 2
            local first_x = center_x +
                                connector.outward_x * first_offset +
                                connector.side_x * corridor_width / 2
            local first_y = center_y +
                                connector.outward_y * first_offset +
                                connector.side_y * corridor_width / 2
            local second_x = center_x +
                                 connector.outward_x * second_offset -
                                 connector.side_x * corridor_width / 2
            local second_y = center_y +
                                 connector.outward_y * second_offset -
                                 connector.side_y * corridor_width / 2
            if mesh:slice_at(first_x, first_y, second_x, second_y) == nil then
                cuts_succeeded = false
                break
            end
        end
        if cuts_succeeded then
            local segments = {}
            for segment_index = 0, extra_cut_count do
                local first_fraction = segment_index /
                                           (extra_cut_count + 1)
                local second_fraction = (segment_index + 1) /
                                            (extra_cut_count + 1)
                local midpoint_fraction = (first_fraction +
                                                second_fraction) / 2
                local offset = inner_offset +
                                   transition_length * midpoint_fraction
                segments[#segments + 1] = {
                    x = center_x + connector.outward_x * offset,
                    y = center_y + connector.outward_y * offset,
                    first_fraction = first_fraction,
                    second_fraction = second_fraction
                }
            end
            sliced_cells[cell_key(connector.x, connector.y)] = true
            slope_connector_points[#slope_connector_points + 1] = {
                inner_x = center_x +
                    connector.outward_x * (inner_offset - 1),
                inner_y = center_y +
                    connector.outward_y * (inner_offset - 1),
                boundary_x = center_x + connector.outward_x * cell_size / 2,
                boundary_y = center_y + connector.outward_y * cell_size / 2,
                outward_x = connector.outward_x,
                outward_y = connector.outward_y,
                segments = segments
            }
        end
    end

    -- Select pools before making ordinary floor cuts. Their complete 3--5
    -- cell footprints must remain unsliced, and enlarged pools must not
    -- overlap one another.
    local selected_pool_candidates = {}
    local selected_pool_cells = {}
    local selected_enlarged_pool_cells = {}

    for _, candidate in ipairs(corner_candidates) do
        local pool_roll = nil
        if not selected_rail_cells[cell_key(candidate.x, candidate.y)] then
            pool_roll = math.random()
        end
        if pool_roll ~= nil and pool_roll < corner_pool_chance then
            candidate.pool_radius = 24 + pool_roll / corner_pool_chance * 12
            candidate.extend_first = candidate.first_can_extend and
                not selected_rail_cells[cell_key(
                    candidate.x + candidate.first.x * 2,
                    candidate.y + candidate.first.y * 2)]
            candidate.extend_second = candidate.second_can_extend and
                not selected_rail_cells[cell_key(
                    candidate.x + candidate.second.x * 2,
                    candidate.y + candidate.second.y * 2)]

            local footprint = {
                {x = candidate.x, y = candidate.y},
                {
                    x = candidate.x + candidate.first.x,
                    y = candidate.y + candidate.first.y
                },
                {
                    x = candidate.x + candidate.second.x,
                    y = candidate.y + candidate.second.y
                }
            }
            if candidate.extend_first then
                footprint[#footprint + 1] = {
                    x = candidate.x + candidate.first.x * 2,
                    y = candidate.y + candidate.first.y * 2
                }
            end
            if candidate.extend_second then
                footprint[#footprint + 1] = {
                    x = candidate.x + candidate.second.x * 2,
                    y = candidate.y + candidate.second.y * 2
                }
            end

            local overlaps_rail = false
            local overlaps_connector = false
            for _, cell in ipairs(footprint) do
                local key = cell_key(cell.x, cell.y)
                if selected_rail_cells[key] then
                    overlaps_rail = true
                end
                if slope_connector_cells[key] then
                    overlaps_connector = true
                end
            end
            local is_enlarged = candidate.extend_first or
                                    candidate.extend_second
            local conflicting_cells = is_enlarged and selected_pool_cells or
                                          selected_enlarged_pool_cells
            local overlaps = false
            for _, cell in ipairs(footprint) do
                if conflicting_cells[cell_key(cell.x, cell.y)] then
                    overlaps = true
                    break
                end
            end
            if not overlaps_rail and not overlaps_connector and
                not overlaps then
                candidate.footprint = footprint
                selected_pool_candidates[#selected_pool_candidates + 1] =
                    candidate
                for _, cell in ipairs(footprint) do
                    local key = cell_key(cell.x, cell.y)
                    selected_pool_cells[key] = true
                    if is_enlarged then
                        selected_enlarged_pool_cells[key] = true
                    end
                end
            end
        end
    end

    for _, candidate in ipairs(split_candidates) do
        local key = cell_key(candidate.x, candidate.y)
        if not selected_rail_cells[key] and not selected_pool_cells[key] and
            math.random() < cell_floor_split_chance then
            local angle_hundredths = math.random(7000, 11000)
            if angle_hundredths == 9000 then
                angle_hundredths = 9001
            end
            local angle = math.rad(angle_hundredths / 100)
            local cosine = math.cos(angle)
            local sine = math.sin(angle)
            local cut_x = candidate.direction_x * cosine -
                              candidate.direction_y * sine
            local cut_y = candidate.direction_x * sine +
                              candidate.direction_y * cosine
            local normal_x = -candidate.direction_y
            local normal_y = candidate.direction_x
            local normal_projection = cut_x * normal_x + cut_y * normal_y
            local distance = corridor_width / 2 / normal_projection
            local center_x = (candidate.x + 0.5) * cell_size - map_size / 2
            local center_y = (candidate.y + 0.5) * cell_size - map_size / 2
            local slice_edge = mesh:slice_at(
                                   center_x - cut_x * distance,
                                   center_y - cut_y * distance,
                                   center_x + cut_x * distance,
                                   center_y + cut_y * distance)
            if slice_edge ~= nil then
                sliced_cells[key] = true
                if math.random() < 0.5 then
                    local vertex = assert(mesh:split_edge(
                                              slice_edge,
                                              0.4 + math.random() * 0.2))
                    local offset = 1 + math.random()
                    if math.random(2) == 1 then
                        offset = -offset
                    end
                    assert(mesh:move_vertex(vertex, -cut_y * offset,
                                            cut_x * offset))
                end
            end
        end
    end

    local function roughen_pool_edge(edge, toward_x, toward_y)
        local segment_count = math.random(3, 4)
        local curve_amplitude = 1.5 + math.random() * 1.5
        local offsets = {}
        for vertex_index = 1, segment_count - 1 do
            local curve = math.sin(math.pi * vertex_index / segment_count) *
                              curve_amplitude
            local roughness = (math.random() - 0.5) * 0.5
            offsets[vertex_index] = curve + roughness
        end
        assert(mesh:roughen_edge(edge, offsets, toward_x, toward_y))
    end

    local pool_points = {}
    for _, candidate in ipairs(selected_pool_candidates) do
        local center_x = (candidate.x + 0.5) * cell_size - map_size / 2
        local center_y = (candidate.y + 0.5) * cell_size - map_size / 2
        local corner_x = center_x -
                             (candidate.first.x + candidate.second.x) *
                                 corridor_width / 2
        local corner_y = center_y -
                             (candidate.first.y + candidate.second.y) *
                                 corridor_width / 2
        local pool_radius = candidate.pool_radius

        local function remember_pool(adjacent_points)
            for _, cell in ipairs(candidate.footprint) do
                sliced_cells[cell_key(cell.x, cell.y)] = true
            end
            local corner_direction_x = corner_x - center_x
            local corner_direction_y = corner_y - center_y
            local pool_angle = math.deg(math.atan(-corner_direction_x,
                                                   corner_direction_y))
            if pool_angle < 0 then
                pool_angle = pool_angle + 360
            end
            pool_points[#pool_points + 1] = {
                x = corner_x +
                    (candidate.first.x + candidate.second.x) *
                        corridor_width / 8,
                y = corner_y +
                    (candidate.first.y + candidate.second.y) *
                        corridor_width / 8,
                adjacent_points = adjacent_points,
                angle = pool_angle
            }
        end

        local function create_compact_pool()
            -- Transverse cuts can join different Rings when a turn wraps a
            -- Hole. This local fallback chord always stays inside the corner
            -- cell, ensuring a selected corner still receives a pool.
            local first_x = corner_x + candidate.first.x * pool_radius
            local first_y = corner_y + candidate.first.y * pool_radius
            local second_x = corner_x + candidate.second.x * pool_radius
            local second_y = corner_y + candidate.second.y * pool_radius
            local pool_edge = mesh:slice_at(first_x, first_y,
                                                 second_x, second_y)
            if pool_edge ~= nil then
                roughen_pool_edge(pool_edge, corner_x, corner_y)
                local adjacent_distance = pool_radius / 2 + 1
                remember_pool({{
                    x = corner_x +
                        (candidate.first.x + candidate.second.x) *
                            adjacent_distance,
                    y = corner_y +
                        (candidate.first.y + candidate.second.y) *
                            adjacent_distance
                }})
                return true
            end
            return false
        end

        if not candidate.extend_first and not candidate.extend_second then
            create_compact_pool()
        else
        local function boundary_distance(cell_x, cell_y, side)
            local neighbour_x = cell_x + side.x
            local neighbour_y = cell_y + side.y
            if neighbour_x < 0 or neighbour_x >= width or
                neighbour_y < 0 or neighbour_y >= height then
                -- Tunnel outlines remain flush at the Map boundary.
                return cell_size / 2
            end
            return corridor_width / 2
        end

        -- Cap each arm separately. A long diagonal between enlarged arms
        -- would leave the L-shaped tunnel and intersect its opposite walls.
        -- The extra cell-size offset puts an ordinary cap in the immediate
        -- neighbour and an enlarged cap in the second cell, while retaining
        -- the old cut's position relative to that cell.
        local first_distance = pool_radius + cell_size *
                                   (candidate.extend_first and 2 or 1)
        local first_cap_cell_x = candidate.x + candidate.first.x *
                                     (candidate.extend_first and 2 or 1)
        local first_cap_cell_y = candidate.y + candidate.first.y *
                                     (candidate.extend_first and 2 or 1)
        local first_center_x = corner_x +
                                   candidate.first.x * first_distance +
                                   candidate.second.x * corridor_width / 2
        local first_center_y = corner_y +
                                   candidate.first.y * first_distance +
                                   candidate.second.y * corridor_width / 2
        local first_outer_distance = boundary_distance(
                                         first_cap_cell_x, first_cap_cell_y, {
                                             x = -candidate.second.x,
                                             y = -candidate.second.y
                                         })
        local first_inner_distance = boundary_distance(
                                         first_cap_cell_x, first_cap_cell_y,
                                         candidate.second)
        local first_outer_x = first_center_x -
                                  candidate.second.x * first_outer_distance
        local first_outer_y = first_center_y -
                                  candidate.second.y * first_outer_distance
        local first_inner_x = first_center_x +
                                  candidate.second.x * first_inner_distance
        local first_inner_y = first_center_y +
                                  candidate.second.y * first_inner_distance
        local first_edge = mesh:slice_at(first_outer_x, first_outer_y,
                                               first_inner_x, first_inner_y)

        local second_distance = pool_radius + cell_size *
                                    (candidate.extend_second and 2 or 1)
        local second_cap_cell_x = candidate.x + candidate.second.x *
                                      (candidate.extend_second and 2 or 1)
        local second_cap_cell_y = candidate.y + candidate.second.y *
                                      (candidate.extend_second and 2 or 1)
        local second_center_x = corner_x +
                                    candidate.second.x * second_distance +
                                    candidate.first.x * corridor_width / 2
        local second_center_y = corner_y +
                                    candidate.second.y * second_distance +
                                    candidate.first.y * corridor_width / 2
        local second_outer_distance = boundary_distance(
                                          second_cap_cell_x,
                                          second_cap_cell_y, {
                                              x = -candidate.first.x,
                                              y = -candidate.first.y
                                          })
        local second_inner_distance = boundary_distance(
                                          second_cap_cell_x,
                                          second_cap_cell_y, candidate.first)
        local second_outer_x = second_center_x -
                                   candidate.first.x * second_outer_distance
        local second_outer_y = second_center_y -
                                   candidate.first.y * second_outer_distance
        local second_inner_x = second_center_x +
                                   candidate.first.x * second_inner_distance
        local second_inner_y = second_center_y +
                                   candidate.first.y * second_inner_distance
        local second_edge = nil
        if first_edge ~= nil then
            second_edge = mesh:slice_at(second_outer_x, second_outer_y,
                                              second_inner_x, second_inner_y)
        end
        if second_edge ~= nil then
            roughen_pool_edge(first_edge, corner_x, corner_y)
            roughen_pool_edge(second_edge, corner_x, corner_y)

            -- Sample the dry floor just beyond the first arm's cap. The
            -- midpoint keeps the point away from either corridor wall.
            local adjacent_x = corner_x +
                                   candidate.first.x * (first_distance + 1) +
                                   candidate.second.x * corridor_width / 2
            local adjacent_y = corner_y +
                                   candidate.first.y * (first_distance + 1) +
                                   candidate.second.y * corridor_width / 2
            local second_adjacent_x = corner_x +
                candidate.second.x * (second_distance + 1) +
                candidate.first.x * corridor_width / 2
            local second_adjacent_y = corner_y +
                candidate.second.y * (second_distance + 1) +
                candidate.first.y * corridor_width / 2
            remember_pool({
                {x = adjacent_x, y = adjacent_y},
                {x = second_adjacent_x, y = second_adjacent_y}
            })
        else
            create_compact_pool()
        end
        end
    end

    -- Apply the width variation only after every operation that locates a
    -- straight wall by coordinate. Try several points so an earlier cut that
    -- inserted a vertex at one candidate does not prevent varying the wall.
    for _, variation in ipairs(wall_variations) do
        local vertex = nil
        for _, fraction in ipairs({0.37, 0.63, 0.23, 0.77, 0.5}) do
            local x = variation.x1 +
                          (variation.x2 - variation.x1) * fraction
            local y = variation.y1 +
                          (variation.y2 - variation.y1) * fraction
            vertex = mesh:split_edge_at(x, y)
            if vertex ~= nil then
                break
            end
        end
        assert(vertex ~= nil, "corridor wall had no segment to vary")

        -- A nearby cut can leave too little room for the full random width
        -- change even though the wall still has a splittable segment. Back
        -- off rather than failing the whole Layer build for that section.
        local moved = false
        local scale = 1
        for _ = 1, 5 do
            moved = mesh:move_vertex(
                vertex, variation.delta_x * scale,
                variation.delta_y * scale)
            if moved then
                break
            end
            scale = scale / 2
        end
    end

    -- Horizontal surfaces use the catalog's 2D material program; walls use
    -- its 3D program. Both variants share the basalt Sub-material id.
    mesh:set_floor_material(layer.vars.mine_material)
    mesh:set_ceiling_material(layer.vars.mine_material)
    mesh:set_wall_material(layer.vars.mine_material)
    mesh:set_ceiling_elevation(0, layer.vars.corridor_base_height,
                               layer.vars.corridor_base_height)

    local floor_primitives = context:decompose_mesh_primitive(mesh)
    local primary = mesh
    local section_primitives = {}
    local floor_regions = {}
    if #floor_primitives > 0 then
        primary = floor_primitives[1]
        for primitive_index, primitive in ipairs(floor_primitives) do
            local height = (math.random() * 2 - 1) * floor_step_variance
            primitive:set_floor_elevation(0, height, height)
            floor_regions[#floor_regions + 1] = {
                primitive = primitive,
                height = height
            }
            if primitive_index > 1 then
                section_primitives[#section_primitives + 1] = primitive
            end
        end
    else
        floor_regions[1] = {primitive = mesh, height = 0}
    end

    local slope_connectors = {}
    for _, connector in ipairs(slope_connector_points) do
        local inner_region = nil
        for _, region in ipairs(floor_regions) do
            if region.primitive:contains_point(connector.inner_x,
                                                connector.inner_y) then
                inner_region = region
                break
            end
        end
        local complete = inner_region ~= nil
        for _, segment in ipairs(connector.segments) do
            for _, region in ipairs(floor_regions) do
                if region.primitive:contains_point(segment.x, segment.y) then
                    segment.primitive = region.primitive
                    region.height = inner_region and inner_region.height or
                                        region.height
                    if inner_region ~= nil then
                        region.primitive:set_floor_elevation(
                            0, inner_region.height, inner_region.height)
                    end
                    break
                end
            end
            complete = complete and segment.primitive ~= nil
        end
        if complete then
            slope_connectors[#slope_connectors + 1] = connector
        end
    end

    for _, pool_point in ipairs(pool_points) do
        local pool_region = nil
        local adjacent_regions = {}
        for _, region in ipairs(floor_regions) do
            if region.primitive:contains_point(pool_point.x, pool_point.y) then
                pool_region = region
            end
            for _, adjacent_point in ipairs(pool_point.adjacent_points) do
                if region.primitive:contains_point(adjacent_point.x,
                                                    adjacent_point.y) then
                    adjacent_regions[#adjacent_regions + 1] = region
                    break
                end
            end
        end
        local adjacent_region = adjacent_regions[1]
        assert(pool_region ~= nil and adjacent_region ~= nil and
                   pool_region ~= adjacent_region,
               "a corner pool did not produce a separate floor polygon")
        -- Enlarged pools have a cap at the end of each arm. Keep both banks
        -- on one sector-relative plane so one randomly lower arm cannot act
        -- as a spillway into the rest of the mine.
        for _, region in ipairs(adjacent_regions) do
            region.height = adjacent_region.height
            region.primitive:set_floor_elevation(
                0, adjacent_region.height, adjacent_region.height)
        end
        pool_region.height = adjacent_region.height
        pool_region.primitive:set_floor_elevation(
            pool_point.angle + 180, adjacent_region.height - 3,
            adjacent_region.height)
        pool_region.primitive:set_liquid_level(CORNER_POOL_LIQUID_LEVEL)
    end

    if selected_rail_run ~= nil and #selected_rail_run.cells >= 2 then
        local first = selected_rail_run.cells[1]
        local last = selected_rail_run.cells[#selected_rail_run.cells]
        local first_x = (first.x + 0.5) * cell_size - map_size / 2
        local first_y = (first.y + 0.5) * cell_size - map_size / 2
        local last_x = (last.x + 0.5) * cell_size - map_size / 2
        local last_y = (last.y + 0.5) * cell_size - map_size / 2
        local direction_x = selected_rail_run.direction_x
        local direction_y = selected_rail_run.direction_y
        local normal_x = -direction_y
        local normal_y = direction_x
        local center_x = (first_x + last_x) / 2
        local center_y = (first_y + last_y) / 2
        local rail_length = math.sqrt((last_x - first_x) ^ 2 +
                                          (last_y - first_y) ^ 2)
        local rail_orientation = math.deg(math.atan(-direction_y,
                                                     direction_x))
        local rail_stop_floor_height = nil
        for _, side in ipairs({-1, 1}) do
            local rail_x = center_x + side * normal_x * 4
            local rail_y = center_y + side * normal_y * 4
            local rail_floor_height = nil
            for _, region in ipairs(floor_regions) do
                if region.primitive:contains_point(rail_x, rail_y) then
                    local _, lower = region.primitive:get_floor_elevation()
                    rail_floor_height = lower
                    break
                end
            end
            assert(rail_floor_height ~= nil,
                   "a selected rail did not have a containing floor region")
            rail_stop_floor_height = rail_stop_floor_height or rail_floor_height

            local rail = context:create_primitive("Rectangle")
            rail:set_size(rail_length, 1)
            -- Tight bounds keep this long, narrow Rectangle from making an
            -- adjacent 256-unit placement cell look occupied.
            rail:set_exact_bounds(true)
            rail:set_position(rail_x, rail_y)
            rail:set_orientation(rail_orientation)
            rail:set_operation("union")
            -- Rails override the tunnel floor; support bridges and posts have
            -- higher priorities and therefore win where a frame crosses them.
            rail:set_priority(1)
            rail:set_floor_elevation(0, rail_floor_height + 1,
                                     rail_floor_height + 1)
            rail:set_ceiling_elevation(0, layer.vars.corridor_base_height,
                                       layer.vars.corridor_base_height)
            rail:set_floor_material("builtin.rusted.iron")
            rail:set_ceiling_material(layer.vars.mine_material)
            rail:set_wall_material("builtin.rusted.iron")
            section_primitives[#section_primitives + 1] = rail
        end

        -- Each end independently gets a wheel stop. The stop rises inward
        -- from rail height, giving the Rectangle a triangular side profile
        -- without extending beyond the rails.
        local function add_rail_stop(endpoint_x, endpoint_y, inward_sign,
                                     slope_angle)
            if math.random() >= RAIL_STOP_CHANCE then
                return
            end

            local stop = context:create_primitive("Rectangle")
            stop:set_size(RAIL_STOP_LENGTH, RAIL_STOP_WIDTH)
            stop:set_exact_bounds(true)
            stop:set_position(
                endpoint_x + inward_sign * direction_x * RAIL_STOP_LENGTH / 2,
                endpoint_y + inward_sign * direction_y * RAIL_STOP_LENGTH / 2)
            stop:set_orientation(rail_orientation)
            stop:set_operation("union")
            stop:set_priority(4)
            stop:set_floor_elevation(
                slope_angle, rail_stop_floor_height + 1,
                rail_stop_floor_height + 1 + RAIL_STOP_HEIGHT)
            stop:set_ceiling_elevation(0, layer.vars.corridor_base_height,
                                       layer.vars.corridor_base_height)
            stop:set_floor_material(layer.vars.mine_material)
            stop:set_ceiling_material(layer.vars.mine_material)
            stop:set_wall_material(layer.vars.mine_material)
            section_primitives[#section_primitives + 1] = stop
        end

        -- Elevation-span angles are in local space: 270 degrees points along
        -- local +X and 90 degrees along local -X.
        add_rail_stop(first_x, first_y, 1, 270)
        add_rail_stop(last_x, last_y, -1, 90)
    end

    local wooden_supports = create_wooden_supports(
                                tile_map, corridor_width, map_size, primary,
                                sliced_cells, floor_regions)
    for _, support in ipairs(wooden_supports) do
        section_primitives[#section_primitives + 1] = support
    end
    return primary, map_size, section_primitives, slope_connectors,
           set_cells, cell_size
end

local function place_tunnels_section(primitive, primitive_size,
                                     section_primitives, slope_connectors,
                                     set_cells, tile_cell_size,
                                     cell_x, cell_y, angle)
    local position_x = (cell_x + 0.5) * primitive_size
    local position_y = (cell_y + 0.5) * primitive_size
    local base_floor_height = base_floor_height_for_cell(cell_x, cell_y)

    assert(tile_cell_size == SCAN_CELL_SIZE,
           "mine occupancy scanning requires 32-unit TileMap cells")
    for _, tile_cell in ipairs(set_cells) do
        local local_x = (tile_cell.x + 0.5) * SCAN_CELL_SIZE -
                            primitive_size / 2
        local local_y = (tile_cell.y + 0.5) * SCAN_CELL_SIZE -
                            primitive_size / 2
        local rotated_x, rotated_y = rotate_point(local_x, local_y, angle)
        mark_scan_cell_containing(position_x + rotated_x,
                                  position_y + rotated_y)
    end

    local function apply_base_floor_height(section_primitive)
        local floor_angle, floor_lower, floor_upper =
            section_primitive:get_floor_elevation()
        section_primitive:set_floor_elevation(
            floor_angle, floor_lower + base_floor_height,
            floor_upper + base_floor_height)
        local ceiling_angle, ceiling_lower, ceiling_upper =
            section_primitive:get_ceiling_elevation()
        section_primitive:set_ceiling_elevation(
            ceiling_angle, ceiling_lower + base_floor_height,
            ceiling_upper + base_floor_height)
    end

    -- create_mesh_primitive normalizes its points around their bounds centre.
    -- Preserve that local centre relative to the TileMap origin; otherwise an
    -- asymmetric TileMap shifts its tunnel while the supports remain in their
    -- cell-relative positions. Rotation can turn that shift onto either axis.
    local primitive_local_x, primitive_local_y = primitive:get_position()
    local primitive_offset_x, primitive_offset_y =
        rotate_point(primitive_local_x, primitive_local_y, angle)
    primitive:set_position(position_x + primitive_offset_x,
                           position_y + primitive_offset_y)
    primitive:set_orientation(angle)
    apply_base_floor_height(primitive)
    context:place_primitive(primitive)

    for _, section_primitive in ipairs(section_primitives) do
        local local_x, local_y = section_primitive:get_position()
        local rotated_x, rotated_y = rotate_point(local_x, local_y, angle)
        section_primitive:set_position(position_x + rotated_x,
                                       position_y + rotated_y)
        section_primitive:set_orientation(
            section_primitive:get_orientation() + angle)
        apply_base_floor_height(section_primitive)
        context:place_primitive(section_primitive)
    end

    for _, connector in ipairs(slope_connectors) do
        local boundary_x, boundary_y = rotate_point(
                                           connector.boundary_x,
                                           connector.boundary_y, angle)
        boundary_x = boundary_x + position_x
        boundary_y = boundary_y + position_y
        local key = string.format("%.3f,%.3f", boundary_x, boundary_y)
        local outward_x, outward_y = rotate_point(
                                         connector.outward_x,
                                         connector.outward_y, angle)
        local first_segment = connector.segments[1]
        local _, floor = first_segment.primitive:get_floor_elevation()
        local _, ceiling = first_segment.primitive:get_ceiling_elevation()
        local placed = {
            segments = connector.segments,
            outward_x = connector.outward_x,
            outward_y = connector.outward_y,
            world_outward_x = outward_x,
            world_outward_y = outward_y,
            floor = floor,
            ceiling = ceiling
        }
        local opposite = unmatched_slope_connectors[key]
        if opposite ~= nil and
            opposite.world_outward_x == -outward_x and
            opposite.world_outward_y == -outward_y then
            local meeting_floor = (placed.floor + opposite.floor) / 2
            local meeting_ceiling = (placed.ceiling + opposite.ceiling) / 2
            for _, end_connector in ipairs({placed, opposite}) do
                local direction_angle = math.deg(math.atan(
                                                    -end_connector.outward_x,
                                                    end_connector.outward_y))
                if direction_angle < 0 then
                    direction_angle = direction_angle + 360
                end
                local function smoothstep(fraction)
                    return fraction * fraction * (3 - 2 * fraction)
                end
                for _, segment in ipairs(end_connector.segments) do
                    local first_weight = smoothstep(segment.first_fraction)
                    local second_weight = smoothstep(segment.second_fraction)
                    segment.primitive:set_floor_elevation(
                        direction_angle,
                        end_connector.floor +
                            (meeting_floor - end_connector.floor) *
                                first_weight,
                        end_connector.floor +
                            (meeting_floor - end_connector.floor) *
                                second_weight)
                    segment.primitive:set_ceiling_elevation(
                        direction_angle,
                        end_connector.ceiling +
                            (meeting_ceiling - end_connector.ceiling) *
                                first_weight,
                        end_connector.ceiling +
                            (meeting_ceiling - end_connector.ceiling) *
                                second_weight)
                end
            end
            unmatched_slope_connectors[key] = nil
        else
            unmatched_slope_connectors[key] = placed
        end
    end
end

local tile_map_mesh, tile_map_size, tile_map_section_primitives,
      tile_map_slope_connectors, tile_map_cells, tile_map_cell_size =
    create_tunnels_section_primitive("TileMaps", 0,
                                     layer.vars.corridor_width)
assert(tile_map_mesh ~= nil, "TileMaps[0] has no set cells")
local tile_map_x, tile_map_y =
    utilities.find_closest_empty_grid_cell(0, 0, tile_map_size)
place_tunnels_section(tile_map_mesh, tile_map_size,
                      tile_map_section_primitives,
                      tile_map_slope_connectors, tile_map_cells,
                      tile_map_cell_size, tile_map_x, tile_map_y, 0)

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
local options_by_prefab = {}
local unrotated_options = {}
for _, prefab in ipairs(tunnel_prefabs) do
    local prefab_options = {}
    options_by_prefab[prefab] = prefab_options
    for _, angle in ipairs(ANGLES) do
        local option = make_option(prefab, angle, #options + 1)
        options[#options + 1] = option
        prefab_options[angle] = option
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
                            local transition = transitions_by_key[key]
                            if transition == nil then
                                transition = {
                                    x = cell.x,
                                    y = cell.y,
                                    option = candidate_option,
                                    connector_keys = {}
                                }
                                transitions[#transitions + 1] = transition
                                transitions_by_key[key] = transition
                            end
                            transition.connector_keys[connector_key] = true
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
local seed_tile_map_index = math.random(0, 1)
local seed_primitive, seed_primitive_size, seed_section_primitives,
      seed_slope_connectors, seed_tile_map_cells, seed_tile_map_cell_size =
    create_tunnels_section_primitive("TileMaps", seed_tile_map_index,
                                     layer.vars.corridor_width)
assert(seed_primitive ~= nil,
       string.format("TileMaps[%d] has no set cells", seed_tile_map_index))
place_tunnels_section(seed_primitive, seed_primitive_size,
                      seed_section_primitives, seed_slope_connectors,
                      seed_tile_map_cells, seed_tile_map_cell_size,
                      seed_x, seed_y, seed.angle)
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
        if not placement.flush_only or
            transition.connector_keys[FLUSH_CONNECTOR_KEY] then
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
end

add_candidates_for_placement(seed_placement)

local tunnel_prefab_count = layer.vars.tunnel_prefab_count
assert(type(tunnel_prefab_count) == "number" and
           tunnel_prefab_count % 1 == 0 and tunnel_prefab_count >= 0 and
           tunnel_prefab_count <= 50,
       "layer.vars.tunnel_prefab_count must be an integer from 0 to 50")
local max_tunnel_prefab_dist = layer.vars.max_tunnel_prefab_dist
assert(type(max_tunnel_prefab_dist) == "number" and
           max_tunnel_prefab_dist % 1 == 0 and
           max_tunnel_prefab_dist >= 0 and max_tunnel_prefab_dist <= 15,
       "layer.vars.max_tunnel_prefab_dist must be an integer from 0 to 15")

local prefab_selection = {}
local required_prefab_count = 0
for _, prefab in ipairs(tunnel_prefabs) do
    local vars = prefab.vars
    local choose_pct = vars.choose_pct
    if choose_pct == nil then
        choose_pct = 0
    end
    assert(type(choose_pct) == "number" and choose_pct == choose_pct and
               choose_pct ~= math.huge and choose_pct ~= -math.huge and
               choose_pct >= 0,
           string.format(
               "Prefab '%s' choose_pct must be a finite non-negative number",
               prefab:get_name()))

    local max_count = vars.max_count
    if max_count == nil then
        max_count = -1
    end
    assert(type(max_count) == "number" and max_count == max_count and
               max_count ~= math.huge and max_count ~= -math.huge and
               max_count % 1 == 0,
           string.format("Prefab '%s' max_count must be an integer",
                         prefab:get_name()))

    local min_count = vars.min_count
    if min_count == nil then
        min_count = 0
    end
    assert(type(min_count) == "number" and min_count == min_count and
               min_count ~= math.huge and min_count ~= -math.huge and
               min_count % 1 == 0,
           string.format("Prefab '%s' min_count must be an integer",
                         prefab:get_name()))
    min_count = math.max(0, min_count)
    assert(max_count < 0 or min_count <= max_count,
           string.format(
               "Prefab '%s' min_count (%d) exceeds max_count (%d)",
               prefab:get_name(), min_count, max_count))

    prefab_selection[#prefab_selection + 1] = {
        prefab = prefab,
        weight = choose_pct,
        maximum = max_count,
        minimum = min_count,
        count = 0
    }
    required_prefab_count = required_prefab_count + min_count
end
assert(required_prefab_count <= tunnel_prefab_count,
       string.format(
           "tunnel_prefab_count (%d) cannot satisfy Prefab min_counts (%d)",
           tunnel_prefab_count, required_prefab_count))

local function weighted_prefab_choice(choices, permit_zero_total)
    local maximum_weight = 0
    for _, choice in ipairs(choices) do
        maximum_weight = math.max(maximum_weight, choice.weight)
    end
    if maximum_weight == 0 then
        if permit_zero_total then
            return choices[math.random(#choices)]
        end
        error("eligible tunnel Prefab choose_pct weights total zero")
    end

    -- Scale first so even very large finite authored weights cannot overflow
    -- their sum and distort selection.
    local total_weight = 0
    for _, choice in ipairs(choices) do
        total_weight = total_weight + choice.weight / maximum_weight
    end
    local target = math.random() * total_weight
    local cumulative_weight = 0
    local last_weighted_choice = nil
    for _, choice in ipairs(choices) do
        if choice.weight > 0 then
            last_weighted_choice = choice
            cumulative_weight = cumulative_weight +
                                    choice.weight / maximum_weight
            if target < cumulative_weight then
                return choice
            end
        end
    end
    return last_weighted_choice
end

local function choose_tunnel_prefab()
    local choices = {}
    for _, choice in ipairs(prefab_selection) do
        if choice.count < choice.minimum then
            choices[#choices + 1] = choice
        end
    end
    if #choices > 0 then
        -- A zero-weight Prefab must still be placeable to meet its minimum.
        return weighted_prefab_choice(choices, true)
    end

    for _, choice in ipairs(prefab_selection) do
        if choice.maximum < 0 or choice.count < choice.maximum then
            choices[#choices + 1] = choice
        end
    end
    if #choices == 0 then
        return nil
    end
    return weighted_prefab_choice(choices, false)
end

local prefab_cells = {}
local first_prefab_cell = math.ceil(-max_tunnel_prefab_dist - 0.5)
local last_prefab_cell = math.floor(max_tunnel_prefab_dist - 0.5)
local maximum_distance_squared = max_tunnel_prefab_dist ^ 2
for y = first_prefab_cell, last_prefab_cell do
    for x = first_prefab_cell, last_prefab_cell do
        local centre_x = x + 0.5
        local centre_y = y + 0.5
        if centre_x * centre_x + centre_y * centre_y <=
            maximum_distance_squared + EPSILON then
            prefab_cells[#prefab_cells + 1] = {x = x, y = y}
        end
    end
end

local function mark_prefab_flush_cells(option, cell_x, cell_y)
    local inward = {
        north = {x = 0, y = -1},
        east = {x = -1, y = 0},
        south = {x = 0, y = 1},
        west = {x = 1, y = 0}
    }
    local connectors = option.connectors[FLUSH_CONNECTOR_KEY]
    for _, direction in ipairs(DIRECTIONS) do
        for _, connector in ipairs(connectors[direction]) do
            local x1, y1, x2, y2 = world_segment(connector, cell_x, cell_y)
            mark_scan_cell_containing(
                (x1 + x2) / 2 + inward[direction].x * SCAN_CELL_SIZE / 2,
                (y1 + y2) / 2 + inward[direction].y * SCAN_CELL_SIZE / 2)
        end
    end
end

local placed_prefab_count = 0
while placed_prefab_count < tunnel_prefab_count and #prefab_cells > 0 do
    local cell_index = math.random(#prefab_cells)
    local cell = prefab_cells[cell_index]
    prefab_cells[cell_index] = prefab_cells[#prefab_cells]
    prefab_cells[#prefab_cells] = nil

    local key = cell_key(cell.x, cell.y)
    if placements_by_cell[key] == nil and cell_is_empty(cell.x, cell.y) then
        local selection = choose_tunnel_prefab()
        if selection == nil then
            break
        end
        local prefab = selection.prefab
        local angle = ANGLES[math.random(#ANGLES)]
        context:place_prefab_instance(
            prefab, cell.x, cell.y, angle,
            base_floor_height_for_cell(cell.x, cell.y))
        local placement = {
            x = cell.x,
            y = cell.y,
            option = options_by_prefab[prefab][angle],
            flush_only = true
        }
        mark_prefab_flush_cells(placement.option, cell.x, cell.y)
        placements_by_cell[key] = placement
        add_candidates_for_placement(placement)
        selection.count = selection.count + 1
        placed_prefab_count = placed_prefab_count + 1
    end
end
for _, selection in ipairs(prefab_selection) do
    assert(selection.count >= selection.minimum,
           string.format(
               "not enough valid Tiles to satisfy Prefab '%s' min_count (%d)",
               selection.prefab:get_name(), selection.minimum))
end
-- Candidate discovery may have cached a Tile as empty before a later Prefab
-- occupied it. Re-query geometry now that the Prefab phase is complete.
empty_cells = {}

dprint(string.format("Placed %d of %d requested prefabs",
                     placed_prefab_count, tunnel_prefab_count))
dprint(string.format("Placing %d tunnel sections", step.vars.iterations))
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
    local tile_map_index = math.random(
        0, context:get_tile_map_count("TileMaps") - 1)
    local primitive, primitive_size, section_primitives, slope_connectors,
          section_tile_map_cells, section_tile_map_cell_size =
        create_tunnels_section_primitive("TileMaps", tile_map_index,
                                         layer.vars.corridor_width)
    assert(primitive ~= nil,
           string.format("TileMaps[%d] has no set cells", tile_map_index))
    place_tunnels_section(primitive, primitive_size, section_primitives,
                          slope_connectors, section_tile_map_cells,
                          section_tile_map_cell_size, candidate.x, candidate.y,
                          candidate.option.angle)
    local placement = {
        x = candidate.x,
        y = candidate.y,
        option = candidate.option
    }
    placements_by_cell[cell_key(candidate.x, candidate.y)] = placement
    add_candidates_for_placement(placement)
end

-- Detect every diagonal pair before placing any cover squares. This keeps the
-- scan independent of the squares it produces and permits adjacent matching
-- windows to overlap one another.
local diagonal_matches = {}
local checked_windows = {}
for _, cell in ipairs(scan_cells) do
    for _, offset in ipairs(WINDOW_OFFSETS) do
        local x = cell.x + offset[1]
        local y = cell.y + offset[2]
        local key = cell_key(x, y)
        if not checked_windows[key] then
            checked_windows[key] = true
            local bottom_left = occupied_scan_cells[cell_key(x, y)] == true
            local bottom_right = occupied_scan_cells[cell_key(x + 1, y)] == true
            local top_left = occupied_scan_cells[cell_key(x, y + 1)] == true
            local top_right = occupied_scan_cells[cell_key(x + 1, y + 1)] == true
            if (bottom_left and top_right and
                not bottom_right and not top_left) or
                (bottom_right and top_left and
                not bottom_left and not top_right) then
                diagonal_matches[#diagonal_matches + 1] = {x = x, y = y}
            end
        end
    end
end

-- Resolve elevations and priorities against the pre-cover geometry as well.
-- Elevation-span endpoints are the extrema of each intersecting Primitive.
for _, match in ipairs(diagonal_matches) do
    local center_x = (match.x + 1) * SCAN_CELL_SIZE
    local center_y = (match.y + 1) * SCAN_CELL_SIZE
    match.orientation = math.random() * 30 - 15
    local radians = math.rad(match.orientation)
    local query_size = DIAGONAL_COVER_SIZE *
                           (math.abs(math.cos(radians)) +
                               math.abs(math.sin(radians)))
    local intersecting = context:find_build_primitives_overlapping(
        center_x - query_size / 2 + EPSILON,
        center_y - query_size / 2 + EPSILON,
        query_size - EPSILON * 2,
        query_size - EPSILON * 2)
    assert(#intersecting > 0,
           "a diagonal occupancy match had no intersecting Primitives")

    local minimum_floor = math.huge
    local maximum_ceiling = -math.huge
    local maximum_priority = -1
    for _, primitive in ipairs(intersecting) do
        local _, floor_lower, floor_upper = primitive:get_floor_elevation()
        local _, ceiling_lower, ceiling_upper =
            primitive:get_ceiling_elevation()
        minimum_floor = math.min(minimum_floor, floor_lower, floor_upper)
        maximum_ceiling = math.max(maximum_ceiling, ceiling_lower,
                                   ceiling_upper)
        maximum_priority = math.max(maximum_priority,
                                    primitive:get_priority())
    end

    assert(maximum_priority < 255,
           "cannot place a diagonal cover above priority 255")
    match.floor = minimum_floor
    if math.random() < 0.5 then
        match.floor = match.floor - DIAGONAL_COVER_FLOOR_DROP
        match.liquid_level = 20 + math.random() * 25
    end
    match.ceiling = maximum_ceiling
    match.priority = maximum_priority + 1
end

for _, match in ipairs(diagonal_matches) do
    local cover = context:create_primitive("Rectangle")
    cover:set_size(DIAGONAL_COVER_SIZE, DIAGONAL_COVER_SIZE)
    cover:set_exact_bounds(true)
    cover:set_position((match.x + 1) * SCAN_CELL_SIZE,
                       (match.y + 1) * SCAN_CELL_SIZE)
    cover:set_orientation(match.orientation)
    cover:set_operation("union")
    cover:set_priority(match.priority)
    cover:set_floor_elevation(0, match.floor, match.floor)
    cover:set_ceiling_elevation(0, match.ceiling, match.ceiling)
    if match.liquid_level ~= nil then
        cover:set_liquid_level(match.liquid_level)
    end
    cover:set_floor_material(layer.vars.mine_material)
    cover:set_ceiling_material(layer.vars.mine_material)
    cover:set_wall_material(layer.vars.mine_material)
    context:place_primitive(cover)
end
