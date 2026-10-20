"""Models. Each takes the proxy (a PIL image), the manifest and the settings,
and returns a PIL image. They are loaded on first use and kept loaded.
"""

import os
import sys
import time

from . import prompt as prompts


def log(message):
    print(f"[phantasos-worker] {message}", file=sys.stderr, flush=True)


class Echo:
    """No model: the proxy, desaturated and halved. For tests, which need the
    whole path through the worker without a GPU or a download."""

    name = "echo"

    def generate(self, proxy, manifest, settings):
        from PIL import ImageOps
        picture = ImageOps.grayscale(proxy).convert("RGB")
        picture = picture.resize((max(1, proxy.width // 2), max(1, proxy.height // 2)))
        return picture, prompts.write(manifest, settings.get("look", "flat"), settings.get("style", ""))


class Klein:
    """FLUX.2 [klein] 4B (Apache-2.0), distilled to four steps, with the proxy
    as its reference image: it keeps the layout and paints what the prompt
    describes.

    Memory: the transformer (bf16, 7.75 GB) stays on the GPU. The Qwen3-4B
    text encoder (8 GB in bf16) is quantised to int8 weights (about 4 GB) so
    both fit a 16 GB card, and prompt embeddings are cached by text, so it
    runs only when the picture's content changes.
    """

    name = "klein4b"
    repo = "black-forest-labs/FLUX.2-klein-4B"
    patterns = ["model_index.json", "scheduler/*", "text_encoder/*", "tokenizer/*",
                "transformer/*", "vae/*", "LICENSE.md"]

    def __init__(self, allow_download, status):
        import torch
        from diffusers import Flux2KleinPipeline
        from huggingface_hub import snapshot_download

        status("finding FLUX.2 Klein 4B")
        try:
            path = snapshot_download(self.repo, allow_patterns=self.patterns,
                                     local_files_only=not allow_download)
        except Exception as error:  # noqa: BLE001 - reported to the delegate as is
            raise RuntimeError(
                f"FLUX.2 Klein 4B is not downloaded ({error}). Turn on 'Allow downloads' "
                f"(HDPHANTASOS_ALLOW_DOWNLOAD=1): about 16 GB.") from error

        status("loading FLUX.2 Klein 4B")
        started = time.time()
        self.torch = torch
        self.pipe = Flux2KleinPipeline.from_pretrained(path, torch_dtype=torch.bfloat16)
        try:
            from torchao.quantization import Int8WeightOnlyConfig, quantize_
            quantize_(self.pipe.text_encoder, Int8WeightOnlyConfig())
        except Exception as error:  # noqa: BLE001
            log(f"int8 text encoder unavailable ({error}); it stays bf16")
        self.pipe.to("cuda")
        self.pipe.set_progress_bar_config(disable=True)
        self.embeddings = {}
        log(f"FLUX.2 Klein 4B loaded in {time.time() - started:.1f} s")

    def _embed(self, text):
        cached = self.embeddings.get(text)
        if cached is None:
            with self.torch.inference_mode():
                cached = self.pipe.encode_prompt(prompt=text, device="cuda")
            if len(self.embeddings) > 16:
                self.embeddings.pop(next(iter(self.embeddings)))
            self.embeddings[text] = cached
        return cached

    def generate(self, proxy, manifest, settings):
        text = prompts.write(manifest, settings.get("look", "flat"), settings.get("style", ""))
        long_side = int(settings.get("size", 512))
        scale = long_side / max(proxy.width, proxy.height)
        # Multiples of 16, as the model needs.
        width = max(16, round(proxy.width * scale / 16) * 16)
        height = max(16, round(proxy.height * scale / 16) * 16)
        reference = proxy.resize((width, height))
        embeds = self._embed(text)
        prompt_embeds = embeds[0] if isinstance(embeds, tuple) else embeds
        generator = self.torch.Generator("cuda").manual_seed(int(settings.get("seed", 0)))
        with self.torch.inference_mode():
            result = self.pipe(image=[reference], prompt_embeds=prompt_embeds, width=width,
                               height=height, num_inference_steps=int(settings.get("steps", 4)),
                               guidance_scale=1.0, generator=generator)
        return result.images[0], text


def create(name, allow_download, status):
    if name == "echo":
        return Echo()
    if name in ("klein4b", "klein"):
        return Klein(allow_download, status)
    raise ValueError(f"unknown model '{name}'; known: klein4b, echo")


def cache_directory():
    return os.environ.get("HF_HOME", "")
