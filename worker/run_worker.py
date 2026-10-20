"""What the delegate runs, with the worker venv's Python:

    <venv python> run_worker.py --cache <dir> --shared <file> [--allow-download]

Puts this directory on sys.path, so the worker code is always the copy
installed beside the plugin (the venv holds only the dependencies), and
points Hugging Face at the cache before anything imports it.
"""

import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)

arguments = sys.argv[1:]
if "--cache" in arguments:
    at = arguments.index("--cache")
    cache = arguments[at + 1]
    del arguments[at:at + 2]
    os.environ.setdefault("HF_HOME", os.path.join(cache, "hf"))
    os.environ["HDPHANTASOS_CACHE"] = cache
if "--allow-download" not in arguments:
    os.environ["HF_HUB_OFFLINE"] = "1"
os.environ.setdefault("HF_HUB_DISABLE_PROGRESS_BARS", "1")
os.environ.setdefault("TRANSFORMERS_VERBOSITY", "error")

from phantasos_worker.__main__ import main  # noqa: E402

sys.exit(main(arguments))
