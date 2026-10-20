# hdPhantasos

An OpenUSD Hydra render delegate that turns simple, described prims into
generated pictures. A fast CPU rasterizer draws the stage as a flat-coloured
guide (the *proxy*), the prims' descriptions say what each thing is, and a
local PyTorch model paints the picture from both.

Phantasos was the god who sent dreams of things (objects, landscapes, water and
stone) rather than of people. Here he turns boxes into a world.

> **Status: phase 0.** The delegate, the proxy and the descriptions work, in
> usdview and usdrecord, on Windows; Linux has build scripts but has not been
> compiled yet. No model is connected yet: the colour AOV is the proxy itself.
> See [docs/roadmap.md](docs/roadmap.md).

| The example stage, as the model will see it | Pixar's Kitchen set, undescribed (1,788 meshes, 77 ms) |
|---|---|
| ![](docs/images/floor_two_boxes_proxy.png) | ![](docs/images/kitchen_set_proxy.png) |

## Describing a stage

Each prim says what it is through constant primvars, declared by the codeless
`PhantasosAPI` schema ([schema/](schema/)). Constant primvars are inherited
down the namespace, so describing a group describes every mesh under it:

```usda
def Xform "chest" (prepend apiSchemas = ["PhantasosAPI"])
{
    string primvars:phantasos:subject = "chest"
    string primvars:phantasos:description = "an old oak chest bound in green iron"
    bool   primvars:phantasos:hasFront = 1          # its +Z is its front
    def Cube "body" { ... }
    def Cube "lid"  { ... }
}
```

| Primvar | Meaning |
|---|---|
| `phantasos:subject` | Meshes with the same key are one subject: one description, one colour. Unset, each mesh is its own subject, keyed by its path |
| `phantasos:description` | What the subject is and looks like, in plain words |
| `phantasos:role` | `subject` (flat colour), `ground` (grey checkerboard), `backdrop` (flat colour, described last) |
| `phantasos:hasFront` | The prompt says which way the subject faces |
| `phantasos:reference` | An image of the subject, for the model as a reference picture |
| `phantasos:proxyColor` | The proxy colour, sRGB; unset, derived from the key, the same in every session |

The schema is optional: the delegate reads the primvars, which reach it with or
without the schema loaded. With it, the properties are typed, documented, and
shown as such in a host's UI.

Every prim type the stage is built from works: `Mesh`, and `Cube`, `Sphere`,
`Cylinder`, `Cone`, `Capsule` and `Plane`, which a scene index plugin turns
into meshes. Point and native instancing work. There are no materials or
lights: the proxy is flat colour by design.

## What the delegate produces

- **AOVs:** `color` (the proxy, linear), `depth`, `normal`, `primId`,
  `instanceId`, `elementId`, and `subjectId` (which subject each pixel shows).
- **A manifest** for each frame when `HDPHANTASOS_MANIFEST` names a
  directory: `manifest.json` (every subject with its description and role, and
  what the frame shows of it: coverage, box, centroid, distance, and which way
  it faces) and `proxy.png` (the guide, 8-bit sRGB). This is what the model
  worker will read.

The proxy follows the training proxies of Matrix-Game 4.0: flat colour per
subject, black outlines between primitives and along creases, a grey checker
ground and a black sky.

## Building and running

```bat
compile.bat              :: build and install into USDEXTRA (Desktop\usd-26.03-extra)
compile.bat test         :: build, install, then run every test
compile.bat core-only    :: the rasterizer and its tests, without OpenUSD

launch_phantasos.bat [stage.usda]                          :: usdview, default: examples\floor_two_boxes.usda
render_phantasos.bat --camera /World/camera stage.usda out.png
```

On Linux: `python3 compile.py`, `./launch_phantasos.sh`, `./render_phantasos.sh`.
See [docs/building.md](docs/building.md).

Settings, as render settings in usdview or environment variables for usdrecord:

| Setting | Variable | Default |
|---|---|---|
| Outline width, pixels at 480 rows (0: none) | `HDPHANTASOS_OUTLINE_WIDTH` | 1 |
| Shade the proxy (darken faces turned away) | `HDPHANTASOS_PROXY_SHADING` | off |
| Ground checker size, world units | `HDPHANTASOS_CHECKER_SIZE` | 1 |

`HDPHANTASOS_TRACE=1` prints what is synced and drawn, with timings.

## Design

[docs/architecture.md](docs/architecture.md) records the decisions: why a CPU
rasterizer, why primvars, and why the model will run in its own process.
