"""Make the worker's venv: Python 3.12, PyTorch for this machine's driver,
and the model libraries. Model weights are not downloaded here; the worker
fetches each the first time it is used, when downloads are allowed.

    uv run --no-project --python 3.12 setup_worker.py [--cache <dir>]

The delegate runs exactly that when 'Allow downloads' is on and the venv is
missing. It needs uv (https://docs.astral.sh/uv/) on PATH.

The cache defaults to %LOCALAPPDATA%\\hdPhantasos on Windows and
~/.cache/hdPhantasos elsewhere; HDPHANTASOS_CACHE overrides both.
"""

import argparse
import os
import shutil
import subprocess
import sys

REQUIREMENTS = ["numpy", "pillow", "accelerate", "diffusers>=0.37", "huggingface_hub",
                "safetensors", "torchao", "transformers>=4.57",
                # Matrix-Game 4.0's inference code (fetched by the worker, not from PyPI)
                "einops", "ftfy", "regex", "sentencepiece"]
# Its GPU kernels are Triton. Linux PyTorch wheels bring Triton; on Windows it
# is a separate build.
if os.name == "nt":
    REQUIREMENTS.append("triton-windows")


def default_cache():
    if os.environ.get("HDPHANTASOS_CACHE"):
        return os.environ["HDPHANTASOS_CACHE"]
    if os.name == "nt":
        return os.path.join(os.environ.get("LOCALAPPDATA", os.path.expanduser("~")), "hdPhantasos")
    return os.path.join(os.path.expanduser("~"), ".cache", "hdPhantasos")


def venv_python(cache):
    if os.name == "nt":
        return os.path.join(cache, "venv", "Scripts", "python.exe")
    return os.path.join(cache, "venv", "bin", "python")


def run(command):
    print("[setup_worker]", " ".join(command), file=sys.stderr, flush=True)
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", default=default_cache())
    args = parser.parse_args()

    uv = shutil.which("uv")
    if not uv:
        print("[setup_worker] uv is not on PATH: see https://docs.astral.sh/uv/", file=sys.stderr)
        return 1
    os.makedirs(args.cache, exist_ok=True)
    python = venv_python(args.cache)
    if not os.path.isfile(python):
        run([uv, "venv", os.path.join(args.cache, "venv"), "--python", "3.12"])
    # torch first, from the index that matches the driver uv finds.
    run([uv, "pip", "install", "--python", python, "torch", "torchvision", "--torch-backend=auto"])
    run([uv, "pip", "install", "--python", python, *REQUIREMENTS])
    print(f"[setup_worker] ready: {python}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
