"""Render the test stages through usdrecord with the installed delegate, and
check what reached it: the subjects and their descriptions (manifest.json) and
the proxy look (proxy.png and usdrecord's own image).

Needs the OpenUSD environment (setup_usd_env.bat/.sh) and an installed plugin,
which `python compile.py test` provides. Exits 77, which CTest reports as a
skip, when usdrecord is not on PATH.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from collections import deque

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SKIP = 77

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAIL", message)


def find_usdrecord():
    # The extensionless Python script, run with this interpreter: on Windows
    # the .cmd wrapper beside it cannot be started without a shell.
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        candidate = os.path.join(directory, "usdrecord")
        if os.path.isfile(candidate):
            return candidate
    return None


def render(usdrecord, stage, output_dir, width=864, settings=None):
    os.makedirs(output_dir, exist_ok=True)
    environment = dict(os.environ, HDPHANTASOS_MANIFEST=output_dir, **(settings or {}))
    image = os.path.join(output_dir, "usdrecord.png")
    command = [sys.executable, usdrecord, "--renderer", "Phantasos",
               "--camera", "/World/camera", "--imageWidth", str(width),
               "--colorCorrectionMode", "sRGB", stage, image]
    result = subprocess.run(command, env=environment, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout)
        print(result.stderr)
    check(result.returncode == 0, f"usdrecord failed on {os.path.basename(stage)}")
    manifest_path = os.path.join(output_dir, "manifest.json")
    check(os.path.isfile(manifest_path), "no manifest.json written")
    if not os.path.isfile(manifest_path):
        return None, None, None
    with open(manifest_path, encoding="utf-8") as handle:
        manifest = json.load(handle)
    from PIL import Image
    proxy = Image.open(os.path.join(output_dir, "proxy.png")).convert("RGB")
    recorded = Image.open(image).convert("RGB") if os.path.isfile(image) else None
    return manifest, proxy, recorded


def by_key(manifest):
    return {subject["key"]: subject for subject in manifest["subjects"]}


def colour_of(subject):
    return tuple(round(c * 255) for c in subject["proxyColor"])


def components(image, colour, tolerance=2):
    """Connected regions of one colour: how many separate things show it."""
    width, height = image.size
    pixels = image.load()
    seen = set()
    regions = []
    match = lambda p: all(abs(a - b) <= tolerance for a, b in zip(p, colour))
    for y in range(height):
        for x in range(width):
            if (x, y) in seen or not match(pixels[x, y]):
                continue
            size = 0
            queue = deque([(x, y)])
            seen.add((x, y))
            while queue:
                cx, cy = queue.popleft()
                size += 1
                for nx, ny in ((cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)):
                    if 0 <= nx < width and 0 <= ny < height and (nx, ny) not in seen \
                            and match(pixels[nx, ny]):
                        seen.add((nx, ny))
                        queue.append((nx, ny))
            regions.append(size)
    return [size for size in regions if size >= 20]


def test_floor_two_boxes(usdrecord, output):
    stage = os.path.join(ROOT, "examples", "floor_two_boxes.usda")
    manifest, proxy, recorded = render(usdrecord, stage, os.path.join(output, "floor_two_boxes"))
    if manifest is None:
        return
    subjects = by_key(manifest)
    check(set(subjects) == {"floor", "chest", "crate"},
          f"subjects are {sorted(subjects)}, expected floor, chest, crate")
    if set(subjects) != {"floor", "chest", "crate"}:
        return

    check(subjects["floor"]["role"] == "ground", "the floor is not ground")
    check(subjects["chest"]["description"].startswith("an old oak chest"),
          "the chest's description, authored on its group, did not reach its cubes")
    check(subjects["chest"].get("facing") == "toward the camera",
          f"chest facing {subjects['chest'].get('facing')!r}")
    for key in ("floor", "chest", "crate"):
        check(subjects[key]["visible"], f"{key} not visible")
    chest, crate = subjects["chest"], subjects["crate"]
    check(chest["centroid"][0] < 0.5 < crate["centroid"][0],
          "the chest should be left of the crate in frame")

    check(proxy.size == (manifest["width"], manifest["height"]), "proxy size differs from manifest")
    check(proxy.size[0] == 864, f"proxy width {proxy.size[0]}")

    # The chest is one colour, its two cubes split by an outline at most.
    check(len(components(proxy, colour_of(chest))) >= 1, "no chest-coloured pixels")
    colours = proxy.getcolors(maxcolors=1 << 20)
    greys = {c for n, c in colours if c[0] == c[1] == c[2] and c[0] > 60 and n > 200}
    check(len(greys) == 2, f"expected the two checker greys, found {sorted(greys)}")
    check(any(c == (0, 0, 0) for n, c in colours), "no black (sky and outlines)")

    # usdrecord's own image is the same picture, through its colour pipeline.
    if recorded is not None:
        check(recorded.size == proxy.size, "usdrecord image size differs from the proxy")
        if recorded.size == proxy.size:
            a, b = recorded.load(), proxy.load()
            total = 0
            for y in range(0, proxy.size[1], 3):
                for x in range(0, proxy.size[0], 3):
                    total += sum(abs(p - q) for p, q in zip(a[x, y], b[x, y]))
            samples = (proxy.size[0] // 3 + 1) * (proxy.size[1] // 3 + 1) * 3
            check(total / samples < 3.0,
                  f"usdrecord image differs from the proxy by {total / samples:.1f} levels")


def test_implicit_and_instances(usdrecord, output):
    stage = os.path.join(HERE, "implicit_and_instances.usda")
    # Without outlines, so each bollard is one patch of its colour rather than
    # one per face; this also checks a setting reaches the delegate from the
    # environment.
    manifest, proxy, _ = render(usdrecord, stage, os.path.join(output, "implicit_and_instances"),
                                settings={"HDPHANTASOS_OUTLINE_WIDTH": "0"})
    if manifest is None:
        return
    subjects = by_key(manifest)
    keys = set(subjects)
    for key in ("lamp", "pillars", "/World/ball", "/World/ground", "/World/undescribed"):
        check(key in keys, f"missing subject {key}; have {sorted(keys)}")
    check(not any("never be seen" in s["description"] for s in manifest["subjects"]
                  if s["visible"]), "an invisible prim was drawn")
    if "/World/ball" in subjects:
        check(subjects["/World/ball"]["visible"], "the sphere was not drawn")
        check(subjects["/World/ball"]["description"] == "a red leather ball",
              "the sphere lost its description")
    if "/World/undescribed" in subjects:
        check(subjects["/World/undescribed"]["description"] == "",
              "an undescribed prim has a description")
    if "pillars" in subjects:
        regions = components(proxy, colour_of(subjects["pillars"]))
        check(len(regions) == 5, f"expected five instanced bollards, found {len(regions)}")
    # The sky is black; with outlines off, nothing below the horizon is.
    pixels = proxy.load()
    w, h = proxy.size
    black_below = sum(1 for y in range(h * 3 // 4, h) for x in range(w)
                      if pixels[x, y] == (0, 0, 0))
    check(black_below == 0, f"{black_below} black pixels on the ground with outlines off")
    if "lamp" in subjects:
        check(subjects["lamp"]["visible"], "the lamp (cylinder, cone, capsule) was not drawn")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    usdrecord = find_usdrecord()
    if usdrecord is None:
        print("usdrecord is not on PATH; source setup_usd_env first. Skipping.")
        return SKIP
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("Pillow is not installed in this Python. Skipping.")
        return SKIP

    shutil.rmtree(args.output, ignore_errors=True)
    test_floor_two_boxes(usdrecord, args.output)
    test_implicit_and_instances(usdrecord, args.output)
    print(f"stage: {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
