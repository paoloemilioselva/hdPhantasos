# ---------------------------------------------------------------------------
# hdPhantasosCore
#
# The rasterizer, the proxy look and the scene manifest. No OpenUSD and no
# third-party dependency, so all of it is testable with a compiler alone: the
# core-only preset exists to run those tests.
# ---------------------------------------------------------------------------

add_library(hdPhantasosCore STATIC
    raster.cpp
)

add_library(hdPhantasos::Core ALIAS hdPhantasosCore)

target_include_directories(hdPhantasosCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(hdPhantasosCore
    PUBLIC hdPhantasosProjectOptions
    PRIVATE Threads::Threads)

set_target_properties(hdPhantasosCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
