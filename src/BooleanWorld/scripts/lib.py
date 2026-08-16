import sys, ctypes, atexit
from enum import IntEnum
from ctypes import c_int, c_uint8, c_uint32, c_float, c_char_p, c_bool, POINTER


_version3 = sys.version_info >= (3, 0)
_integer = int if _version3 else (int, long)

lib_name = 'core-dll.dll'


class CtypesEnum(IntEnum):
    @classmethod
    def from_param(cls, obj):
        return int(obj)
        
        
class Operation(CtypesEnum):
    Union = 0
    Intersection = 1,
    Difference = 2
    XOR = 3
			
            
class FillRule(CtypesEnum):
    NonZero = 0
    EvenOdd = 1
    
    
class VertexTransformerKey(CtypesEnum):
    Scale = 0
    Angle = 1
    OrbitAngle = 2
    OrbitDistance = 3    
    
    
class TransformFunction(CtypesEnum):
    Sine = 2
    InvCosine = 3
    Triangle = 4
    Saw = 5
    Square = 6    

class TransformOperation(CtypesEnum):
    Add = 0
    Mul = 1
    AbsDiff = 2
    Min = 3
    Max = 4
    Avg = 5
    Less = 6
    Greater = 7
    LessEq = 8
    GreaterEq = 9   
    
    
def load_library():
    import os
    
    # Figure out where the library binary should be
    places = [sys.argv[0]]
    try:
        # Try to search near the wrapper module
        places.insert(0, __file__)
    except NameError:
        # There may be no module (e. g. a standalone app)
        pass
    places = [os.path.dirname(os.path.abspath(place)) for place in places]
    
    # Construct a platform-specific name of the library binary file
    if 'win32' in sys.platform:
        name = lib_name
    else:
        raise RuntimeError('Unsupported platform: ' + sys.platform)
    
    # Now actually try to load the library binary
    for place in places:
        try:
            l = ctypes.CDLL(os.path.join(place, name))
            
            # Set function types
            l.create_world.argtypes = [c_float]
            l.create_world.restype = c_int

            l.destroy_world.restype = c_int
            
            l.set_world_name.argtypes = [c_char_p]
            l.set_world_name.restype = c_int            
            
            l.serialize_world.argtypes = [c_char_p]
            l.serialize_world.restype = c_int            

            l.create_regular_polygon.argtypes = [Operation, FillRule, c_uint32, c_uint32]
            l.create_regular_polygon.restype = c_int
            
            l.create_rectangle_polygon.argtypes = [Operation, FillRule, c_float, c_uint32]
            l.create_rectangle_polygon.restype = c_int

            l.create_torus_polygon.argtypes = [Operation, FillRule, c_float, c_float, c_uint32]
            l.create_torus_polygon.restype = c_int
            
            l.set_primitive_size.argtypes = [c_float, c_float]
            l.set_primitive_size.restype = c_int
            
            l.set_primitive_layer.argtypes = [c_uint8]
            l.set_primitive_layer.restype = c_int

            l.set_primitive_priority.argtypes = [c_uint8]
            l.set_primitive_priority.restype = c_int

            l.set_primitive_flags.argtypes = [c_uint32]
            l.set_primitive_flags.restype = c_int

            l.add_primitive_flags.argtypes = [c_uint32]
            l.add_primitive_flags.restype = c_int

            l.remove_primitive_flags.argtypes = [c_uint32]
            l.remove_primitive_flags.restype = c_int

            l.set_primitive_floor_z.argtypes = [c_float]
            l.set_primitive_floor_z.restype = c_int
            
            l.set_primitive_ceiling_z.argtypes = [c_float]
            l.set_primitive_ceiling_z.restype = c_int 
            
            l.set_primitive_time_update_distance.argtypes = [c_float]
            l.set_primitive_time_update_distance.restype = c_int

            l.set_primitive_time_update_distance.argtypes = [c_float]
            l.set_primitive_time_update_distance.restype = c_int
            
            l.set_primitive_position.argtypes = [c_float, c_float]
            l.set_primitive_position.restype = c_int

            l.set_primitive_transform_offset.argtypes = [c_float, c_float]
            l.set_primitive_transform_offset.restype = c_int
            
            l.set_primitive_influence_eye_origin_offset.argtypes = [c_float, c_float]
            l.set_primitive_influence_eye_origin_offset.restype = c_int
  
            l.set_primitive_influence_eye_angle_offset.argtypes = [c_float]
            l.set_primitive_influence_eye_angle_offset.restype = c_int

            l.set_primitive_follow_orbit_angle.argtypes = [c_bool]
            l.set_primitive_follow_orbit_angle.restype = c_int
            
            l.set_primitive_transform_0_input.argtypes = [c_uint32, c_uint32, c_uint32]
            l.set_primitive_transform_0_input.restype = c_int

            l.set_primitive_transform_0_constant.argtypes = [c_uint32, c_uint32, c_float]
            l.set_primitive_transform_0_constant.restype = c_int
            
            l.set_primitive_transform_0_function.argtypes = [c_uint32, c_uint32, c_uint32, c_float]
            l.set_primitive_transform_0_function.restype = c_int
            
            l.set_primitive_transform_0_operation.argtypes = [c_uint32, c_uint32]
            l.set_primitive_transform_0_operation.restype = c_int
            
            l.set_primitive_animation_value.argtypes = [VertexTransformerKey, POINTER(c_float), c_int]
            l.set_primitive_animation_value.restype = c_int
            
            return l
        except OSError as e:
            print(e)

    raise RuntimeError('Cannot load Core library: no loadable {} found in {}'.format(name, places))
    
    

