"""
TODO:
- Store first Primitive with id 1 so we can load into Editor without getting overwritten by the Ghost
- Function to set the basic "reactive" polygons which animate when out of sight
"""

import sys
import random
import traceback

from lib import load_library, Operation, FillRule, VertexTransformerKey, TransformFunction, TransformOperation
from core import *
from blue_noise import generate_blue_noise

    
#
# create_world()
#
# Initialise World with ghost Primitive and set as active
#
def create_world(l, name, size):
    if l.create_world(8192) != 0:
        raise Exception(f"Could not create World '{name}' with size {size}")
        
    l.set_world_name(name.encode("utf-8"))
    
    # Create Ghost
    #create_regular_polygon(l, 3, Operation.Union, 0, 0, 230, 0, 0, 0, Mat_Marble)
    #l.set_primitive_flags(PF_Ghost)

 
#
# Entrypoint
#
if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Syntax: python {sys.argv[0]} <world_name> <size>")
        sys.exit(1)
    
    exitCode = 0
    
    try:
        worldName = sys.argv[1]
        worldSize = int(sys.argv[2])
        
        l = load_library()

        create_world(l, worldName, worldSize)
        
        create_rectangle_polygon(l, Operation.Union, 0, 0, 100, 1, 0, 0, 0, Mat_Marble)
        create_rectangle_polygon(l, Operation.Union, -50, 50, 60, 1, 0, 0, 0, Mat_Marble)

        create_rectangle_polygon(l, Operation.Difference, 0, 0, 70, 1, 0, 0, 0, Mat_Stone)

        create_rectangle_polygon(l, Operation.Union, 50, -50, 100, 1, 0, 0, 0, Mat_Marble)
        create_rectangle_polygon(l, Operation.Difference, 50, -50, 70, 1, 0, 0, 0, Mat_Stone)
        
        
        """
        create_regular_polygon(l, 4, Operation.Union, 0, 0, 200, 0, 0, 0, Mat_Marble)
        create_regular_polygon(l, 4, Operation.Union, 100, 100, 200, 0, 0, 0, Mat_Marble)
        
        maxSize = 300
        maxSize2 = maxSize / 2
        minDim = -(worldSize / 2) + maxSize2
        maxDim = (worldSize / 2) - maxSize2
        
        points = generate_blue_noise(1000, worldSize - maxSize, 100)
        points = []
        for point in points:
            x = point[0] - (worldSize - maxSize) * 0.5
            y = point[1] - (worldSize - maxSize) * 0.5
            
            angle = random.random() * 360
            create_regular_polygon(l, 4, Operation.Union, x, y, 165, angle, 0, 0, random.choice([Mat_Marble, Mat_Stone]), int(random.random() * 8), int(32 + random.random() * 24))

        print(f"Created {len(points)} primitives")
        """ 
        # Save
        filename = f"{worldName}.yaml"
        
        print(f"Writing to {filename}")
        l.serialize_world(filename.encode("utf-8"))
            
        # Exit
        l.destroy_world()
    except Exception as e:
        print(traceback.format_exc())
        print(e)
        exitCode = 1
    finally:
        sys.exit(exitCode)