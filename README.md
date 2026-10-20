# hdPhantasos

An OpenUSD Hydra render delegate that turns simple, described prims into
generated pictures. A fast CPU rasterizer draws the stage as a flat-coloured
guide (the *proxy*), the prims' descriptions say what each thing is, and a
local PyTorch model paints the picture from both.

Phantasos was the god who sent dreams of things (objects, landscapes, water and
stone) rather than of people. Here he turns boxes into a world.

> **Status: phase 1.** The delegate, the proxy and the descriptions work in
> usdview and usdrecord, and generation with FLUX.2 Klein 4B works through
> usdrecord, on Windows. Generation in the usdview viewport and the Linux
> build are not tested yet. See [docs/roadmap.md](docs/roadmap.md).

| The example stage, as the model will see it | Pixar's Kitchen set, undescribed (1,788 meshes, 77 ms) |
|---|---|
| ![](docs/images/floor_two_boxes_proxy.png) | ![](docs/images/kitchen_set_proxy.png) |

## Examples

| Stage | What it is |
|---|---|
| [examples/floor_two_boxes.usda](examples/floor_two_boxes.usda) | A floor, a chest made of two cubes, and a crate: the smallest described stage |
| [examples/city_drive.usda](examples/city_drive.usda) | A city street driven from behind a sedan, laid out like Matrix-Game 4.0's City Racing scene: 35 described subjects (20 buildings, parked cars, pedestrians, lamps, trees, road, lane markings), 120 frames at 24 fps with a chase camera. Written by [examples/make_city_drive.py](examples/make_city_drive.py): edit that, not the .usda |

![The city drive at frame 60](docs/images/city_drive_proxy.png)

All 120 frames of the city drive render in 5.4 s through usdrecord, start-up included.

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

## Generating pictures

Turn on **Generate pictures** (or `HDPHANTASOS_GENERATE=1`) and the delegate
hands each view to a model worker: the proxy goes in, a picture comes back and
replaces the proxy in the colour AOV. The proxy shows at once while the picture
is made; a newer view replaces a request still waiting, so orbiting never
builds a queue. usdrecord waits for each frame's picture.

The first model is **FLUX.2 [klein] 4B** (Apache-2.0, distilled to four
steps), with the proxy as its reference image and a prompt written from the
manifest: each subject's description, named by its proxy colour and place
("the pink shape in the lower middle is a modern four-door sedan"), after the
stage's `phantasos:shot`. On the RTX 5060 Ti: 1.4 s a picture at 512x288,
3.0 s at 768x432, 12.9 GB of VRAM, 15 s to load.

**Use the clay look as Klein's guide**: in a test on the city drive it gave the
cleanest photographs, where flat colours sometimes leak into the picture as
painted walls.

The worker is a separate process with its own Python ([worker/](worker/)),
kept in a cache (`%LOCALAPPDATA%\hdPhantasos`, `~/.cache/hdPhantasos`, or
`HDPHANTASOS_CACHE`). Nothing is downloaded unless **Allow downloads** is on:
then the first use makes the venv with [uv](https://docs.astral.sh/uv/)
(PyTorch for the machine's driver, about 4 GB) and fetches the model (about
15 GB). Its log is `<cache>/logs/worker.log`.

| Setting | Variable | Default |
|---|---|---|
| Generate pictures | `HDPHANTASOS_GENERATE` | off |
| Model: `klein4b`, or `echo` (no model: a grey copy, for tests) | `HDPHANTASOS_MODEL` | klein4b |
| Picture size, long side in pixels | `HDPHANTASOS_GENERATE_SIZE` | 512 |
| Seed | `HDPHANTASOS_SEED` | 0 |
| Style, added to the prompt | `HDPHANTASOS_STYLE` | |
| Allow downloads | `HDPHANTASOS_ALLOW_DOWNLOAD` | off |

With `HDPHANTASOS_MANIFEST` set, each frame also writes `generated.png` and
`prompt.txt` beside the manifest.

## Building and running

```bat
compile.bat              :: build and install into USDEXTRA (Desktop\usd-26.03-extra)
compile.bat test         :: build, install, then run every test
compile.bat core-only    :: the rasterizer and its tests, without OpenUSD

launch_phantasos.bat [stage.usda]                          :: usdview, default: examples\city_drive.usda
render_phantasos.bat --camera /World/camera stage.usda out.png
```

On Linux: `python3 compile.py`, `./launch_phantasos.sh`, `./render_phantasos.sh`.
See [docs/building.md](docs/building.md).

Settings, as render settings in usdview or environment variables for usdrecord:

| Setting | Variable | Default |
|---|---|---|
| Outline width, pixels at 480 rows (0: none) | `HDPHANTASOS_OUTLINE_WIDTH` | 1 |
| Shade the proxy: the same colours, lit by one light fixed to the camera (above left), so each face of a box has its own tone | `HDPHANTASOS_PROXY_SHADING` | off |
| Clay: one grey material on everything, ground included, under the same light (wins over shading) | `HDPHANTASOS_PROXY_CLAY` | off |
| Ground checker size, world units | `HDPHANTASOS_CHECKER_SIZE` | 1 |

`HDPHANTASOS_TRACE=1` prints what is synced and drawn, with timings.

## Design

[docs/architecture.md](docs/architecture.md) records the decisions: why a CPU
rasterizer, why primvars, and why the model will run in its own process.
