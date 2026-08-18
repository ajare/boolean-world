from ctypes import c_int, c_uint32, c_float, c_char_p

from lib import Operation, FillRule, VertexTransformerKey, TransformFunction, TransformOperation


# Primitive Flags
PF_Interacts = 1
PF_Ghost = 2
PF_NoTimeUpdatePlayerStatic = 4
PF_NoTimeUpdateVisible = 8

# Animation scales
AnimationScales = [
    (1, 10),
    (0, 360),
    (0, 360),
    (0, 500)
]

# Influence scales
InfluenceScales = [
    (0, 500),
    (0, 500),
    (0, 500),
    (0, 500)
]

# Materials
Mat_Marble = 0
Mat_Stone = 1


#
# set_primitive_animation_value()
#
# Set the current Primitive's animation values for a given key
#
def set_primitive_animation_value(l, key, values):
    """
    :param l: library module handle.
    :param key: VertexTransformerKey
    :param values: list of [time, value] pairs, where time should be in [0, 1]
    """
    # Validate
    scale = AnimationScales[key] 
    for value in values:
        t, v = value
        
        if v < scale[0] or v > scale[1]:
            raise Exception(f"Animation value {v} for key {key} out of range: {scale}")
    
    numValues = len(values) * 2
    
    FloatArray = c_float * numValues
    c_array = FloatArray(*[v for ivalues in values for v in ivalues])    
    
    if l.set_primitive_animation_value(key, c_array, numValues) != 0:
        raise Exception(f"Could not set {numValues} animation values for {key}")
   

#
# create_rectangle_polygon()
#
# Create a RectanglePolygon with the given operation, sides and position.  FillRule=NonZero
# is assumed
#
def create_rectangle_polygon(l, op, x, y, size, ratio, angle, orbit_distance, orbit_angle, material, floorZ=0, ceilingZ=40):
    """
    :param l: library module handle.
    :param sides: numbers of sides.  Should be >= 3
    :param op: Operation enum from module
    :param x: global x position
    :param y: global y position
    :param size: radius
    :param ratio: width/height ratio
    :param angle: angle (both keyframes are set to this)
    :param orbit_distance: orbit distance (both keyframes are set to this)
    :param orbit_angle: orbit angle (both keyframes are set to this)
    :material: material index
    :floorZ: floor position
    :ceilingZ: ceiling position - must be higher than floor!
    """
    print(f"Creating rectangle polygon with ratio of {ratio} at {x},{y}")
    l.create_rectangle_polygon(op, FillRule.NonZero, ratio, material)
    l.set_primitive_priority(0)
    l.set_primitive_position(x, y)
    l.set_primitive_size(size, size)
    
    l.set_primitive_floor_z(floorZ)
    l.set_primitive_ceiling_z(ceilingZ)
     
    set_primitive_animation_value(l, VertexTransformerKey.Scale, [(0, 1), (1, 1)])
    l.set_primitive_transform_0_constant(VertexTransformerKey.Scale, 0, 1)
    l.set_primitive_transform_0_constant(VertexTransformerKey.Scale, 1, 1)
    
    set_primitive_animation_value(l, VertexTransformerKey.Angle, [(0, angle), (1, angle)])
    l.set_primitive_transform_0_constant(VertexTransformerKey.Angle, 0, 1)
    l.set_primitive_transform_0_constant(VertexTransformerKey.Angle, 1, 1)
      
    set_primitive_animation_value(l, VertexTransformerKey.OrbitAngle, [(0, orbit_angle), (1, orbit_angle)])
    l.set_primitive_transform_0_constant(VertexTransformerKey.OrbitAngle, 0, 1)
    l.set_primitive_transform_0_constant(VertexTransformerKey.OrbitAngle, 1, 1)
    
    set_primitive_animation_value(l, VertexTransformerKey.OrbitDistance, [(0, orbit_distance), (1, orbit_distance)])
    l.set_primitive_transform_0_constant(VertexTransformerKey.OrbitDistance, 0, 1)
    l.set_primitive_transform_0_constant(VertexTransformerKey.OrbitDistance, 1, 1)
    
    
#
# create_regular_polygon()
#
# Create a RegularPolygon with the given operation, sides and position.  FillRule=NonZero
# is assumed
#
def create_regular_polygon(l, sides, op, x, y, size, angle, orbit_distance, orbit_angle, material, floorZ=0, ceilingZ=40):
    """
    :param l: library module handle.
    :param sides: numbers of sides.  Should be >= 3
    :param op: Operation enum from module
    :param x: global x position
    :param y: global y position
    :param size: radius
    :param angle: angle (both keyframes are set to this)
    :param orbit_distance: orbit distance (both keyframes are set to this)
    :param orbit_angle: orbit angle (both keyframes are set to this)
    :material: material index
    :floorZ: floor position
    :ceilingZ: ceiling position - must be higher than floor!
    """
    print(f"Creating regular polygon with {sides} sides at {x},{y}")
    l.create_regular_polygon(op, FillRule.NonZero, sides, material)
    l.set_primitive_priority(0)
    l.set_primitive_position(x, y)
    l.set_primitive_size(size, size)
    
    l.set_primitive_floor_z(floorZ)
    l.set_primitive_ceiling_z(ceilingZ)
     
    set_primitive_animation_value(l, VertexTransformerKey.Scale, [(0, 1), (1, 1)])
    l.set_primitive_transform_0_constant(VertexTransformerKey.Scale, 0, 1)
    l.set_primitive_transform_0_constant(VertexTransformerKey.Scale, 1, 1)
    
    set_primitive_animation_value(l, VertexTransformerKey.Angle, [(0, angle), (1, angle)])
    l.set_primitive_transform_0_constant(VertexTransformerKey.Angle, 0, 1)
    l.set_primitive_transform_0_constant(VertexTransformerKey.Angle, 1, 1)
      
    set_primitive_animation_value(l, VertexTransformerKey.OrbitAngle, [(0, orbit_angle), (1, orbit_angle)])
    l.set_primitive_transform_0_constant(VertexTransformerKey.OrbitAngle, 0, 1)
    l.set_primitive_transform_0_constant(VertexTransformerKey.OrbitAngle, 1, 1)
    
    set_primitive_animation_value(l, VertexTransformerKey.OrbitDistance, [(0, orbit_distance), (1, orbit_distance)])
    l.set_primitive_transform_0_constant(VertexTransformerKey.OrbitDistance, 0, 1)
    l.set_primitive_transform_0_constant(VertexTransformerKey.OrbitDistance, 1, 1)


#
# create_torus_polygon()
#
# Create a RegularPolygon with the given operation, sides and position.  FillRule=NonZero
# is assumed
#
def create_torus_polygon(l, op, x, y, size, thickness, material):
    """
    :param l: library module handle.
    :param op: Operation enum from module
    :param x: global x position
    :param y: global y position
    :param size: radius (outer)
    :material: material index
    """
    l.create_torus_polygon(op, FillRule.NonZero, thickness, 1, material)
    l.set_primitive_priority(0)
    l.set_primitive_position(x, y)
    l.set_primitive_size(size, size)
     
    set_primitive_animation_value(l, VertexTransformerKey.Scale, [(0, 1), (1, 1)])
    set_primitive_animation_value(l, VertexTransformerKey.Angle, [(0, 0), (1, 0)])
    set_primitive_animation_value(l, VertexTransformerKey.OrbitAngle, [(0, 0), (1, 0)])
    set_primitive_animation_value(l, VertexTransformerKey.OrbitDistance, [(0, 0), (1, 0)])  
    
    
#
# create_regular_reactive_rotator()
#
# Create a Regular Polygon Primitive which rotates when Player approaches it, following the rules for
# non-visible clipping
#
#
def create_regular_reactive_rotator(l, sides, op, x, y, size, timeUpdateDist, angle0, angle1, rotateTime, fn, material):
    l.create_regular_polygon(op, FillRule.NonZero, sides, material)
    l.set_primitive_priority(0)
    l.set_primitive_position(x, y)
    l.set_primitive_size(size, size)
    #l.set_primitive_flags(PF_NoTimeUpdatePlayerStatic + PF_NoTimeUpdateVisible)
    #l.set_primitive_time_update_distance(timeUpdateDist)
    
    l.set_primitive_transform_0_function(VertexTransformerKey.Angle, 0, fn, rotateTime)
    
    set_primitive_animation_value(l, VertexTransformerKey.Scale, [(0, 1), (1, 1)])
    set_primitive_animation_value(l, VertexTransformerKey.Angle, [(0, angle0), (1, angle1)])
    set_primitive_animation_value(l, VertexTransformerKey.OrbitAngle, [(0, 0), (1, 0)])
    set_primitive_animation_value(l, VertexTransformerKey.OrbitDistance, [(0, 0), (1, 0)])
    

#
# create_rectangle_reactive_rotator()
#
# Create a Rectangle Primitive which rotates when Player approaches it, following the rules for
# non-visible clipping
#
#
def create_rectangular_reactive_rotator(l, ratio, op, x, y, timeUpdateDist, angle0, angle1, material):
    l.create_rectangle_polygon(op, FillRule.NonZero, ratio, material)
    l.set_primitive_priority(0)
    l.set_primitive_position(x, y)
    l.set_primitive_flags(PF_NoTimeUpdatePlayerStatic + PF_NoTimeUpdateVisible)
    l.set_primitive_time_update_distance(timeUpdateDist)
    
    set_primitive_animation_value(l, VertexTransformerKey.Scale, [(0, 1), (1, 1)])
    set_primitive_animation_value(l, VertexTransformerKey.Angle, [(0, angle0), (1, angle1)])
    set_primitive_animation_value(l, VertexTransformerKey.OrbitAngle, [(0, 0), (1, 0)])
    set_primitive_animation_value(l, VertexTransformerKey.OrbitDistance, [(0, 0), (1, 0)])
