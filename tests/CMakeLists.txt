# ---------------------------------------------------------------------------
# Tests
#
# The core tests need neither OpenUSD nor a GPU and always build. The stage
# test renders the example stages through usdrecord with the installed plugin,
# so it runs only in a full build, after `compile.py` has installed.
# ---------------------------------------------------------------------------

add_executable(hdPhantasosCoreTests core_tests.cpp)
target_link_libraries(hdPhantasosCoreTests PRIVATE hdPhantasos::Core)
target_include_directories(hdPhantasosCoreTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME core COMMAND hdPhantasosCoreTests)

if(HDPHANTASOS_BUILD_HYDRA_PLUGIN)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME stage
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/usd/stage_test.py
              --output ${CMAKE_CURRENT_BINARY_DIR}/stage)
  # The test runs usdrecord from the environment compile.py builds in, which
  # setup_usd_env.bat/.sh made; ctest started by hand needs that environment
  # sourced first.
  set_tests_properties(stage PROPERTIES SKIP_RETURN_CODE 77)
endif()
