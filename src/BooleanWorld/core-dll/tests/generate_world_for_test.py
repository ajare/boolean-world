import os
import runpy
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))
python_scripts_dir = os.path.join(script_dir, "..", "..", "scripts")
sys.path.insert(0, python_scripts_dir)

from core import Mat_Marble, create_rectangle_polygon
from lib import Operation, load_library

create_world = runpy.run_path(
    os.path.join(python_scripts_dir, "gen-world.py"))["create_world"]


WORLD_SIZE = 1536
ORBIT_ANGLE = 37
ORBIT_DISTANCE = 123


def main():
    if len(sys.argv) != 2:
        raise RuntimeError("usage: generate_world_for_test.py <output path>")

    library = load_library()
    create_world(library, "python-generated-world", WORLD_SIZE)

    try:
        create_rectangle_polygon(
            library,
            Operation.Union,
            0,
            0,
            100,
            1,
            0,
            ORBIT_DISTANCE,
            ORBIT_ANGLE,
            Mat_Marble)
        if library.serialize_world(sys.argv[1].encode("utf-8")) != 0:
            raise RuntimeError("could not serialize test world")
    finally:
        library.destroy_world()


if __name__ == "__main__":
    main()
