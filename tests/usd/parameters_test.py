"""The usdview editor's model (usdview/phantasosUsdview/parameters.py) on the
example stage, and an edit reaching the delegate live: the same recorder
renders before and after a description changes, as usdview's viewport does.

    python tests/usd/parameters_test.py --output <dir>
"""

import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SKIP = 77
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print("FAIL:", message)


def test_model(params):
    from pxr import Usd
    every = {p.name: p for p in params.parameters()}
    check(set(params.ORDER) <= set(every), f"the schema lacks {set(params.ORDER) - set(every)}")
    if not set(params.ORDER) <= set(every):
        return
    check([p.name for p in params.parameters()][:len(params.ORDER)] == params.ORDER,
          "the parameters are not in editing order")
    role = every["phantasos:role"]
    check(role.allowed == ["subject", "hero", "ground", "backdrop"], f"role allows {role.allowed}")
    check("hero:" in role.doc and len(role.doc) > 200, "role's documentation is not the full text")
    check(role.display_name == "Role" and every["phantasos:proxyColor"].group == "Proxy",
          "display names or groups are missing")
    kinds = {name: p.kind for name, p in every.items()}
    check(kinds == {"phantasos:subject": "string", "phantasos:description": "text", "phantasos:role": "token",
                    "phantasos:hasFront": "bool", "phantasos:reference": "asset",
                    "phantasos:proxyColor": "color", "phantasos:shot": "text"}, f"kinds: {kinds}")

    stage = Usd.Stage.Open(os.path.join(ROOT, "examples", "floor_two_boxes.usda"))
    stage.SetEditTarget(stage.GetSessionLayer())
    body, lid = stage.GetPrimAtPath("/World/chest/body"), stage.GetPrimAtPath("/World/chest/lid")
    description = every["phantasos:description"]

    reading = params.read([body], description)
    check(reading.origin == "inherited" and str(reading.source) == "/World/chest" and
          "oak chest" in reading.value, "the body does not inherit the chest's description")

    params.set_value([body, lid], description, "a steel strongbox")
    reading = params.read([body, lid], description)
    check(reading.origin == "here" and reading.authored == 2 and reading.value == "a steel strongbox",
          "setting on two prims did not author both")
    check(params.has_schema(body), "setting did not apply PhantasosAPI")
    check(not stage.GetRootLayer().dirty, "an edit in the session layer touched the root layer")

    params.set_value([lid], description, "a brass lid")
    reading = params.read([body, lid], description)
    check(reading.mixed and reading.value is None, "two values do not read as mixed")

    left = params.remove([body, lid], description)
    reading = params.read([body, lid], description)
    check(not left and reading.origin == "inherited", "removing did not restore the inherited value")

    colour = every["phantasos:proxyColor"]
    params.set_value([body], colour, (1.0, 0.5, 0.0))
    check(tuple(params.read([body], colour).value) == (1.0, 0.5, 0.0), "colour did not round-trip")
    reference = every["phantasos:reference"]
    params.set_value([body], reference, "textures/oak.png")
    check(params.read([body], reference).value.path == "textures/oak.png", "asset did not round-trip")

    left = params.remove_all([body], list(every.values()))
    check(not left and not params.has_schema(body), "remove all left parameters or the schema")
    check(params.read([body], colour).origin == "fallback", "the colour is still set after remove all")

    # The chest's own description lives in the root layer: removing it from
    # the session layer cannot, and says so.
    chest = stage.GetPrimAtPath("/World/chest")
    left = params.remove([chest], description)
    check([p.GetPath() for p in left] == [chest.GetPath()],
          "a value in a weaker layer was not reported as left")


def test_live_edit(params, output):
    """The delegate sees an edit on a stage it is already drawing."""
    from pxr import Usd, UsdAppUtils, UsdGeom
    out = os.path.join(output, "live_edit")
    os.makedirs(out, exist_ok=True)
    os.environ["HDPHANTASOS_MANIFEST"] = out
    stage = Usd.Stage.Open(os.path.join(ROOT, "examples", "floor_two_boxes.usda"))
    stage.SetEditTarget(stage.GetSessionLayer())
    camera = UsdGeom.Camera(stage.GetPrimAtPath("/World/camera"))
    recorder = UsdAppUtils.FrameRecorder("HdPhantasosRendererPlugin", False, False)
    recorder.SetImageWidth(320)

    def describe():
        recorder.Record(stage, camera, Usd.TimeCode.Default(), os.path.join(out, "frame.png"))
        with open(os.path.join(out, "manifest.json"), encoding="utf-8") as handle:
            return {s["key"]: s for s in json.load(handle)["subjects"]}

    before = describe()
    check("oak chest" in before.get("chest", {}).get("description", ""), "no chest before the edit")
    every = {p.name: p for p in params.parameters()}
    params.set_value([stage.GetPrimAtPath("/World/chest")], every["phantasos:description"], "a steel strongbox")
    params.set_value([stage.GetPrimAtPath("/World/crate")], every["phantasos:role"], "hero")
    after = describe()
    check(after.get("chest", {}).get("description") == "a steel strongbox",
          f"the delegate did not see the new description: {after.get('chest', {}).get('description')!r}")
    check(after.get("crate", {}).get("role") == "hero", "the delegate did not see the new role")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        from pxr import Usd  # noqa: F401
    except ImportError:
        print("OpenUSD's Python is not on PYTHONPATH; source setup_usd_env first. Skipping.")
        return SKIP
    sys.path.insert(0, os.path.join(ROOT, "usdview", "phantasosUsdview"))
    import parameters as params
    if not params.parameters():
        print("PhantasosAPI is not registered (phantasosSchema not installed). Skipping.")
        return SKIP
    test_model(params)
    test_live_edit(params, args.output)
    print(f"parameters: {len(failures)} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
