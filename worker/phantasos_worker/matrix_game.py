"""Matrix-Game 4.0 racing (SkyworkAI, Apache-2.0) as a streaming model.

A causal Wan2.2-5B video model, distilled to one step, that paints a
low-poly guide video four frames at a time and remembers the last 21 latent
frames (84 pictures), so a street it has painted stays the same street. It
was trained on one city-driving game, so it paints that kind of world.

It streams: each frame the delegate sends is taken into the next chunk, and
each answer is the oldest painted frame not yet shown. The picture shown is
therefore a few frames behind the proxy (one chunk), and the answer says
which request's proxy it shows ("source"), so the delegate knows when the
stream has caught up with a camera that stopped.

The first frame of a stream is its anchor (the racing model's "sink"): kept
in memory for the whole stream and shown as frame zero. Choices:

    model    the model paints it from the proxy, like any other frame
    klein    FLUX.2 Klein 4B paints it from the proxy first (needs Klein's
             weights; the two models do not fit a 16 GB card together, so
             Matrix-Game is loaded after Klein is done)
    release  the image released with the checkpoint: a frame of their game,
             only meaningful for their own city

Its code is not on PyPI: the inference package is fetched from GitHub at a
pinned commit (git needed) into <cache>/src. Weights come through the
Hugging Face cache, pinned to the release revisions:

    Skywork/Matrix-Game-4.0           driving_game/model.safetensors   10.0 GB
                                      base/taew2_2.pth (TAEHV codec)    23 MB
    alibaba-pai/Wan2.2-Fun-5B-Control models_t5_umt5-xxl-enc-bf16.pth  11.4 GB
                                      google/umt5-xxl tokenizer         21 MB

The runtime is theirs for the RTX 5070 Ti (16 GB): block linears in int8,
CUDA graphs, one fixed caption per loaded model. A new caption reloads the
model (cached captions do not need the text encoder again).
"""

import gc
import os
import shutil
import subprocess
import sys
import time
from collections import deque

from . import prompt as prompts

SOURCE_REPOSITORY = "https://github.com/SkyworkAI/Matrix-Game.git"
SOURCE_COMMIT = "c7a88bfc039cd97695082484986e95b962390676"
RELEASE = ("Skywork/Matrix-Game-4.0", "2c53100457da7abcf0e966f75637c644d3423a63")
BASE = ("alibaba-pai/Wan2.2-Fun-5B-Control", "b8bc1a65ab71d054ba4636dc0dac104aa4df2686")
WEIGHTS = {
    "checkpoint": (RELEASE, "driving_game/model.safetensors"),
    "sink": (RELEASE, "driving_game/sink.png"),
    "taehv": (RELEASE, "base/taew2_2.pth"),
    "t5": (BASE, "models_t5_umt5-xxl-enc-bf16.pth"),
    "tokenizer_map": (BASE, "google/umt5-xxl/special_tokens_map.json"),
    "tokenizer_model": (BASE, "google/umt5-xxl/spiece.model"),
    "tokenizer": (BASE, "google/umt5-xxl/tokenizer.json"),
    "tokenizer_config": (BASE, "google/umt5-xxl/tokenizer_config.json"),
}
WIDTH, HEIGHT = 864, 480
CHUNK = 4  # frames per chunk after the first
MAX_LATENTS = 8192  # a stream's position budget; a new stream starts after it


def log(message):
    print(f"[phantasos-worker] {message}", file=sys.stderr, flush=True)


def fetch_source(cache, allow_download, status):
    """The directory holding the `matrix_game` package, fetched once."""
    root = os.path.join(cache, "src", f"Matrix-Game-{SOURCE_COMMIT[:12]}")
    project = os.path.join(root, "Matrix-Game-4")
    if os.path.isdir(os.path.join(project, "matrix_game")):
        return project
    if not allow_download:
        raise RuntimeError("Matrix-Game 4.0's code is not downloaded. Turn on 'Allow downloads' "
                           "(HDPHANTASOS_ALLOW_DOWNLOAD=1): its code, then about 21.5 GB of weights.")
    git = shutil.which("git")
    if not git:
        raise RuntimeError("fetching Matrix-Game 4.0's code needs git on PATH")
    status("fetching Matrix-Game 4.0's code")
    partial = root + ".partial"
    shutil.rmtree(partial, ignore_errors=True)

    def run(*arguments):
        # stdout is the delegate's channel: git's output goes to the log.
        subprocess.run([git, *arguments], check=True, stdout=sys.stderr, stderr=sys.stderr)

    run("clone", "--filter=blob:none", "--no-checkout", SOURCE_REPOSITORY, partial)
    run("-C", partial, "sparse-checkout", "set", "--no-cone", "/Matrix-Game-4/matrix_game/",
        "/Matrix-Game-4/LICENSE", "/Matrix-Game-4/NOTICE", "/Matrix-Game-4/THIRD_PARTY_NOTICES.md",
        "/Matrix-Game-4/licenses/")
    run("-C", partial, "checkout", "--quiet", SOURCE_COMMIT)
    os.replace(partial, root)
    return project


def fetch_weights(allow_download, status):
    from huggingface_hub import hf_hub_download
    if allow_download:
        status("downloading Matrix-Game 4.0 racing (about 21.5 GB)")
    paths = {}
    for key, ((repo, revision), name) in WEIGHTS.items():
        try:
            paths[key] = hf_hub_download(repo, name, revision=revision,
                                         local_files_only=not allow_download)
        except Exception as error:  # noqa: BLE001 - reported to the delegate as is
            raise RuntimeError(
                f"Matrix-Game 4.0 racing is not downloaded ({name}: {error}). Turn on 'Allow "
                f"downloads' (HDPHANTASOS_ALLOW_DOWNLOAD=1): about 21.5 GB.") from error
    return paths


def frame_box(width, height):
    """The centred 16:9 part of a width x height picture that the model sees:
    (left, top, right, bottom)."""
    if width * HEIGHT > height * WIDTH:
        inner = round(height * WIDTH / HEIGHT)
        left = (width - inner) // 2
        return left, 0, left + inner, height
    inner = round(width * HEIGHT / WIDTH)
    top = (height - inner) // 2
    return 0, top, width, top + inner


class MatrixGame:
    name = "matrixgame"
    streaming = True

    def __init__(self, allow_download, status, cache):
        import torch
        self.torch = torch
        self.status = status
        self.allow_download = allow_download
        self.cache = cache
        status("finding Matrix-Game 4.0")
        project = fetch_source(cache, allow_download, status)
        self.files = fetch_weights(allow_download, status)
        if project not in sys.path:
            sys.path.insert(0, project)
        self.captions = {}  # caption -> its T5 embedding, on the CPU
        self.transformer = self.vae = None
        self.caption = None  # the caption the loaded transformer holds
        self.stream = None
        self.stream_key = None
        self.inputs = []  # (request id, frame) waiting for a full chunk
        self.outputs = deque()  # (request id, picture) painted, not yet shown
        self.last = None  # (request id, picture) shown last

    # --- Loading ------------------------------------------------------------

    def _free(self):
        if self.stream is not None:
            self.stream.close()
        self.stream = self.transformer = self.vae = None
        self.caption = None
        gc.collect()
        self.torch.cuda.empty_cache()

    def _embed(self, caption):
        """The caption's umT5 embedding [1, 512, 4096], from memory, from the
        disk cache, or encoded (the encoder is 11 GB: it is on the GPU only
        while it encodes)."""
        cached = self.captions.get(caption)
        if cached is not None:
            return cached
        import hashlib
        torch = self.torch
        path = os.path.join(self.cache, "captions", hashlib.sha256(caption.encode()).hexdigest()[:32] + ".pt")
        if os.path.isfile(path):
            embedding = torch.load(path, weights_only=True)
        else:
            from matrix_game.inference.wan.modules.t5 import umt5_xxl
            from matrix_game.inference.wan.modules.tokenizers import HuggingfaceTokenizer
            self.status("encoding the caption (umT5-XXL)")
            started = time.time()
            # Built empty and filled from the bf16 checkpoint as it is:
            # their wrapper builds it in fp32 on the CPU first (23 GB).
            encoder = umt5_xxl(encoder_only=True, return_tokenizer=False, dtype=torch.bfloat16,
                               device=torch.device("meta"))
            state = torch.load(self.files["t5"], map_location="cpu", weights_only=True, mmap=True)
            encoder.load_state_dict(state, assign=True)
            encoder = encoder.to("cuda").eval().requires_grad_(False)
            tokenizer = HuggingfaceTokenizer(name=os.path.dirname(self.files["tokenizer"]), seq_len=512,
                                             clean="whitespace")
            ids, mask = tokenizer([caption], return_mask=True, add_special_tokens=True)
            with torch.no_grad():
                context = encoder(ids.to("cuda"), mask.to("cuda"))
                context[0, int(mask.gt(0).sum()):] = 0  # padding is zero, as in their encoder
                embedding = context.to(torch.bfloat16).cpu()
            del encoder, state, context
            gc.collect()
            torch.cuda.empty_cache()
            os.makedirs(os.path.dirname(path), exist_ok=True)
            torch.save(embedding, path)
            log(f"caption encoded in {time.time() - started:.1f} s")
        if len(self.captions) >= 16:
            self.captions.pop(next(iter(self.captions)))
        self.captions[caption] = embedding
        return embedding

    def _runtime_config(self):
        from matrix_game.inference.rtx5070ti import PRESETS, RuntimeConfig
        from matrix_game.inference.rtx5070ti.config import LINEAR_ROLES
        major, _ = self.torch.cuda.get_device_capability()
        if major >= 10:
            return PRESETS["rtx5070ti"]
        # Before Blackwell there is no MXFP8: every block linear in int8.
        return RuntimeConfig(attention="sdpa", cross_attention="sdpa",
                             linears=dict.fromkeys(LINEAR_ROLES, "int8"))

    def _load(self, caption):
        """The transformer for `caption`, and the codec, compiled and warmed up."""
        if self.transformer is not None and self.caption == caption:
            return
        torch = self.torch
        self._free()
        embedding = self._embed(caption)
        from matrix_game.inference.rtx5070ti import load_transformer, warm_up
        from matrix_game.inference.taehv import TAEHV
        from matrix_game.inference.vae_graph import StreamingVAEGraph
        self.status("loading Matrix-Game 4.0 racing")
        started = time.time()
        torch.backends.cuda.matmul.allow_tf32 = False
        torch.backends.cudnn.allow_tf32 = False
        self.transformer = load_transformer(self.files["checkpoint"], config=self._runtime_config())
        vae = TAEHV(self.files["taehv"]).to("cuda", torch.bfloat16).eval().requires_grad_(False)
        self.vae = StreamingVAEGraph(vae)
        self.embedding = embedding.to("cuda", torch.bfloat16)
        log(f"Matrix-Game loaded in {time.time() - started:.1f} s")
        self.status("compiling Matrix-Game 4.0 racing for this GPU")
        started = time.time()
        # One throwaway stream long enough to fill the window: every kernel
        # compiled, every graph captured. Later streams reuse them.
        warm_up(self.transformer, self.vae, self.embedding, None, "first_frame", "absolute", "taehv")
        log(f"Matrix-Game warmed up in {time.time() - started:.1f} s")
        self.caption = caption

    # --- The first frame ----------------------------------------------------

    def _first_frame(self, proxy, manifest, settings, choice):
        """The stream's anchor as a PIL image at 864x480, or None for the
        model to paint it."""
        from PIL import Image, ImageOps
        if choice == "release":
            with Image.open(self.files["sink"]) as image:
                return ImageOps.fit(image.convert("RGB"), (WIDTH, HEIGHT), Image.Resampling.LANCZOS)
        if choice != "klein":
            return None
        from .backends import Klein
        # Klein and Matrix-Game do not fit a 16 GB card together.
        self._free()
        klein = Klein(self.allow_download, self.status)
        try:
            self.status("painting the first frame (FLUX.2 Klein 4B)")
            picture, _ = klein.generate(proxy, manifest, dict(settings, size=WIDTH))
        finally:
            del klein
            gc.collect()
            self.torch.cuda.empty_cache()
        return ImageOps.fit(picture.convert("RGB"), (WIDTH, HEIGHT), Image.Resampling.LANCZOS)

    # --- Streaming ----------------------------------------------------------

    def _to_tensor(self, image):
        from matrix_game.inference.preprocessing import frames_to_tensor
        return frames_to_tensor([image])

    def _push(self, ids, frames):
        torch = self.torch
        from PIL import Image
        pixels = torch.cat(frames, 0)
        painted = []
        self.stream.push(pixels, frame_start=self.stream.frame, on_rgb=lambda rgb: painted.append(rgb.numpy().copy()))
        for frame_id, rgb in zip(ids, painted[0]):
            self.outputs.append((frame_id, Image.fromarray(rgb)))

    def _start(self, frame_id, proxy, view, manifest, settings, caption):
        from matrix_game.inference.rtx5070ti import FastStream
        from matrix_game.inference.stream import encode_sink_image
        choice = settings.get("firstFrame", "model")
        anchor = self._first_frame(proxy, manifest, settings, choice)
        self._load(caption)
        sink = None
        if anchor is not None:
            # encode_sink_image reads a file; the anchor is written beside the logs.
            path = os.path.join(os.environ.get("HDPHANTASOS_CACHE", "."), "run", "first_frame.png")
            os.makedirs(os.path.dirname(path), exist_ok=True)
            anchor.save(path)
            from pathlib import Path
            sink = encode_sink_image(self.vae, Path(path), "taehv", first_frame=True)
            sink = sink.to("cuda", self.torch.bfloat16)
        self.stream = FastStream(self.transformer, self.vae, self.embedding, seed=int(settings.get("seed", 0)),
                                 max_latents=MAX_LATENTS, vae_backend="taehv", steps=(1000,), first_steps=(1000,),
                                 sink_latent=sink, sink_mode="first_frame", sink_rope_mode="absolute")
        self.inputs.clear()
        self.outputs.clear()
        self._push([frame_id], [self._to_tensor(view)])

    def generate(self, proxy, manifest, settings, frame_id=0):
        from matrix_game.inference.preprocessing import crop_and_resize
        caption = prompts.racing_caption(manifest, settings.get("style", ""))
        # What makes a stream: a change to any of these starts a new one. The
        # caption is fixed at a stream's first frame, as in their fixed mode.
        key = (int(settings.get("seed", 0)), settings.get("firstFrame", "model"),
               settings.get("style", ""), manifest.get("shot", ""))
        view = crop_and_resize(proxy.convert("RGB"), HEIGHT, WIDTH)
        if (self.stream is None or self.stream_key != key or
                self.stream.index >= MAX_LATENTS - 1):
            self.stream_key = key
            self._start(frame_id, proxy, view, manifest, settings, caption)
        else:
            self.inputs.append((frame_id, self._to_tensor(view)))
            if len(self.inputs) == CHUNK:
                ids, frames = zip(*self.inputs)
                self.inputs.clear()
                self._push(list(ids), list(frames))
        if self.outputs:
            self.last = self.outputs.popleft()
        source, picture = self.last
        return self._fit(picture, proxy), self.caption, source

    def _fit(self, picture, proxy):
        """The 864x480 picture in the proxy's frame: where the proxy is not
        16:9, the part the model did not see shows the proxy, dimmed."""
        from PIL import Image
        box = frame_box(proxy.width, proxy.height)
        if box == (0, 0, proxy.width, proxy.height):
            return picture
        canvas = Image.eval(proxy.convert("RGB"), lambda v: v // 3)
        canvas.paste(picture.resize((box[2] - box[0], box[3] - box[1]), Image.Resampling.BILINEAR), box[:2])
        return canvas
