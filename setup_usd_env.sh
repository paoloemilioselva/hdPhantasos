# ===========================================================================
# hdPhantasos - shared standalone OpenUSD runtime environment (Linux).
#
# The counterpart of setup_usd_env.bat, and kept in step with it: every
# USD-facing script calls one of the two, and they are the only places that
# know where OpenUSD lives.
#
# Source it, do not run it -- the variables it sets must survive into the
# caller:
#
#   . ./setup_usd_env.sh
#
# Overrides, all optional, all honoured if already set:
#   USDROOT                  OpenUSD install root
#   USDEXTRA                 install prefix for hdPhantasos's own plugin
#   HDPHANTASOS_SKIP_PYTHON  set to 1 to skip the Python conformance check
# ===========================================================================

_hdphantasos_env() {
  local script="$1"
  HDPHANTASOS_ROOT="$(cd "$(dirname "$script")" && pwd)"
  export HDPHANTASOS_ROOT

  # -------------------------------------------------------------------------
  # 1. Locate OpenUSD.
  #     An explicit USDROOT always wins. Otherwise probe known-good roots,
  #     newest first, and validate by the presence of pxrConfig.cmake.
  # -------------------------------------------------------------------------
  if [ -z "${USDROOT:-}" ]; then
    local candidate
    for candidate in \
        "$HOME/dev/usd-26.03" "$HOME/dev/usd-26.05" "$HOME/dev/usd-25.08" \
        "/opt/usd-26.03" "/opt/usd-26.05" "/opt/usd-25.08"; do
      if [ -f "$candidate/pxrConfig.cmake" ]; then
        USDROOT="$candidate"
        break
      fi
    done
  fi
  if [ -z "${USDROOT:-}" ]; then
    echo "[hdPhantasos] OpenUSD was not found."
    echo "              Set USDROOT to an OpenUSD install root containing pxrConfig.cmake,"
    echo "              or install one under ~/dev or /opt. See docs/building.md."
    return 1
  fi
  if [ ! -f "$USDROOT/pxrConfig.cmake" ]; then
    echo "[hdPhantasos] USDROOT does not look like an OpenUSD install: $USDROOT"
    echo "              Expected \"$USDROOT/pxrConfig.cmake\" to exist."
    return 1
  fi
  export USDROOT

  # Install prefix for the plugin itself, shared with hdClaude: plugin
  # discovery is a union, so each delegate is one more entry in the same tree.
  export USDEXTRA="${USDEXTRA:-$HOME/usd-26.03-extra}"

  # -------------------------------------------------------------------------
  # 2. Python conformance.
  #     OpenUSD's Python bindings are ABI-locked to the interpreter the
  #     distribution was built against, which pxrConfig.cmake records.
  #     The active environment's Python is used as-is.
  # -------------------------------------------------------------------------
  if [ "${HDPHANTASOS_SKIP_PYTHON:-}" != "1" ]; then
    local version='import sys;print("%d.%d"%sys.version_info[:2])'
    local recorded required active python
    recorded="$(sed -n 's/.*set(Python3_EXECUTABLE \[\[\([^]]*\)\]\].*/\1/p' \
                "$USDROOT/pxrConfig.cmake" | head -n 1)"
    if [ -n "$recorded" ] && [ -x "$recorded" ]; then
      required="$("$recorded" -c "$version" 2>/dev/null)"
    fi
    # Fall back to the version OpenUSD 26.03 ships against when the recorded
    # interpreter has been moved or removed.
    required="${required:-3.12}"

    python="$(command -v python3 || command -v python)"
    if [ -z "$python" ]; then
      echo "[hdPhantasos] Python was not found in the active environment."
      echo "              OpenUSD at $USDROOT requires Python $required."
      return 1
    fi
    active="$("$python" -c "$version" 2>/dev/null)"
    if [ "$active" != "$required" ]; then
      echo "[hdPhantasos] Python version mismatch."
      echo "              Active:   $active  ($python)"
      echo "              Required: $required  (the interpreter OpenUSD was built against)"
      [ -n "$recorded" ] && echo "              Recorded: $recorded"
      echo "              Activate a matching environment, or set HDPHANTASOS_SKIP_PYTHON=1"
      echo "              for builds that do not use the Python bindings."
      return 1
    fi
  fi

  # -------------------------------------------------------------------------
  # 3. OpenUSD runtime paths. Linux resolves shared libraries through
  #     LD_LIBRARY_PATH where Windows uses PATH.
  # -------------------------------------------------------------------------
  export PXR_PLUGINPATH_NAME="$USDROOT:$USDROOT/plugin/usd:$USDEXTRA/plugin/usd"
  export PYTHONPATH="$USDROOT/lib/python:$USDEXTRA/lib/python${PYTHONPATH:+:$PYTHONPATH}"
  export PATH="$USDROOT/bin:$USDEXTRA/bin:$PATH"
  export LD_LIBRARY_PATH="$USDROOT/lib:$USDEXTRA/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  return 0
}

# bash names a sourced file in BASH_SOURCE; zsh in $0.
_hdphantasos_env "${BASH_SOURCE[0]:-$0}"
_hdphantasos_status=$?
unset -f _hdphantasos_env
return $_hdphantasos_status 2>/dev/null || exit $_hdphantasos_status
