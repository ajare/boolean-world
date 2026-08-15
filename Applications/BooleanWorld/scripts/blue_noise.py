import random
import math

# Default values
WIDTH = 512
HEIGHT = 512
RADIUS = 10.0
K = 30  # attempts per active point
cell_size = RADIUS / math.sqrt(2)
grid_width = int(WIDTH / cell_size) + 1
grid_height = int(HEIGHT / cell_size) + 1
grid = [-1] * (grid_width * grid_height)

points = []
active = []

def grid_index(x, y):
    return y * grid_width + x

def dist2(a, b):
    dx = a[0] - b[0]
    dy = a[1] - b[1]
    return dx * dx + dy * dy

def insert_point(p):
    points.append(p)
    active.append(p)

    gx = int(p[0] / cell_size)
    gy = int(p[1] / cell_size)
    grid[grid_index(gx, gy)] = len(points) - 1

def is_valid(p):
    gx = int(p[0] / cell_size)
    gy = int(p[1] / cell_size)

    for y in range(gy - 2, gy + 3):
        for x in range(gx - 2, gx + 3):
            if x < 0 or y < 0 or x >= grid_width or y >= grid_height:
                continue

            idx = grid[grid_index(x, y)]
            if idx != -1:
                if dist2(p, points[idx]) < RADIUS * RADIUS:
                    return False
    return True

def generate_around(p):
    r = RADIUS * (1 + random.random())
    angle = 2 * math.pi * random.random()
    return (
        p[0] + r * math.cos(angle),
        p[1] + r * math.sin(angle),
    )

def generate_blue_noise(count, dim, radius=10.0):
    global WIDTH, HEIGHT, RADIUS, K
    global cell_size, grid_width, grid_height
    global grid
    global active, points

    WIDTH = dim
    HEIGHT = dim
    RADIUS = radius
    K = 30
    cell_size = RADIUS / math.sqrt(2)
    grid_width = int(WIDTH / cell_size) + 1
    grid_height = int(HEIGHT / cell_size) + 1
    grid = [-1] * (grid_width * grid_height)
    points = []
    active = []
    
    # initial point
    p0 = (random.random() * dim, random.random() * dim)
    insert_point(p0)

    while active:
        idx = random.randrange(len(active))
        p = active[idx]

        found = False
        for _ in range(K):
            q = generate_around(p)

            if 0 <= q[0] < dim and 0 <= q[1] < dim:
                if is_valid(q):
                    insert_point(q)
                    found = True

        if not found:
            active.pop(idx)
            
        if len(points) == count:
            break

    return points
