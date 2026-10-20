# ---------------------------------------------------------------------------
# hdPhantasos
#
# The Hydra render delegate: the seam between OpenUSD and the core. Built as a
# MODULE: nothing links against a Hydra plugin; the Plug system loads it.
# ---------------------------------------------------------------------------

add_library(hdPhantasos MODULE
    implicit_surface_scene_index_plugin.cpp
    instancer.cpp
    mesh.cpp
    render_buffer.cpp
    render_delegate.cpp
    render_pass.cpp
    renderer_plugin.cpp
    scene_store.cpp
    worker_link.cpp
)

target_include_directories(hdPhantasos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(hdPhantasos
    PRIVATE
        hdPhantasosProjectOptions
        hdPhantasos::Core
        gf
        hd
        hdsi
        hio
        plug
        sdf
        tf
        vt
)

# _SILENCE_...: OpenUSD's hd/dataSource.h uses the shared_ptr atomics C++20
# deprecates, and MSVC says so at every include.
target_compile_definitions(hdPhantasos PRIVATE HDPHANTASOS_EXPORTS
    _SILENCE_CXX20_OLD_SHARED_PTR_ATOMIC_SUPPORT_DEPRECATION_WARNING)

# OpenUSD's headers are not warning-clean at /W4; our own code is held to it.
if(MSVC)
  target_compile_options(hdPhantasos PRIVATE /wd4244 /wd4267 /wd4275 /wd4251 /wd4100)
endif()

# The plugin's own name, with no lib prefix: Plug matches the LibraryPath in
# plugInfo.json literally.
set_target_properties(hdPhantasos PROPERTIES
    PREFIX ""
    OUTPUT_NAME "hdPhantasos"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin/usd"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin/usd"
)

# ---------------------------------------------------------------------------
# Plugin layout, the same as hdClaude's and OpenUSD's own:
#
#   plugin/usd/plugInfo.json                     {"Includes": ["*/resources/"]}
#   plugin/usd/hdPhantasos.dll                   (hdPhantasos.so on Linux)
#   plugin/usd/hdPhantasos/resources/plugInfo.json
#
# Assembled in the build tree as well as at install, so the plugin is usable
# straight from the build tree.
# ---------------------------------------------------------------------------
set(HDPHANTASOS_PLUGIN_RESOURCES "${CMAKE_BINARY_DIR}/plugin/usd/hdPhantasos/resources")

# LibraryPath must name the module exactly as the linker wrote it, so it is
# generated from the target rather than written by hand.
file(GENERATE
    OUTPUT "${HDPHANTASOS_PLUGIN_RESOURCES}/plugInfo.json"
    INPUT  "${CMAKE_SOURCE_DIR}/resources/plugInfo.json.in")
file(GENERATE
    OUTPUT "${CMAKE_BINARY_DIR}/plugin/usd/plugInfo.json"
    CONTENT "{\n    \"Includes\": [ \"*/resources/\" ]\n}\n")

# The model worker's Python code goes beside the plugin's resources, where the
# delegate finds it through Plug and runs it with the worker venv's Python.
add_custom_command(TARGET hdPhantasos POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/worker"
            "${HDPHANTASOS_PLUGIN_RESOURCES}/worker"
    COMMENT "hdPhantasos: copying the model worker into the plugin tree"
    VERBATIM
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/worker/"
    DESTINATION "plugin/usd/hdPhantasos/resources/worker"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "tests" EXCLUDE)

install(TARGETS hdPhantasos
    LIBRARY DESTINATION "plugin/usd"
    RUNTIME DESTINATION "plugin/usd"
)
install(FILES "${HDPHANTASOS_PLUGIN_RESOURCES}/plugInfo.json"
    DESTINATION "plugin/usd/hdPhantasos/resources")
install(FILES "${CMAKE_BINARY_DIR}/plugin/usd/plugInfo.json"
    DESTINATION "plugin/usd")
