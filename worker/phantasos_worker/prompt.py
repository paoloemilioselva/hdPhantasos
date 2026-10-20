"""The prompt, written from a frame's manifest.

The proxy already says where everything is; the prompt says what everything
is. In the flat and shaded looks every subject has its own colour, so each
is named by its colour (and its place in the picture, to tell two of a
colour apart): "the magenta block in the lower left is an old oak chest".
In clay, where everything is grey, subjects are named by place alone.

Positions are coarse on purpose (a 3x3 grid): a prompt that changes with
every small camera move would have to be encoded again for every frame,
while one that changes only when the picture's content does is encoded
once and reused.
"""

import colorsys

MAX_SUBJECTS = 24
MIN_COVERAGE = 0.0008


def colour_name(rgb):
    r, g, b = rgb
    h, l, s = colorsys.rgb_to_hls(r, g, b)
    if s < 0.15:
        return "grey"
    hue = h * 360
    names = [(15, "red"), (40, "orange"), (65, "yellow"), (95, "lime green"),
             (150, "green"), (180, "teal"), (200, "cyan"), (225, "sky blue"),
             (255, "blue"), (285, "purple"), (320, "magenta"), (345, "pink"), (360, "red")]
    name = next(label for limit, label in names if hue < limit)
    if l > 0.75 and name not in ("yellow", "lime green"):
        name = "light " + name
    return name


def place(centroid):
    x, y = centroid
    column = "left" if x < 1 / 3 else "right" if x > 2 / 3 else ""
    row = "upper" if y < 1 / 3 else "lower" if y > 2 / 3 else ""
    if not column and not row:
        return "in the centre"
    if not row:
        return f"on the {column}"
    if not column:
        return f"in the {row} middle"
    return f"in the {row} {column}"


def visible_subjects(manifest):
    subjects = [s for s in manifest.get("subjects", [])
                if s.get("visible") and s.get("coverage", 0) >= MIN_COVERAGE]
    subjects.sort(key=lambda s: -s.get("coverage", 0))
    return subjects[:MAX_SUBJECTS]


def write(manifest, look="flat", style=""):
    """The prompt for one frame, and nothing that changes with small moves."""
    subjects = visible_subjects(manifest)
    clay = look == "clay"
    lines = []
    if clay:
        lines.append("Turn this grey clay render into a photograph. Keep the camera, the layout "
                     "and every object's shape, size and place exactly; give each its real "
                     "materials, colours and lighting.")
    else:
        lines.append("Turn this colour-coded layout into a photograph. Keep the camera, the layout "
                     "and every object's shape, size and place exactly. The flat colours are only "
                     "labels, the black lines only outlines: give each object its real materials, "
                     "colours and lighting.")
    shot = manifest.get("shot", "")
    if shot:
        lines.append(shot.rstrip(".") + ".")
    if style:
        lines.append(style.rstrip(".") + ".")

    for subject in subjects:
        description = subject.get("description", "").strip()
        role = subject.get("role", "subject")
        where = place(subject.get("centroid", (0.5, 0.5)))
        if role == "ground" and not clay and subject.get("proxyColor") is None:
            label = "The grey checkerboard"
        elif clay:
            label = f"The {'ground' if role == 'ground' else 'shape'} {where}"
        else:
            label = f"The {colour_name(subject['proxyColor'])} shape {where}"
        if description:
            lines.append(f"{label} is {description.rstrip('.')}.")
        elif role != "ground":
            lines.append(f"{label} is an ordinary object that belongs in this scene.")

    if not clay:
        lines.append("The black area above everything is the sky.")
    lines.append("Nothing else: no extra objects, no text, no people unless described.")
    return " ".join(lines)
