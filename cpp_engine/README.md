# OpenSpiel Adapter

Exposes Coup to [OpenSpiel](https://github.com/google-deepmind/open_spiel) (CFR, MCCFR, exploitability, ...) as the game `coup`. It is a thin adapter over the C engine (`../c_engine/coup_core.c`, called via `extern "C"`), not a separate implementation, so OpenSpiel plays the same game as PufferLib, the web stack and the TUI.

## Build & test

Requires the OpenSpiel submodule:

```bash
git submodule update --init lib/OpenSpiel
make test-cpp        # from repo root: builds and runs coup_game_test
make mccfr-example   # outcome-sampling MCCFR demo (2 players)
```

Or manually:

```bash
mkdir -p build && cd build
cmake .. && cmake --build . --target coup_game_test mccfr_example bench_cpp -j
./coup_game_test
./mccfr_example 20000
```

C++ programs use the game through `LoadGame("coup", {{"players", GameParameter(3)}})`. Link `coup_game` with `-force_load` (macOS) or `--whole-archive` (Linux) so the `REGISTER_SPIEL_GAME` static initializer is kept; `CMakeLists.txt` has a `link_coup_game()` helper for this.

The game is **not** registered in the pip `open_spiel` / `pyspiel` package. For Python, `make pyspiel` (repo root) configures this directory with `-DCOUP_BUILD_PYSPIEL=ON` in a separate build dir (`build-py/`) against the uv venv's Python. That turns on OpenSpiel's own `pyspiel` target and adds `coup_game.cc` + `coup_core.c` + `coup_search.c` to it as an OBJECT library. Objects linked directly are always kept, so no `-force_load` is needed, and `coup_game.a` is not used there because it embeds `open_spiel_core`, which pyspiel already contains. The default build (`make test-cpp`) leaves Python off (`OPEN_SPIEL_BUILD_WITH_PYTHON` is read from the environment, and this CMakeLists sets it). See `training/README.md` for the Python env and which algorithms work.

## Files

- `coup_game.h` / `coup_game.cc`: `CoupGame` and `CoupState`.
- `coup_game_test.cc`: tests (see below).
- `mccfr_example.cc`: outcome-sampling MCCFR; prints p0's opening policy per starting hand.
- `CMakeLists.txt`: builds `coup_core.c` and `coup_search.c` (as C), the adapter, the tests, the example, and `../profiling/bench_cpp.cc`.

## Design

- **State.** `CoupState` holds the C `Game` (56-byte POD), a compact per-action event log used to render information states, and a `CoupObsTracker` from `c_engine/coup_obs.h`. The log entries are the C search API's `CoupLogEvent`s (`LoggedEvent` is an alias), recorded by `coup_event_make()` from `c_engine/coup_search.h`. `Clone()` is a plain copy.
- **Chance.** `game_init(g, n, 0, 0)` leaves the game in `PHASE_DEAL`, so the 2n deal draws are explicit chance nodes. Challenge redraws and the two exchange draws are chance nodes too. The chance outcome is the card type (0-4), weighted by the deck counts from `chance_outcomes()`. Decisions go through `step_deterministic`. The adapter never calls `step_with_rng`, and a test checks that the RNG embedded in `Game` is never advanced.
- **Actions.** OpenSpiel actions are the engine's absolute actions 0-31. `coup_obs.h` uses relative target indices, but only inside the observation tensor.
- **Information state** (`InformationStateString(p)`). It has perfect recall and leaks nothing. It contains:
  - the seat id and player count;
  - p's own dealt cards, redraws and exchange draws, while other players' draws appear only as `?` tokens (the recipient is public);
  - every public decision in order;
  - challenge outcomes (`[won]`/`[lost]`) and the face-up card of every lost influence;
  - p's own exchange-discard picks, with other players' picks shown as `ExchDiscard?`.
- **Observation tensor.** This is `coup_obs_write` (uint8 cast to float), size `COUP_OBS_SIZE` (587), the same features PufferLib trains on. It keeps only the last 8 events.
- **Information-state tensor** (860 floats, for Deep CFR / NFSP / R-NaD / `rl_environment`). It is the observation tensor plus whole-game counters per relative seat (role claims, blocks, challenges made/won/lost, roles proven, roles caught bluffing) and the cards p returned to the deck in their own exchanges. It is computed from the same facts as the string, so it is a function of the infostate, but it is a fixed-size summary rather than an injective encoding. Tabular algorithms key on the string.
- **Resampling** (`ResampleFromInfostate(p, rng)`, used by IS-MCTS and other determinizing search). It returns a full history that p cannot tell apart from the real one: own cards and public actions are kept, opponents' hidden draws and exchange picks are resampled, and every public revelation (lost cards, challenge outcomes, redraw slots) replays exactly.
  - The sampler is C code, `coup_resample()` in `c_engine/coup_search.c`, so search code can use it without OpenSpiel. The algorithm is documented in that file. The adapter passes it the event log and the OpenSpiel `rng`, replays the returned actions into a fresh `CoupState`, and checks that the infostate string matches.
  - Cards are modelled as tokens with role constraints. A lookahead pass plus local repairs keep retries rare, about 1.0-1.2 attempts per call.
  - It takes about 4.3 µs per call end to end, including the replay and the string check; the old C++ sampler took about 14 µs. The C core alone takes about 1.6 µs.
  - The distribution approximates the chance-weighted posterior and matches it exactly right after the deal.
- **Returns.** The winner gets +1 and each other player gets -1/(n-1), so the game is zero-sum for any n (`kZeroSum`, `UtilitySum() = 0`, utility range [-1/(n-1), 1]). A game that hits MAX_TURNS is decided by the C engine's `get_winner()` tiebreak.
- **Bounds.**
  - `MaxGameLength() = MAX_TURNS * (3n + 3)` decisions. Per turn that covers 1 main action, three response windows of at most n-1 decisions each, 3 lose-card decisions and 2 exchange picks.
  - `MaxChanceNodesInHistory() = 2n + 3 * MAX_TURNS`.
  - `MaxChanceOutcomes() = 5`.
- **Params.** `players` (2-6, default 2) and `refund_on_challenge` (default true; maps to `game_set_refund_on_challenge`).
- **Undo.** `UndoAction` replays the history prefix, because the engine has no inverse step. It exists for tests and tools only.

## Tests (`coup_game_test`)

- OpenSpiel `RandomSimTest` for 2-6 players, with and without the refund rule. It covers legal-action masks, clone, serialize/deserialize, observation shapes, utility bounds and MaxGameLength.
- No-leak test: two worlds that differ only in opponents' hidden cards, exchange draws and exchange picks (and, separately, in a challenge redraw) must give the observer identical infostate strings, observation tensors and observation strings.
- Public revelations: lost cards and challenge outcomes must change every player's infostate.
- Zero-sum returns over random games, and a forced MAX_TURNS tiebreak game.
- Exhaustive depth-limited, CFR-style tree traversal: within each infoset the legal actions must match, and every player's observation must be a function of their infostate.
- Undo/clone round trips, and a check that the RNG is never used.
- Outcome-sampling MCCFR smoke run (300 iterations, 2 players).
- Information-state tensor: shape, a hand-checked example, no-leak, and (in the traversal) a function of the infostate string.
- Resampling: OpenSpiel's `ResampleInfostateTest` for 2-6 players; resampled states play on to terminal; the post-deal marginal matches the exact unseen-card odds; caught bluffs and revealed cards are always respected.
