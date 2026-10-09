cmake_minimum_required(VERSION 3.17)
project(coup_openspiel C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# OpenSpiel submodule (lib/OpenSpiel). Its CMake root is open_spiel/.
# ---------------------------------------------------------------------------
set(OPEN_SPIEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/OpenSpiel")
set(OS "${OPEN_SPIEL_DIR}/open_spiel")
set(C_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../c_engine")

if(NOT EXISTS "${OS}/CMakeLists.txt")
  message(FATAL_ERROR
    "OpenSpiel not found at ${OPEN_SPIEL_DIR}. "
    "Run:  git submodule update --init lib/OpenSpiel")
endif()

# COUP_BUILD_PYSPIEL=ON additionally configures OpenSpiel's own `pyspiel`
# Python extension (open_spiel/python) with coup linked in; build it with
# `cmake --build . --target pyspiel` (see `make pyspiel`, which uses a separate
# build dir and the uv venv's Python). Default OFF keeps `make test-cpp` lean.
#
# OpenSpiel's optional-dependency switches are read from the *environment*
# (a -D cache value is ignored), so set them here for the configure step.
option(COUP_BUILD_PYSPIEL "Build OpenSpiel's pyspiel extension with coup" OFF)
if(NOT DEFINED ENV{OPEN_SPIEL_BUILD_WITH_PYTHON})
  if(COUP_BUILD_PYSPIEL)
    set(ENV{OPEN_SPIEL_BUILD_WITH_PYTHON} ON)
  else()
    set(ENV{OPEN_SPIEL_BUILD_WITH_PYTHON} OFF)
  endif()
endif()
# These only toggle which OpenSpiel python tests get registered with ctest;
# skip the jax/torch detection scripts.
foreach(_os_dep OPEN_SPIEL_ENABLE_JAX OPEN_SPIEL_ENABLE_PYTORCH)
  if(NOT DEFINED ENV{${_os_dep}})
    set(ENV{${_os_dep}} OFF)
  endif()
endforeach()

add_subdirectory("${OS}" "${CMAKE_CURRENT_BINARY_DIR}/open_spiel" EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# The one and only rules engine: c_engine/coup_core.c, compiled as C.
# ---------------------------------------------------------------------------
# coup_search.c is the opt-in search API (public state, hand space,
# resampler); the adapter uses its event recorder and resampler.
add_library(coup_core STATIC ${C_ENGINE_DIR}/coup_core.c ${C_ENGINE_DIR}/coup_search.c)
target_include_directories(coup_core PUBLIC ${C_ENGINE_DIR})
set_source_files_properties(${C_ENGINE_DIR}/coup_core.c ${C_ENGINE_DIR}/coup_search.c
  PROPERTIES LANGUAGE C)

# ---------------------------------------------------------------------------
# OpenSpiel adapter (registers the game "coup").
# ---------------------------------------------------------------------------
add_library(coup_game STATIC coup_game.cc coup_game.h)
target_include_directories(coup_game PUBLIC
  ${OPEN_SPIEL_DIR}            # "open_spiel/spiel.h"
  ${OS}/abseil-cpp             # "absl/..."
  ${OS}/json/include           # "nlohmann/..."
  ${C_ENGINE_DIR})
target_link_libraries(coup_game PUBLIC
  coup_core
  open_spiel_core
  absl::strings absl::str_format absl::synchronization
  absl::flat_hash_map absl::flat_hash_set absl::optional absl::span
  absl::random_random absl::flags absl::flags_parse absl::time)

# REGISTER_SPIEL_GAME is a static initializer in an otherwise-unreferenced
# object, so the archive must be force-loaded into every executable.
function(link_coup_game target)
  if(APPLE)
    target_link_libraries(${target} PRIVATE -Wl,-force_load $<TARGET_FILE:coup_game> coup_game)
  else()
    target_link_libraries(${target} PRIVATE -Wl,--whole-archive coup_game -Wl,--no-whole-archive coup_game)
  endif()
endfunction()

# The two OpenSpiel algorithm sources MCCFR needs. Compiled directly rather
# than pulling in the whole `algorithms` object library (which drags in every
# game and evaluation module).
set(COUP_MCCFR_SOURCES
  ${OS}/algorithms/cfr.cc
  ${OS}/algorithms/outcome_sampling_mccfr.cc)

# ---------------------------------------------------------------------------
# Tests: OpenSpiel basic_tests (RandomSimTest etc.) + adapter-specific checks.
# ---------------------------------------------------------------------------
add_executable(coup_game_test
  coup_game_test.cc
  $<TARGET_OBJECTS:tests>
  ${COUP_MCCFR_SOURCES})
link_coup_game(coup_game_test)

# Standalone MCCFR example (outcome sampling, prints average-policy stats).
add_executable(mccfr_example mccfr_example.cc ${COUP_MCCFR_SOURCES})
link_coup_game(mccfr_example)

# ---------------------------------------------------------------------------
# pyspiel with coup (COUP_BUILD_PYSPIEL=ON). pyspiel already contains all of
# OpenSpiel's objects (incl. open_spiel_core), so it must not get coup_game.a
# (which embeds open_spiel_core objects -> duplicate symbols). Instead the
# adapter is compiled once more as an OBJECT library whose objects go straight
# into the module; directly-linked objects are always kept, so the
# REGISTER_SPIEL_GAME initializer survives without -force_load.
# ---------------------------------------------------------------------------
if(COUP_BUILD_PYSPIEL)
  if(NOT TARGET pyspiel)
    message(FATAL_ERROR "COUP_BUILD_PYSPIEL=ON but OpenSpiel did not define "
      "the pyspiel target (is OPEN_SPIEL_BUILD_WITH_PYTHON=OFF in the env?)")
  endif()
  add_library(coup_game_pyobj OBJECT coup_game.cc
    ${C_ENGINE_DIR}/coup_core.c ${C_ENGINE_DIR}/coup_search.c)
  target_include_directories(coup_game_pyobj PRIVATE
    ${OPEN_SPIEL_DIR} ${OS}/abseil-cpp ${OS}/json/include ${C_ENGINE_DIR})
  target_sources(pyspiel PRIVATE $<TARGET_OBJECTS:coup_game_pyobj>)
  add_dependencies(pyspiel coup_game_pyobj)
  # Known output dir: <build>/python/pyspiel.so (put on sys.path by `make pyspiel`).
  set(COUP_PYSPIEL_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/python" CACHE PATH
    "Directory that receives pyspiel.so")
  set_target_properties(pyspiel PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${COUP_PYSPIEL_OUTPUT_DIR}")
endif()

# ---------------------------------------------------------------------------
# Benchmark
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../profiling/bench_cpp.cc")
  add_executable(bench_cpp ../profiling/bench_cpp.cc)
  link_coup_game(bench_cpp)
  target_compile_options(bench_cpp PRIVATE -O3 -march=native)
endif()
