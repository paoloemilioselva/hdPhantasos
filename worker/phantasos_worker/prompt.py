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
    # The hero (the subject the shot follows) first, then by size in frame.
    subjects.sort(key=lambda s: (s.get("role") != "hero", -s.get("coverage", 0)))
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


# ---------------------------------------------------------------------------
# Matrix-Game 4.0 racing captions
# ---------------------------------------------------------------------------
#
# The racing model was trained on captions its game writes for each chunk
# (game/racing/src/CaptionStream.cpp in their source), so it is given one in
# the same form:
#
#   Daytime city, cloudy sky. Center: player-controlled <the hero>, large in
#   view. Around: <thing> at left, <thing> at far top-right, ...
#
# Directions are their 9-way world-angle bins: left or right beyond 12 degrees
# off the camera's heading, top above 2 degrees of elevation, bottom below -6,
# and "far" from 60 units away. Sizes are their pixel thresholds, measured on
# a 1280x720 frame.

TRAINING_PIXELS = 1280 * 720
CAPTION_WORDS = 150  # their token budget, in words (they count 1.33 tokens a word)


def _direction(subject, camera, with_distance):
    import math
    eye, forward, up = camera.get("position"), camera.get("forward"), camera.get("up")
    position = subject.get("position")
    if not (eye and forward and up and position):
        x, y = subject.get("centroid", (0.5, 0.5))
        h = "left" if x < 1 / 3 else "right" if x > 2 / 3 else ""
        v = "top" if y < 1 / 3 else "bottom" if y > 2 / 3 else ""
        return (f"{v}-{h}" if v and h else v or h) or "center"

    def dot(a, b):
        return sum(p * q for p, q in zip(a, b))

    def minus(a, b):
        return [p - q for p, q in zip(a, b)]

    def scaled(a, k):
        return [p * k for p in a]

    def unit(a):
        n = math.sqrt(dot(a, a)) or 1.0
        return scaled(a, 1 / n)

    up = unit(up)
    # The heading, flattened onto the ground plane; the camera's up stands in
    # for the stage's, which Hydra does not pass.
    ahead = unit(minus(forward, scaled(up, dot(forward, up))))
    right = [ahead[1] * up[2] - ahead[2] * up[1], ahead[2] * up[0] - ahead[0] * up[2],
             ahead[0] * up[1] - ahead[1] * up[0]]
    d = minus(position, eye)
    height = dot(d, up)
    flat = math.hypot(dot(d, right), dot(d, ahead))
    horizontal = math.degrees(math.atan2(dot(d, right), dot(d, ahead)))
    elevation = math.degrees(math.atan2(height, flat))
    h = "left" if horizontal < -12 else "right" if horizontal > 12 else ""
    v = "top" if elevation > 2 else "bottom" if elevation < -6 else ""
    where = (f"{v}-{h}" if v and h else v or h) or "center"
    return ("far " if with_distance and flat >= 60 else "") + where


def _name(description):
    """A description as their catalog names things: no leading article."""
    text = description.strip().rstrip(".")
    for article in ("a ", "an ", "the "):
        if text.lower().startswith(article):
            return text[len(article):]
    return text


def racing_caption(manifest, style=""):
    """The caption Matrix-Game racing is given, from the first frame of a stream."""
    camera = manifest.get("camera", {})
    shot = manifest.get("shot", "").strip()
    # Their captions open with the time of day and the weather only.
    opening = shot.split(".")[0].strip() if shot else "Daytime city, cloudy sky"
    text = opening.rstrip(".") + ". "
    if style:
        text += style.rstrip(".") + ". "

    visible = [s for s in manifest.get("subjects", [])
               if s.get("visible") and s.get("role") != "ground" and s.get("description")]
    hero = next((s for s in visible if s.get("role") == "hero"), None)
    if hero:
        text += "Center: player-controlled " + _name(hero["description"])
        coverage = hero.get("coverage", 0)
        if coverage >= 0.06:
            text += ", dominating foreground"
        elif coverage >= 0.02:
            text += ", large in view"
        text += ". "

    important = [s for s in visible if s is not hero and s.get("coverage", 0) * TRAINING_PIXELS >= 800]
    important.sort(key=lambda s: -s.get("coverage", 0))
    selected = [s for s in important if s.get("coverage", 0) * TRAINING_PIXELS >= 1200]
    # Fill empty directions from smaller but genuinely visible things.
    covered = {_direction(s, camera, False) for s in selected}
    for s in important:
        if s not in selected and _direction(s, camera, False) not in covered:
            covered.add(_direction(s, camera, False))
            selected.append(s)
    selected.sort(key=lambda s: -s.get("coverage", 0))

    def compose(items):
        if not items:
            return text.rstrip()
        parts = [f"{_name(s['description'])} at {_direction(s, camera, True)}" for s in items]
        return text + "Around: " + ", ".join(parts) + "."

    caption = compose(selected)
    while selected and len(caption.split()) > CAPTION_WORDS:
        selected.pop()
        caption = compose(selected)
    return caption
