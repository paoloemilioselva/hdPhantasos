# Architecture

Last revised 2026-10-20 (phase 0).

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

### 2.5 The model will run in its own process

Not inside the delegate, because:

- every host brings its own Python and DLLs (usdview, usdrecord, later
  Houdini), and PyTorch, CUDA and OpenUSD in one process clash;
- a model crash or an out-of-memory must not take the host down;
- the same worker can serve other tools (Frameforge, viewToPrompt).

The delegate starts the worker, exchanges frames through shared memory (a
864x480 proxy is 1.2 MB), and the worker creates its venv with `uv`, picks the
PyTorch build for the driver, and downloads weights at pinned revisions with
SHA256 checks, asking before any large download. Worker and delegate always
run on the same machine.

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
