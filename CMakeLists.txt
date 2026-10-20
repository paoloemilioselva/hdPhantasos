cmake_minimum_required(VERSION 3.25)

project(hdPhantasos VERSION 0.1.0 LANGUAGES C CXX)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CTest)
include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Options
#
# The core (rasterizer, proxy look, scene manifest) depends on nothing but the
# C++ standard library and always builds. The Hydra plugin and the codeless
# schema need an OpenUSD install, found by setup_usd_env.bat/.sh.
# ---------------------------------------------------------------------------
option(HDPHANTASOS_BUILD_HYDRA_PLUGIN "Build the OpenUSD Hydra render delegate" ON)
option(HDPHANTASOS_WARNINGS_AS_ERRORS "Treat hdPhantasos compiler warnings as errors" OFF)

set(HDPHANTASOS_OPENUSD_ROOT "$ENV{USDROOT}" CACHE PATH
    "OpenUSD install root. Defaults to the USDROOT set by setup_usd_env.bat/.sh.")
if(NOT HDPHANTASOS_OPENUSD_ROOT AND WIN32)
  set(HDPHANTASOS_OPENUSD_ROOT "C:/dev/usd-26.03" CACHE PATH "OpenUSD install root" FORCE)
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(DEFINED ENV{USDEXTRA})
    set(CMAKE_INSTALL_PREFIX "$ENV{USDEXTRA}" CACHE PATH "hdPhantasos install prefix" FORCE)
  endif()
endif()

# ---------------------------------------------------------------------------
# Shared compile options
# ---------------------------------------------------------------------------
add_library(hdPhantasosProjectOptions INTERFACE)
target_compile_features(hdPhantasosProjectOptions INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(hdPhantasosProjectOptions INTERFACE
      /W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /utf-8 /EHsc)
  target_compile_definitions(hdPhantasosProjectOptions INTERFACE
      NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
  if(HDPHANTASOS_WARNINGS_AS_ERRORS)
    target_compile_options(hdPhantasosProjectOptions INTERFACE /WX)
  endif()
else()
  target_compile_options(hdPhantasosProjectOptions INTERFACE -Wall -Wextra -Wpedantic)
  if(HDPHANTASOS_WARNINGS_AS_ERRORS)
    target_compile_options(hdPhantasosProjectOptions INTERFACE -Werror)
  endif()
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Subsystems, in dependency order.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)

if(HDPHANTASOS_BUILD_HYDRA_PLUGIN)
  include(OpenUSD)
  add_subdirectory(src/hydra)
  add_subdirectory(schema)
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
