"""The worker's loop: one frame at a time, as the delegate sends them.

    python -m phantasos_worker --shared <file> [--allow-download]

The delegate starts it through run_worker.py, which sets the cache paths
before anything imports Hugging Face code.
"""

import argparse
import sys
import time
import traceback

from . import backends
from .protocol import Link


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--shared", required=True, help="the memory-mapped frame file")
    parser.add_argument("--allow-download", action="store_true")
    args = parser.parse_args(argv)

    link = Link(args.shared)
    models = {}

    def status(message):
        backends.log(message)
        link.send({"op": "status", "message": message})

    link.send({"op": "ready"})
    for message in link.messages():
        op = message.get("op")
        if op == "quit":
            break
        if op != "frame":
            continue
        frame_id = message.get("id")
        try:
            from PIL import Image
            width, height = int(message["width"]), int(message["height"])
            settings = message.get("settings", {})
            name = settings.get("model", "klein4b")
            if name not in models:
                models[name] = backends.create(name, args.allow_download, status)
            started = time.time()
            proxy = Image.frombytes("RGB", (width, height), link.read_input(width, height))
            picture, prompt = models[name].generate(proxy, message.get("manifest", {}), settings)
            picture = picture.convert("RGB")
            link.write_output(picture.tobytes(), picture.width, picture.height)
            link.send({"op": "result", "id": frame_id, "width": picture.width,
                       "height": picture.height, "ms": round((time.time() - started) * 1000, 1),
                       "prompt": prompt})
        except Exception as error:  # noqa: BLE001 - every failure goes back to the delegate
            traceback.print_exc(file=sys.stderr)
            link.send({"op": "error", "id": frame_id, "message": str(error)})
    link.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
