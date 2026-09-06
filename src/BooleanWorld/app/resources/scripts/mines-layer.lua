local utilities = include("World/UtilityFunctions")

local GRID_SIZE = 256
local CONNECTOR_KEY = "flush-connector"
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

local function make_option(prefab, angle, id)
    local connectors = {north = {}, east = {}, south = {}, west = {}}

    for _, edge in ipairs(prefab:get_metadata_edges()) do
        local metadata = edge:get_metadata()
        local direction = metadata[CONNECTOR_KEY]
        if DIRECTION_SET[direction] then
            local x1, y1, x2, y2 = edge:get_endpoints()
            x1, y1 = rotate_point(x1, y1, angle)
            x2, y2 = rotate_point(x2, y2, angle)
            direction = ROTATED_DIRECTION[angle][direction]
            connectors[direction][#connectors[direction] + 1] = {
                x1 = x1,
                y1 = y1,
                x2 = x2,
                y2 = y2
            }
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
        local existing_connectors =
            existing_option.connectors[existing_direction]
        local candidate_direction = OPPOSITE[existing_direction]

        for _, candidate_option in ipairs(options) do
            local candidate_connectors =
                candidate_option.connectors[candidate_direction]
            for _, candidate_segment in ipairs(candidate_connectors) do
                for _, existing_segment in ipairs(existing_connectors) do
                    local x, y = matching_cell(candidate_segment,
                                               existing_segment, 0, 0)
                    if x ~= nil and
                        (#candidate_connectors == 1 and
                             #existing_connectors == 1 or
                         connector_sets_match(candidate_connectors, x, y,
                                              existing_connectors, 0, 0)) then
                        local key = cell_key(x, y) .. ":" .. candidate_option.id
                        if transitions_by_key[key] == nil then
                            local transition = {
                                x = x,
                                y = y,
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

dprint(string.format("Placing %d prefabs", params.iterations))
for _ = 1, params.iterations do
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
