# ---------------------------------------------------------------------------
# OpenUSD.
#
# An external dependency: hdPhantasos builds against a standalone OpenUSD
# distribution and never vendors, fetches, or patches USD source, so only
# public OpenUSD headers may be included.
#
# Do not add Houdini search paths here. A Houdini-ABI build needs a separate
# preset and dependency graph, not a conditional in this file.
# ---------------------------------------------------------------------------

include_guard(GLOBAL)

if(NOT EXISTS "${HDPHANTASOS_OPENUSD_ROOT}/pxrConfig.cmake")
  message(FATAL_ERROR
      "OpenUSD was not found at '${HDPHANTASOS_OPENUSD_ROOT}'.\n"
      "Expected '${HDPHANTASOS_OPENUSD_ROOT}/pxrConfig.cmake'.\n"
      "Set -DHDPHANTASOS_OPENUSD_ROOT=<install root>, or build with compile.py, "
      "whose setup_usd_env.bat/.sh discovers one and exports USDROOT. See "
      "docs/building.md.")
endif()

list(PREPEND CMAKE_PREFIX_PATH "${HDPHANTASOS_OPENUSD_ROOT}")

# Point USD's transitive dependencies at the same distribution, so a system
# installation of any of them cannot be picked up and produce an ABI mismatch
# that only shows up as a crash at plugin load.
set(OpenSubdiv_DIR "${HDPHANTASOS_OPENUSD_ROOT}/lib/cmake/OpenSubdiv" CACHE PATH "" FORCE)
set(Imath_DIR      "${HDPHANTASOS_OPENUSD_ROOT}/lib/cmake/Imath"      CACHE PATH "" FORCE)

# pxrConfig.cmake finds MaterialX itself when OpenUSD was built with it. The
# delegate never uses MaterialX, but the find has to succeed for pxr's targets
# to load, so it is pointed at the copy OpenUSD was built against: bundled in
# the OpenUSD root (build_usd.py), or installed on its own (rez, spack, ...),
# in which case pxrConfig.cmake records its MaterialX_DIR. $MATERIALX_ROOT
# overrides both, as in hdClaude.
if(DEFINED ENV{MATERIALX_ROOT} AND NOT "$ENV{MATERIALX_ROOT}" STREQUAL "")
  set(_materialx_dir "$ENV{MATERIALX_ROOT}/lib/cmake/MaterialX")
elseif(EXISTS "${HDPHANTASOS_OPENUSD_ROOT}/lib/cmake/MaterialX/MaterialXConfig.cmake")
  set(_materialx_dir "${HDPHANTASOS_OPENUSD_ROOT}/lib/cmake/MaterialX")
else()
  file(STRINGS "${HDPHANTASOS_OPENUSD_ROOT}/pxrConfig.cmake" _materialx_dir
       REGEX "set\\(MaterialX_DIR \\[\\[[^]]*\\]\\]\\)" LIMIT_COUNT 1)
  string(REGEX REPLACE ".*\\[\\[([^]]*)\\]\\].*" "\\1" _materialx_dir "${_materialx_dir}")
endif()
if(_materialx_dir)
  set(MaterialX_DIR "${_materialx_dir}" CACHE PATH "" FORCE)
endif()

find_package(OpenGL REQUIRED)

find_package(pxr CONFIG REQUIRED
    PATHS "${HDPHANTASOS_OPENUSD_ROOT}"
    NO_DEFAULT_PATH)

message(STATUS "hdPhantasos: OpenUSD ${PXR_VERSION} at ${HDPHANTASOS_OPENUSD_ROOT}")
