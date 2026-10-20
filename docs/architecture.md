# Architecture

Last revised 2026-10-20 (phase 1b).

## 1. What it is

A Hydra render delegate with two halves:

1. **The delegate (C++, this repository's `src/`).** Reads the stage through
   Hydra, rasterizes it on the CPU into a *proxy* (a flat-coloured guide
   image) plus ID, depth and normal passes, and collects what each prim is
   (its *description*) into a per-frame *manifest*.
2. **The worker (Python, phase 1).** A separate process with its own venv
   and PyTorch. It turns the proxy and the manifest into a prompt and a
   picture, and hands the picture back as the `color` AOV.

Phase 0 is the first half: the colour AOV is the proxy itself.

## 2. Decisions

### 2.1 A CPU rasterizer, not a path tracer

The model needs a guide, not a render: where each thing is, how big, which
way it faces. One sample a pixel of flat colour gives that. A CPU rasterizer
(`src/core/raster.cpp`) does it in milliseconds and leaves the whole GPU to
the model, which is what limits the scene size, not the drawing.

- Tiled by horizontal bands of 16 rows. Each band belongs to one thread, so
  no pixel is ever written by two threads and nothing needs atomics.
- Edge functions in double precision. After near-plane clipping a vertex can
  be thousands of pixels off screen, where float leaves cracks.
- Ties in depth go to the lower triangle record, so the picture does not
  depend on thread timing (tested).
- Position and normal are not read back from the depth buffer: each pixel's
  ray is intersected with its triangle's plane, exact at any distance.
- Measured on the Windows workstation (RTX 5060 Ti box): Kitchen set 537k triangles in 77 ms, Intel Sponza
  3.7M in 341 ms, both at 1280x934. Threads are made per frame (about 15 ms
  of a tiny frame's cost); a pool is an easy win if it ever matters.

### 2.2 The proxy look follows Matrix-Game 4.0

Its training proxies are flat colour per object, black outlines on every
primitive, a grey checker ground and a black sky. A model trained on those
(their checkpoints, or a fine-tune of ours later) needs exactly that input,
and the look is also a clear guide for an image model.

- Colours are derived from the subject key (FNV-1a to a saturated hue), so a
  subject keeps its colour across frames and sessions. Never grey or dark,
  so never mistaken for ground, sky or outline.
- Outlines are drawn into the nearer of two differing pixels (so a line lies
  on the object in front), between primitives and along creases sharper
  than 60 degrees. Width scales with image height.
- Two other looks, for models that read form rather than identity: *shaded*
  keeps the colours under one light fixed to the camera (above left, a
  little in front), so a box always shows three tones; *clay* is one grey
  material on everything under the same light plus a soft sky term. The
  light is fixed to the camera rather than the world so the same view of the
  same thing always reads the same.
- The ground checker lies in the plane the surface mostly faces, so Y-up and
  Z-up stages both work without reading the stage's up axis, which Hydra does
  not pass.

### 2.3 Descriptions travel as primvars; the schema only declares them

Primvars reach a render delegate in every Hydra host, with or without a
schema plugin loaded. A custom attribute does not: it would need a scene
index plugin to carry it, and that plugin would have to load in every host.
The codeless `PhantasosAPI` schema adds types, fallbacks, documentation and UI
on top of the same primvars, and nothing breaks when it is absent.

Constant primvars inherit down the namespace, so a group's description
reaches every part, and parts with the same `phantasos:subject` key become one
subject (one colour, one description). Tested on the chest in the example and
on a point instancer's prototypes.

### 2.4 Render tags are read from the render index at draw time

Hydra updates an rprim's render tag without syncing it, and the scene
delegate's `GetRenderTag` returned an empty tag during Sync in USD 26.03. The
render pass asks `HdRenderIndex::GetRenderTag` for each mesh instead.

### 2.5 The model runs in its own process

Not inside the delegate, because:

- every host brings its own Python and DLLs (usdview, usdrecord, later
  Houdini), and PyTorch, CUDA and OpenUSD in one process clash;
- a model crash or an out-of-memory must not take the host down;
- the same worker can serve other tools (Frameforge, viewToPrompt).

How it works (`src/hydra/worker_link.*`, `worker/`):

- The delegate starts `worker/run_worker.py` with the venv's Python on the
  first frame that asks for a picture, and keeps it running. Messages are
  JSON lines on the worker's stdin and stdout; its stderr goes to
  `<cache>/logs/worker.log`.
- Pixels go through a memory-mapped file in `<cache>/run`: the proxy in, the
  picture out, 8-bit RGB, up to 4K each. `worker/phantasos_worker/protocol.py`
  has the layout.
- One thread in the delegate does all the process I/O. Requests are newest
  wins: one frame with the worker, and a newer one replaces a request still
  waiting.
- The render pass shows the proxy at once and stays unconverged, so the host
  keeps calling, until the picture for that exact framing is back. A picture
  that arrives for an older framing is dropped.
- No download happens unless 'Allow downloads' is on. Then a missing venv is
  made by `uv run setup_worker.py` (Python 3.12, PyTorch for the driver via
  uv's `--torch-backend auto`, the model libraries), and the model weights
  are fetched by the worker into `<cache>/hf`.
- The worker code is the copy installed beside the plugin, put on `sys.path`
  by `run_worker.py`; the venv holds only dependencies, so a rebuild of the
  plugin updates the worker without a reinstall.

Worker and delegate always run on the same machine.

### 2.6 Klein: the proxy as reference image, the descriptions by colour

FLUX.2 Klein 4B takes reference images and edits from them. The proxy is its
reference; the prompt names each subject by its proxy colour and its place on
a 3x3 grid ("the pink shape in the lower middle is a modern four-door
sedan"). The grid is coarse on purpose: the prompt then changes only when the
picture's content does, and its text-encoder pass (Qwen3-4B, int8 weights) is
cached by text.

Measured on the city drive (one frame, three looks): clay gives the most
photographic result; flat and shaded leak flat colours into the picture
(painted walls, a pink block for a car).

### 2.7 Matrix-Game 4.0: a stream, one frame in for one frame out

The racing checkpoint is a causal video model: chunks of four frames, each
painted from its proxy frames and the last 21 latent frames it painted. The
delegate renders one frame at a time, so the worker keeps a stream
(`worker/phantasos_worker/matrix_game.py`):

- Each request's proxy joins the next chunk; when four are in, the chunk is
  painted. Each answer is the oldest painted frame not yet shown, with the
  id of the request whose proxy it shows (`source`). The stream runs three
  frames behind, and the answer rate follows the requests.
- The render pass shows every streamed picture, keeps the last one on screen
  while the camera moves (not the proxy), and while the newest shows an
  earlier view than the current one, sends the current proxy again until the
  stream catches up; then it converges. In usdview the stream follows the
  camera; usdrecord gets each frame's own picture, at the cost of feeding
  each frame about four times.
- A stream starts at the first frame and lasts until a setting that shapes
  it (seed, first frame, style, the stage's shot) changes. Its caption is
  written once, from the first frame, in the form their game writes them;
  `phantasos:role = "hero"` marks the subject their captions put in the
  centre, and the manifest's per-subject world position and the camera's
  up vector give their 9-way direction bins.
- Their RTX 5070 Ti runtime (int8 block linears, CUDA graphs) runs on the
  RTX 5060 Ti at 51 ms a frame in 11.7 GB. It holds one caption per load, so
  a new caption reloads the transformer (about 8 s, plus 8 s of warm-up).
  Before Blackwell (the A4500 is Ampere) the one MXFP8 layer becomes int8.
- Their T5 wrapper builds the 5.7B-parameter encoder in fp32 on the CPU
  (23 GB) before loading it; the worker builds it empty and assigns the bf16
  checkpoint, bit-identical in 12 s instead of 76. Encoded captions are
  cached in `<cache>/captions`.
- Their code is fetched by git at a pinned commit (a sparse checkout of the
  inference package, 1.8 MB) into `<cache>/src`; the weights at the release
  revisions through the Hugging Face cache.

What it paints, on the city drive: it follows the camera, the road, the ego
car, parked cars and trees, but it paints its own city (it was trained on
one game), and in the flat look the lime-green sidewalks become green
barriers. A Klein first frame gives a photograph of our street for a second
or two, then drifts and smears.

## 3. Layout

```
include/hdphantasos/core/raster.h   the core API: no OpenUSD
src/core/raster.cpp                  rasterizer, proxy look, manifest
src/hydra/                           the delegate: plugin, delegate, mesh,
                                     instancer, render pass, buffers, the
                                     implicit-surface scene index plugin
schema/                              the codeless PhantasosAPI schema
examples/floor_two_boxes.usda        a floor and two described boxes
tests/core_tests.cpp                 the core, without OpenUSD
tests/usd/stage_test.py              usdrecord renders, checked through the manifest
```

## 4. Conventions

OpenUSD's, so nothing is converted at the seam: row-major matrices on row
vectors, OpenGL clip space, image rows bottom-up in AOVs. The manifest and
`proxy.png` are top-left, as a person describes a picture.
