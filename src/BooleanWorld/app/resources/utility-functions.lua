local utilities = {}

local function is_integer(value)
    return type(value) == "number" and value == math.floor(value)
end

-- Returns the Prefabs from a DefinePrefabs step whose Tile grid has the
-- requested size, preserving the step's authored collection order.
function utilities.get_prefabs_with_grid_size(step, grid_size)
    local matches = {}

    for _, prefab in ipairs(step:get_prefabs()) do
        if prefab:get_tile_size() == grid_size then
            matches[#matches + 1] = prefab
        end
    end

    return matches
end

local function is_empty(cell_x, cell_y, grid_size)
    return #context:find_build_primitives_overlapping(
        cell_x * grid_size,
        cell_y * grid_size,
        grid_size,
        grid_size
    ) == 0
end

-- Finds the empty grid cell nearest to (origin_x, origin_y), measured by
-- squared Euclidean distance between cell coordinates. Equal-distance cells
-- prefer the greatest X + Y; equal sums prefer the greatest X, then Y, to
-- keep the otherwise unspecified tie deterministic.
function utilities.find_closest_empty_grid_cell(origin_x, origin_y, grid_size)
    assert(is_integer(origin_x), "origin cell X must be an integer")
    assert(is_integer(origin_y), "origin cell Y must be an integer")
    assert(type(grid_size) == "number" and grid_size > 0 and
               grid_size < math.huge,
           "grid size must be a positive finite number")

    local best_x = nil
    local best_y = nil
    local best_distance = math.huge
    local best_sum = -math.huge
    local radius = 0

    while true do
        for offset_y = -radius, radius do
            for offset_x = -radius, radius do
                if radius == 0 or math.abs(offset_x) == radius or
                    math.abs(offset_y) == radius then
                    local cell_x = origin_x + offset_x
                    local cell_y = origin_y + offset_y
                    local distance = offset_x * offset_x + offset_y * offset_y
                    local sum = cell_x + cell_y

                    if distance <= best_distance and
                        is_empty(cell_x, cell_y, grid_size) and
                        (distance < best_distance or sum > best_sum or
                         (sum == best_sum and
                          (best_x == nil or cell_x > best_x or
                           (cell_x == best_x and cell_y > best_y)))) then
                        best_x = cell_x
                        best_y = cell_y
                        best_distance = distance
                        best_sum = sum
                    end
                end
            end
        end

        -- Every unvisited cell is at least radius + 1 cells away along one
        -- axis. Once that is farther than the best candidate, no later ring
        -- can replace it or tie it.
        if best_x ~= nil and (radius + 1) * (radius + 1) > best_distance then
            return best_x, best_y
        end

        radius = radius + 1
    end
end

return utilities
