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
  ${OPEN_SPIEL_DIR}              # so "open_spiel/spiel.h" resolves
)

# Link against the full open_spiel shared library, which bundles all OBJECT
# libraries (core, games, algorithms, etc.) and transitively exports abseil.
# Note: open_spiel_core is an OBJECT library and lacks transitive deps.
target_link_libraries(coup_game PUBLIC open_spiel)

# ---------------------------------------------------------------------------
# Test executable (coup_game_test.cc will be added in a later task)
# ---------------------------------------------------------------------------
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/coup_game_test.cc")
  add_executable(coup_game_test coup_game_test.cc)
  target_link_libraries(coup_game_test PRIVATE coup_game open_spiel)
endif()
