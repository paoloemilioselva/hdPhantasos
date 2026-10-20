#!/usr/bin/env bash
# Open a stage in usdview with the hdPhantasos delegate selected.

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/setup_usd_env.sh" || exit $?

scene="${1:-$here/examples/floor_two_boxes.usda}"
if [ ! -f "$scene" ]; then
  echo "[hdPhantasos] Stage not found: $scene"
  exit 1
fi

echo "[hdPhantasos] usdview: $scene"
exec usdview "$scene" --renderer Phantasos
