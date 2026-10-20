"""The link to the delegate.

Messages are JSON objects, one per line: the delegate writes to the worker's
stdin, the worker answers on stdout. stdout carries nothing else, so every
log line goes to stderr (which the delegate leaves on the host's console).

Pixels go through a memory-mapped file the delegate creates, so a frame is
never serialised: the delegate writes the proxy at INPUT_OFFSET and the
worker writes the picture at OUTPUT_OFFSET, both 8-bit RGB, top row first.

    delegate -> worker
        {"op": "frame", "id": 7, "width": 864, "height": 480,
         "settings": {...}, "manifest": {...}}
        {"op": "quit"}
    worker -> delegate
        {"op": "status", "message": "loading FLUX.2 Klein 4B"}
        {"op": "result", "id": 7, "width": 512, "height": 288, "ms": 905.2,
         "prompt": "..."}
        {"op": "error", "id": 7, "message": "..."}

Keep src/hydra/worker_link.cpp in step with this file.
"""

import json
import mmap
import sys

HEADER = 4096
# One region holds a 4K 16:9 frame; the file is HEADER + two regions.
REGION = 3840 * 2160 * 3
INPUT_OFFSET = HEADER
OUTPUT_OFFSET = HEADER + REGION
FILE_SIZE = HEADER + 2 * REGION


class Link:
    def __init__(self, shared_path):
        self._file = open(shared_path, "r+b")
        self._map = mmap.mmap(self._file.fileno(), FILE_SIZE)
        self._out = sys.stdout

    def messages(self):
        for line in sys.stdin:
            line = line.strip()
            if line:
                yield json.loads(line)

    def read_input(self, width, height):
        size = width * height * 3
        if size > REGION:
            raise ValueError(f"a {width}x{height} frame does not fit the shared region")
        return bytes(self._map[INPUT_OFFSET:INPUT_OFFSET + size])

    def write_output(self, rgb, width, height):
        size = width * height * 3
        if size > REGION or len(rgb) != size:
            raise ValueError(f"a {width}x{height} picture does not fit the shared region")
        self._map[OUTPUT_OFFSET:OUTPUT_OFFSET + size] = rgb

    def send(self, message):
        self._out.write(json.dumps(message) + "\n")
        self._out.flush()

    def close(self):
        self._map.close()
        self._file.close()
