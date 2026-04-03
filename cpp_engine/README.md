# C++ Engine (OpenSpiel)

Pure C++ implementation of Coup as an [OpenSpiel](https://github.com/google-deepmind/open_spiel) game. Used for CFR and game-theoretic analysis.

## Build

Requires the OpenSpiel submodule:

```bash
git submodule update --init lib/OpenSpiel
make test-cpp    # from repo root
```

Or manually via CMake:

```bash
mkdir build && cd build
cmake .. && make
./coup_game_test
```

## Usage (Python)

```python
import pyspiel
game = pyspiel.load_game("coup(players=2)")
state = game.new_initial_state()
```

## Key Files

- `coup_game.h` -- `CoupGame` and `CoupState` class declarations
- `coup_game.cc` -- Full implementation (~41KB)
- `coup_game_test.cc` -- OpenSpiel test suite (RandomSimTest, chance outcomes, etc.)
- `CMakeLists.txt` -- Builds against `lib/OpenSpiel/` submodule

## Design

Independent implementation from the C engine, sharing the same rules and 32-action space. Cross-framework tests in `../tests/` enforce parity between engines.
