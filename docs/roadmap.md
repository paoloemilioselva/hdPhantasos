# Roadmap

| Phase | Result | State |
|---|---|---|
| **0** | The delegate in usdview and usdrecord: CPU rasterizer, proxy, AOVs, the PhantasosAPI schema, the per-frame manifest, an example stage, tests | **Done on Windows, 2026-10-20.** Linux not compiled yet |
| **1** | The worker: its own process and venv, self-setup (uv, PyTorch for the driver, pinned weights, asks before large downloads), shared-memory frames, and the first model: a still from the proxy and a prompt written from the manifest | Next |
| **2** | Sequences: consistent clips from a camera move or an animated stage, offline through usdrecord (Wan2.2-Fun-Control), and Matrix-Game 4.0's racing checkpoint as a real-time streaming experiment | |
| **3** | Houdini Solaris: a build against the HDK | Later, not planned yet |

## Decisions (with dates)

- 2026-10-20: not a game. The input is a USD stage of described prims.
- 2026-10-20: a fast CPU rasterizer, to leave the GPU to the model.
- 2026-10-20: descriptions are primvars, declared by a codeless API schema.
- 2026-10-20: the worker always runs on the same machine as the delegate.
- 2026-10-20: no Houdini support for now.
- 2026-10-20: MIT licence. Model weights are never in the repository; each
  keeps its own licence, listed when it is added.

## Open questions

- Phase 1's first model: a still (fastest to show descriptions working) or
  straight to video.
- Whole-shot settings (style, atmosphere, time of day): on a `RenderSettings`
  prim, or as render settings only.
- Whether a stage may carry its own authored proxy look beyond
  `proxyColor`.

## Known limits of phase 0

- Skinned meshes (ExtComputation points) are not supported.
- Orthographic cameras are untested; the manifest's distances also
  assume a perspective eye point.
- A subject's facing comes from the first placed mesh's +Z, so a group's
  parts are expected to share the group's orientation.
