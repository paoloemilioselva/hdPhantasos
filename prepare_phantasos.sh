#!/usr/bin/env bash
# Set up the model worker ahead of time: the venv (Python 3.12, PyTorch,
# the libraries) and every model, downloaded and checked, so the first
# picture in usdview does not wait for them. Safe to run again after an
# interruption: everything resumes or is checked and fixed.
#
#   ./prepare_phantasos.sh                    everything (about 42 GB)
#   ./prepare_phantasos.sh --models klein4b   one model: klein4b, matrixgame
#   ./prepare_phantasos.sh --quick            check files by size, not SHA-256
#   ./prepare_phantasos.sh --check            download nothing; report what is missing
#
# Where: HDPHANTASOS_CACHE, else ~/.cache/hdPhantasos.
# Needs uv (https://docs.astral.sh/uv/) and git on PATH.

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v uv >/dev/null 2>&1; then
  echo "[hdPhantasos] uv is not on PATH. Install it:"
  echo "  curl -LsSf https://astral.sh/uv/install.sh | sh"
  exit 1
fi

models=(--models all)
for argument in "$@"; do
  [ "$argument" = "--models" ] && models=()
done
exec uv run --no-project --python 3.12 "$here/worker/setup_worker.py" "${models[@]}" "$@"
