"""Make the worker's venv: Python 3.12, PyTorch for this machine's driver,
and the model libraries; with --models, download and check the models too
(prepare_models.py).

    uv run --no-project --python 3.12 setup_worker.py [--cache <dir>]
        [--models klein4b,matrixgame|all] [--quick] [--check]

The delegate runs it without --models when 'Allow downloads' is on and the
venv is missing; the worker then fetches each model the first time it is
used. prepare_phantasos.bat/.sh run it with --models all, so everything is
in place before usdview starts. Run again after an interruption: an
unfinished venv is completed and unfinished downloads resume. --check
installs and downloads nothing, and exits 1 if anything is missing.
It needs uv (https://docs.astral.sh/uv/) on PATH.

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
    parser.add_argument("--models", help="also download these models: klein4b, matrixgame, or all")
    parser.add_argument("--quick", action="store_true", help="check model files by size only")
    parser.add_argument("--check", action="store_true", help="install and download nothing; report")
    args = parser.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    python = venv_python(args.cache)

    if args.check:
        if not os.path.isfile(python):
            print(f"[setup_worker] no venv in {args.cache}", file=sys.stderr)
            return 1
        result = subprocess.run([python, "-c", "import torch, diffusers, transformers, einops, sentencepiece; "
                                 "print('[setup_worker] venv: torch', torch.__version__, "
                                 "'CUDA' if torch.cuda.is_available() else 'NO CUDA')"])
        if result.returncode != 0:
            print("[setup_worker] the venv is incomplete: run without --check", file=sys.stderr)
            return 1
        if args.models:
            return subprocess.run([python, os.path.join(here, "prepare_models.py"), "--cache", args.cache,
                                   "--models", args.models, "--check"]).returncode
        return 0

    uv = shutil.which("uv")
    if not uv:
        print("[setup_worker] uv is not on PATH: see https://docs.astral.sh/uv/", file=sys.stderr)
        return 1
    os.makedirs(args.cache, exist_ok=True)
    venv = os.path.join(args.cache, "venv")
    if os.path.isdir(venv) and not os.path.isfile(python):
        # A venv cut short before its Python was in place: start it again.
        print(f"[setup_worker] removing an unfinished venv: {venv}", file=sys.stderr)
        shutil.rmtree(venv)
    if not os.path.isfile(python):
        run([uv, "venv", os.path.join(args.cache, "venv"), "--python", "3.12"])
    # torch first, from the index that matches the driver uv finds.
    run([uv, "pip", "install", "--python", python, "torch", "torchvision", "--torch-backend=auto"])
    run([uv, "pip", "install", "--python", python, *REQUIREMENTS])
    print(f"[setup_worker] ready: {python}", file=sys.stderr)
    if args.models:
        command = [python, os.path.join(here, "prepare_models.py"), "--cache", args.cache,
                   "--models", args.models]
        if args.quick:
            command.append("--quick")
        print("[setup_worker]", " ".join(command), file=sys.stderr, flush=True)
        return subprocess.run(command).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
