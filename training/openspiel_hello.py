#!/usr/bin/env python3
"""OpenSpiel entry point for Coup.

Coup is NOT available in the pip `open_spiel` / `pyspiel` package. The game is
an out-of-tree C++ adapter (cpp_engine/coup_game.cc) over the C engine, built
against the lib/OpenSpiel submodule. Two ways to use it:

    make pyspiel                 # pyspiel built from lib/OpenSpiel with coup
                                 # (uv venv; see training/README.md)
    make mccfr-example           # pure C++: cpp_engine/mccfr_example

This script checks whether a pyspiel with `coup` registered is importable (if
so it runs a short random playout); otherwise it builds and runs the C++ MCCFR
example.

Usage: uv run training/openspiel_hello.py [iterations]
"""
import os
import random
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "cpp_engine", "build")
EXAMPLE = os.path.join(BUILD, "mccfr_example")


def try_pyspiel():
    try:
        import pyspiel  # type: ignore
    except ImportError:
        return False
    if "coup" not in pyspiel.registered_names():
        print("pyspiel is importable but has no 'coup' game (pip build?); run `make pyspiel`.")
        return False
    game = pyspiel.load_game("coup(players=2)")
    state = game.new_initial_state()
    while not state.is_terminal():
        if state.is_chance_node():
            actions, probs = zip(*state.chance_outcomes())
            state.apply_action(random.choices(actions, weights=probs)[0])
        else:
            state.apply_action(random.choice(state.legal_actions()))
    print(f"pyspiel coup random playout returns: {state.returns()}")
    return True


def run_cpp_example(iterations):
    if not os.path.exists(EXAMPLE):
        print("Building cpp_engine/mccfr_example (first build compiles OpenSpiel core)...")
        os.makedirs(BUILD, exist_ok=True)
        subprocess.run(["cmake", ".."], cwd=BUILD, check=True)
        subprocess.run(["cmake", "--build", ".", "--target", "mccfr_example", "-j"],
                       cwd=BUILD, check=True)
    subprocess.run([EXAMPLE, str(iterations)], check=True)


def main():
    iterations = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
    if not try_pyspiel():
        run_cpp_example(iterations)


if __name__ == "__main__":
    main()
