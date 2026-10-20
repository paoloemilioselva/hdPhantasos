#!/usr/bin/env bash
# Render with the hdPhantasos delegate. Accepts normal usdrecord arguments.
#
# The Linux counterpart of render_phantasos.bat, which explains the fixed
# flags and lists the settings. Change both or neither.

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
. "$here/setup_usd_env.sh" || exit $?

if [ $# -eq 0 ]; then
  # The settings listing lives once, in render_phantasos.bat.
  sed -n -e 's/\r$//' -e 's/^  ECHO\.$//p' -e 's/^  ECHO \{0,1\}//p' "$here/render_phantasos.bat" |
    sed -e 's/\^\(.\)/\1/g' -e 's/render_phantasos\.bat/render_phantasos.sh/'
  exit 2
fi

exec usdrecord --renderer Phantasos --purposes render --colorCorrectionMode sRGB "$@"
