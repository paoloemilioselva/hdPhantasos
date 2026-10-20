# Roadmap

| Phase | Result | State |
|---|---|---|
| **0** | The delegate in usdview and usdrecord: CPU rasterizer, proxy, AOVs, the PhantasosAPI schema, the per-frame manifest, an example stage, tests | **Done on Windows, 2026-10-20; builds and works on Linux, 2026-10-21** |
| **1** | The worker: its own process and venv, self-setup (uv, PyTorch for the driver, weights only with downloads allowed), shared-memory frames, and the first model: FLUX.2 Klein 4B, a still from the proxy and a prompt written from the manifest | **Working through usdrecord on Windows, 2026-10-20.** usdview viewport not tried yet; prompt still leaks flat colours in the flat look |
| **1b** | Matrix-Game 4.0 racing as a streaming back end: frames in order, a session per shot | **Working through usdrecord on Windows, 2026-10-20**: about 20 frames a second on the RTX 5060 Ti. Streams in the usdview viewport |
| **2** | Sequences: consistent clips from a camera move or an animated stage, offline through usdrecord (Wan2.2-Fun-Control), and Matrix-Game 4.0's racing checkpoint as a real-time streaming experiment | |
| **3** | Houdini Solaris: a build against the HDK | Later, not planned yet |

## Decisions (with dates)

- 2026-10-20: not a game. The input is a USD stage of described prims.
- 2026-10-20: a fast CPU rasterizer, to leave the GPU to the model.
- 2026-10-20: descriptions are primvars, declared by a codeless API schema.
- 2026-10-20: the worker always runs on the same machine as the delegate.
- 2026-10-20: no Houdini support for now.
- 2026-10-20: phase 1, Klein first, then Matrix-Game racing streaming.
- 2026-10-20: a stream's first frame defaults to the model's own (`model`);
  Klein's (`klein`) looks better for a second or two, then drifts.
- 2026-10-20: MIT licence. Model weights are never in the repository; each
  keeps its own licence, listed when it is added.

## Open questions

- A stream's caption is fixed at its first frame (their fixed mode). Their
  dynamic mode re-captions every chunk, but needs the 11 GB text encoder
  resident beside the model: not on a 16 GB card.
- usdrecord feeds a stream each frame about four times; a proper offline
  sequence (all proxies first, then the stream) belongs to phase 2.
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
