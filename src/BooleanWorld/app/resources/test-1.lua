local utilities = include("World/UtilityFunctions")

local room = context:create_primitive("Rectangle")
room:set_size(64, 32)
room:set_position(128, 64)
room:set_operation("union")
room:set_priority(0)
context:place_primitive(room)

local grid_x, grid_y = utilities.find_closest_empty_grid_cell(2, 1, 64)
