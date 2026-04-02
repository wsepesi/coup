cmake_minimum_required(VERSION 3.17)
project(coup_openspiel CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# OpenSpiel submodule
# ---------------------------------------------------------------------------
# The OpenSpiel repo is expected at lib/OpenSpiel/ relative to the project root.
# Its top-level CMakeLists.txt lives inside open_spiel/, so we add that subtree.
set(OPEN_SPIEL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../lib/OpenSpiel")

if(NOT EXISTS "${OPEN_SPIEL_DIR}/open_spiel/CMakeLists.txt")
  message(FATAL_ERROR
    "OpenSpiel not found at ${OPEN_SPIEL_DIR}. "
    "Run:  git submodule update --init --recursive")
endif()

add_subdirectory(
  "${OPEN_SPIEL_DIR}/open_spiel"
  "${CMAKE_CURRENT_BINARY_DIR}/open_spiel"
  EXCLUDE_FROM_ALL
)

# ---------------------------------------------------------------------------
# Coup game library
# ---------------------------------------------------------------------------
add_library(coup_game STATIC
  coup_game.cc
  coup_game.h
)

target_include_directories(coup_game PUBLIC
  ${OPEN_SPIEL_DIR}                              # so "open_spiel/spiel.h" resolves
  ${OPEN_SPIEL_DIR}/open_spiel/abseil-cpp        # so "absl/..." resolves
  ${OPEN_SPIEL_DIR}/open_spiel/json/include      # so "nlohmann/..." resolves
)

# Link against open_spiel_core OBJECT library + abseil.
target_link_libraries(coup_game PUBLIC
  open_spiel_core
  absl::strings absl::str_format absl::synchronization
  absl::flat_hash_map absl::flat_hash_set absl::optional absl::span
  absl::random_random absl::flags absl::flags_parse
)

# ---------------------------------------------------------------------------
# Test executable (coup_game_test.cc will be added in a later task)
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/coup_game_test.cc")
  add_executable(coup_game_test coup_game_test.cc)
  target_link_libraries(coup_game_test PRIVATE coup_game)
endif()

# ---------------------------------------------------------------------------
# Benchmark executable
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../profiling/bench_cpp.cc")
  add_executable(bench_cpp ../profiling/bench_cpp.cc)
  # Force-load entire coup_game archive so REGISTER_SPIEL_GAME static init is kept
  if(APPLE)
    target_link_libraries(bench_cpp PRIVATE -Wl,-force_load coup_game)
  else()
    target_link_libraries(bench_cpp PRIVATE -Wl,--whole-archive coup_game -Wl,--no-whole-archive)
  endif()
  target_compile_options(bench_cpp PRIVATE -O3 -march=native)
endif()
