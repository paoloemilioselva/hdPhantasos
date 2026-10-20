"""Write examples/city_drive.usda: a city street driven from behind a car.

The layout follows Matrix-Game 4.0's City Racing scene, so the stage can feed
its racing checkpoint as well as any other model: a third-person view behind
a four-door sedan driving down a street between buildings, with parked cars,
pedestrians, street lamps and trees. Every thing is a described subject.

Plain Python, no OpenUSD needed: run it, then look at the result with
    launch_phantasos.bat examples\\city_drive.usda
Edit the tables below and run it again rather than editing the .usda.
"""

import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
OUTPUT = os.path.join(HERE, "city_drive.usda")

FPS = 24
FRAMES = 120              # 5 seconds
SPEED = 12.0              # metres a second, about 43 km/h
LANE_X = 1.6              # the ego car keeps to the right-hand lane, clear of
                          # the cars parked along the kerb
ROAD_HALF = 5.0           # road from -5 to +5 in x
KERB = 8.0                # sidewalks from 5 to 8; buildings start beyond
STREET_START, STREET_END = 40.0, -260.0

SHOT = ("Daytime city, cloudy sky, soft overcast light. A third-person driving view behind a "
        "modern four-door sedan in the right-hand lane of a city street, between multi-storey "
        "buildings. Photographic, realistic.")

# One description per building, in order down each side of the street.
LEFT_BUILDINGS = [
    "a dark grey glass office tower with narrow vertical mullions",
    "a red-brown brick apartment block with black iron fire escapes",
    "a pale limestone bank with tall columned windows",
    "a cream stucco hotel with striped awnings over the ground floor",
    "a dark grey high-rise with a stepped crown",
    "a narrow red brick shop house with a green painted shopfront",
    "a blue glass office block reflecting the clouds",
    "a weathered concrete parking garage with open levels",
    "a sand-coloured art deco tower with vertical ribs",
    "a low brick warehouse converted to loft apartments",
]
RIGHT_BUILDINGS = [
    "a silver-grey high-rise with horizontal bands of windows",
    "a brown brick apartment block with bay windows",
    "a white modern office building with deep balconies",
    "a red-brown high-rise with a water tower on the roof",
    "a corner diner with a chrome facade and a neon sign",
    "a grey stone courthouse with a broad flight of steps",
    "a black steel and glass tower",
    "a yellow brick tenement with laundry lines between windows",
    "a green glass hotel with a curved front",
    "a terracotta-clad office building with arched windows",
]

PARKED_CARS = [  # (side, z, description)
    (+1, -18.0, "a silver hatchback parked against the kerb"),
    (+1, -34.0, "a dark blue estate car parked against the kerb"),
    (-1, -26.0, "a white delivery van parked against the kerb"),
    (+1, -61.0, "a red SUV parked against the kerb"),
    (-1, -72.0, "a black taxi parked against the kerb"),
]

PEDESTRIANS = [  # (x, z at frame 1, z at the last frame, description)
    (6.5, -20.0, -26.0, "a woman in a beige trench coat walking away along the sidewalk"),
    (-6.8, -38.0, -32.0, "a man in a dark suit with a briefcase walking toward the camera"),
    (6.2, -55.0, -55.0, "a delivery cyclist in a yellow jacket standing beside the kerb"),
]


def lines_to_text(lines):
    return "\n".join(lines) + "\n"


def describe(indent, subject, description, role=None, has_front=False):
    pad = " " * indent
    out = [f'{pad}string primvars:phantasos:subject = "{subject}"',
           f'{pad}string primvars:phantasos:description = "{description}"']
    if role:
        out.append(f'{pad}token primvars:phantasos:role = "{role}"')
    if has_front:
        out.append(f"{pad}bool primvars:phantasos:hasFront = 1")
    return out


def cube(indent, name, translate, scale):
    pad = " " * indent
    return [f'{pad}def Cube "{name}"', f"{pad}{{",
            f"{pad}    double size = 2",
            f"{pad}    double3 xformOp:translate = {tuple(translate)}",
            f"{pad}    float3 xformOp:scale = {tuple(scale)}",
            f'{pad}    uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:scale"]',
            f"{pad}}}"]


def car_parts(indent, length=4.6, width=1.85, height=1.45):
    """A four-door sedan in blocks, its front along +Z: body, cabin, wheels."""
    half_l, half_w = length / 2, width / 2
    out = cube(indent, "body", (0, 0.55, 0), (half_w, 0.32, half_l))
    out += cube(indent, "cabin", (0, 1.12, -0.2), (half_w * 0.88, (height - 0.87) / 2, half_l * 0.48))
    pad = " " * indent
    for name, x, z in (("wheelFL", 1, 1), ("wheelFR", -1, 1), ("wheelRL", 1, -1), ("wheelRR", -1, -1)):
        out += [f'{pad}def Cylinder "{name}"', f"{pad}{{",
                f'{pad}    token axis = "X"',
                f"{pad}    double height = 0.25",
                f"{pad}    double radius = 0.33",
                f"{pad}    double3 xformOp:translate = ({x * (half_w - 0.05)}, 0.33, {z * (half_l - 0.85)})",
                f'{pad}    uniform token[] xformOpOrder = ["xformOp:translate"]',
                f"{pad}}}"]
    return out


def figure(indent):
    """A person in blocks, about 1.75 m tall, facing +Z."""
    out = cube(indent, "legs", (0, 0.45, 0), (0.17, 0.45, 0.12))
    out += cube(indent, "torso", (0, 1.2, 0), (0.22, 0.32, 0.13))
    out += cube(indent, "arms", (0, 1.15, 0), (0.3, 0.3, 0.08))
    pad = " " * indent
    out += [f'{pad}def Sphere "head"', f"{pad}{{",
            f"{pad}    double radius = 0.12",
            f"{pad}    double3 xformOp:translate = (0, 1.64, 0)",
            f'{pad}    uniform token[] xformOpOrder = ["xformOp:translate"]',
            f"{pad}}}"]
    return out


def main():
    # The ego car must pass the parked cars, not drive through them.
    car_half_width = 1.85 / 2
    parked_x = ROAD_HALF - 0.95
    assert LANE_X + car_half_width < parked_x - car_half_width, "the ego car's lane is not clear"

    rng = random.Random(4)
    last = FRAMES
    travel = SPEED * (FRAMES - 1) / FPS
    L = ['#usda 1.0',
         '(',
         '    doc = """A city street, driven from behind a car, for hdPhantasos.',
         '',
         '    Laid out like Matrix-Game 4.0\'s City Racing scene: a third-person view',
         '    behind a four-door sedan in the right-hand lane, buildings on both sides,',
         '    parked cars, pedestrians, street lamps and trees. Every thing is a',
         '    described subject (primvars:phantasos:*). The car drives',
         f'    {travel:.0f} m in {FRAMES} frames at {FPS} fps; the camera follows it.',
         '',
         '    Written by examples/make_city_drive.py: edit that and run it again."""',
         '    defaultPrim = "World"',
         '    metersPerUnit = 1',
         '    upAxis = "Y"',
         '    startTimeCode = 1',
         f'    endTimeCode = {last}',
         f'    timeCodesPerSecond = {FPS}',
         f'    framesPerSecond = {FPS}',
         ')',
         '',
         # The shot, on the root, so every mesh inherits it.
         'def Xform "World" (',
         '    prepend apiSchemas = ["PhantasosAPI"]',
         ')',
         '{',
         f'    string primvars:phantasos:shot = "{SHOT}"',
         '']

    # --- Ground: everything beyond the kerbs, the grey checkerboard.
    L += ['    def Mesh "ground" (', '        prepend apiSchemas = ["PhantasosAPI"]', '    )', '    {']
    L += describe(8, "ground", "grey paving slabs", role="ground")
    L += ['        int[] faceVertexCounts = [4]',
          '        int[] faceVertexIndices = [0, 1, 2, 3]',
          f'        point3f[] points = [(-60, 0, {STREET_START}), (60, 0, {STREET_START}), '
          f'(60, 0, {STREET_END}), (-60, 0, {STREET_END})]',
          '    }', '']

    # --- Road and lane markings.
    L += ['    def Mesh "road" (', '        prepend apiSchemas = ["PhantasosAPI"]', '    )', '    {']
    L += describe(8, "road", "a wide asphalt city street, dark grey and slightly worn")
    L += ['        int[] faceVertexCounts = [4]',
          '        int[] faceVertexIndices = [0, 1, 2, 3]',
          f'        point3f[] points = [(-{ROAD_HALF}, 0.01, {STREET_START}), ({ROAD_HALF}, 0.01, {STREET_START}), '
          f'({ROAD_HALF}, 0.01, {STREET_END}), (-{ROAD_HALF}, 0.01, {STREET_END})]',
          '    }', '']

    dashes = [(0.0, 0.03, z) for z in range(int(STREET_START), int(STREET_END), -9)]
    L += ['    def PointInstancer "laneMarkings" (', '        prepend apiSchemas = ["PhantasosAPI"]', '    )', '    {']
    L += describe(8, "lane markings", "white dashed centre-line markings")
    L += ['        rel prototypes = </World/laneMarkings/Prototypes/dash>',
          f'        int[] protoIndices = [{", ".join("0" for _ in dashes)}]',
          f'        point3f[] positions = [{", ".join(str(p) for p in dashes)}]',
          '        def Scope "Prototypes"', '        {']
    L += cube(12, "dash", (0, 0, 0), (0.08, 0.02, 1.5))
    L += ['        }', '    }', '']

    # --- Sidewalks, one subject on both sides.
    L += ['    def Xform "sidewalks" (', '        prepend apiSchemas = ["PhantasosAPI"]', '    )', '    {']
    L += describe(8, "sidewalks", "concrete sidewalks with granite kerbs")
    mid_z, half_z = (STREET_START + STREET_END) / 2, (STREET_START - STREET_END) / 2
    half_w = (KERB - ROAD_HALF) / 2
    L += cube(8, "left", (-(ROAD_HALF + half_w), 0.075, mid_z), (half_w, 0.075, half_z))
    L += cube(8, "right", (ROAD_HALF + half_w, 0.075, mid_z), (half_w, 0.075, half_z))
    L += ['    }', '']

    # --- Buildings: one subject each, down both sides.
    L += ['    def Scope "buildings"', '    {']
    for side, names in ((-1, LEFT_BUILDINGS), (+1, RIGHT_BUILDINGS)):
        z = STREET_START - 2
        for index, description in enumerate(names):
            depth = rng.uniform(14, 26)
            width = rng.uniform(10, 16)
            height = rng.choice([12, 18, 24, 32, 45, 60])
            name = f"{'left' if side < 0 else 'right'}{index + 1:02d}"
            x = side * (KERB + 1 + width / 2)
            L += [f'        def Cube "{name}" (', '            prepend apiSchemas = ["PhantasosAPI"]', '        )',
                  '        {']
            L += describe(12, f"building {name}", description)
            L += ['            double size = 2',
                  f'            double3 xformOp:translate = ({x:.2f}, {height / 2:.2f}, {z - depth / 2:.2f})',
                  f'            float3 xformOp:scale = ({width / 2:.2f}, {height / 2:.2f}, {depth / 2:.2f})',
                  '            uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:scale"]',
                  '        }']
            z -= depth + rng.uniform(0.5, 4)
    L += ['    }', '']

    # --- Street lamps and trees, instanced along both kerbs.
    def street_furniture(name, subject, description, spacing, offset, prototype):
        positions = []
        for side in (-1, 1):
            for z in range(int(STREET_START - offset), int(STREET_END), -spacing):
                positions.append((side * (ROAD_HALF + 0.6), 0.15, float(z)))
        out = [f'    def PointInstancer "{name}" (', '        prepend apiSchemas = ["PhantasosAPI"]', '    )', '    {']
        out += describe(8, subject, description)
        out += [f'        rel prototypes = </World/{name}/Prototypes/item>',
                f'        int[] protoIndices = [{", ".join("0" for _ in positions)}]',
                f'        point3f[] positions = [{", ".join(str(p) for p in positions)}]',
                '        def Scope "Prototypes"', '        {',
                '            def Xform "item"', '            {']
        out += prototype
        out += ['            }', '        }', '    }', '']
        return out

    lamp = ['                def Cylinder "pole"', '                {', '                    token axis = "Y"',
            '                    double height = 6', '                    double radius = 0.08',
            '                    double3 xformOp:translate = (0, 3, 0)',
            '                    uniform token[] xformOpOrder = ["xformOp:translate"]', '                }',
            '                def Sphere "lantern"', '                {', '                    double radius = 0.3',
            '                    double3 xformOp:translate = (0, 6.1, 0)',
            '                    uniform token[] xformOpOrder = ["xformOp:translate"]', '                }']
    tree = ['                def Cylinder "trunk"', '                {', '                    token axis = "Y"',
            '                    double height = 2.4', '                    double radius = 0.15',
            '                    double3 xformOp:translate = (0, 1.2, 0)',
            '                    uniform token[] xformOpOrder = ["xformOp:translate"]', '                }',
            '                def Sphere "crown"', '                {', '                    double radius = 1.6',
            '                    double3 xformOp:translate = (0, 3.6, 0)',
            '                    uniform token[] xformOpOrder = ["xformOp:translate"]', '                }']
    L += street_furniture("streetLamps", "street lamps", "black cast-iron street lamps", 24, 6, lamp)
    L += street_furniture("streetTrees", "street trees", "young plane trees with full green crowns", 24, 18, tree)

    # --- Parked cars, each its own subject, facing along the street.
    L += ['    def Scope "parkedCars"', '    {']
    for index, (side, z, description) in enumerate(PARKED_CARS):
        name = f"car{index + 1:02d}"
        L += [f'        def Xform "{name}" (', '            prepend apiSchemas = ["PhantasosAPI"]', '        )', '        {']
        L += describe(12, f"parked car {index + 1}", description, has_front=True)
        heading = 180 if side > 0 else 0  # traffic drives on the right
        L += [f'            double3 xformOp:translate = ({side * (ROAD_HALF - 0.95)}, 0, {z})',
              f'            float xformOp:rotateY = {heading}',
              '            uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateY"]']
        L += car_parts(12)
        L += ['        }']
    L += ['    }', '']

    # --- Pedestrians, block figures, some walking.
    L += ['    def Scope "pedestrians"', '    {']
    for index, (x, z0, z1, description) in enumerate(PEDESTRIANS):
        name = f"person{index + 1:02d}"
        heading = 180 if z1 < z0 else 0
        L += [f'        def Xform "{name}" (', '            prepend apiSchemas = ["PhantasosAPI"]', '        )', '        {']
        L += describe(12, f"pedestrian {index + 1}", description, has_front=True)
        if z0 == z1:
            L += [f'            double3 xformOp:translate = ({x}, 0.15, {z0})']
        else:
            L += ['            double3 xformOp:translate.timeSamples = {',
                  f'                1: ({x}, 0.15, {z0}),', f'                {last}: ({x}, 0.15, {z1}),', '            }']
        L += [f'            float xformOp:rotateY = {heading}',
              '            uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateY"]']
        L += figure(12)
        L += ['        }']
    L += ['    }', '']

    # --- The ego car, driving away from the camera down the right-hand lane.
    L += ['    def Xform "egoCar" (', '        prepend apiSchemas = ["PhantasosAPI"]', '    )', '    {']
    L += describe(8, "ego car",
                  "a modern four-door sedan with a curved roof and a separate rear trunk, metallic dark grey",
                  has_front=True)
    L += ['        double3 xformOp:translate.timeSamples = {',
          f'            1: ({LANE_X}, 0, 0),', f'            {last}: ({LANE_X}, 0, {-travel:.2f}),', '        }',
          '        float xformOp:rotateY = 180',
          '        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateY"]']
    L += car_parts(8)
    L += ['    }', '']

    # --- The chase camera: behind and above the car, moving with it. 16:9 at
    # the model's 864 x 480 is close enough; usdrecord sizes from the aperture.
    L += ['    def Camera "camera"', '    {',
          '        float focalLength = 24',
          '        float horizontalAperture = 36',
          '        float verticalAperture = 20',
          '        float2 clippingRange = (0.1, 2000)',
          '        double3 xformOp:translate.timeSamples = {',
          f'            1: ({LANE_X}, 2.6, 7.5),', f'            {last}: ({LANE_X}, 2.6, {7.5 - travel:.2f}),', '        }',
          '        float xformOp:rotateX = -9',
          '        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateX"]',
          '    }']
    L += ['}']

    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(lines_to_text(L))
    print("wrote", OUTPUT)


if __name__ == "__main__":
    main()
