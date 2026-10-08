# Training

## PufferLib PPO (self-play)

PufferLib 5.0 has no Python package: the trainer is a compiled CLI with the env built in. The env lives in [`puffer/`](../puffer/README.md):

```bash
./puffer/install.sh                       # link env + config into lib/PufferLib
cd lib/PufferLib && ./build.sh coup       # CUDA trainer -> ./puffer (needs nvcc)
./puffer train                            # config: puffer/coup.ini
```

See `puffer/README.md` for DGX steps, selfplay, and evaluation against bots.


## OpenSpiel from Python (all algorithms)

pip `open_spiel` has no `coup`, so `make pyspiel` builds OpenSpiel's own `pyspiel` extension from the `lib/OpenSpiel` submodule with the coup adapter compiled in (CMake option `COUP_BUILD_PYSPIEL=ON` in `cpp_engine/CMakeLists.txt`, build dir `cpp_engine/build-py`, output `cpp_engine/build-py/python/pyspiel.so`). It is built against the uv venv's Python and writes `.venv/lib/python3.12/site-packages/coup_openspiel.pth`, which puts `lib/OpenSpiel` (for `open_spiel.python.*`) and the build dir on `sys.path`. pybind11, pybind11_abseil and abseil are already vendored in the submodule tree; nothing is downloaded.

```bash
uv sync --extra torch --extra jax --extra misc   # Python 3.12 env (root pyproject.toml)
make pyspiel                                     # several minutes the first time; re-run after recreating .venv
uv run python -c "import pyspiel; print(pyspiel.load_game('coup', {'players': 3}))"
make test-py                                     # training/tests, ~20 s
```

Extras: `torch` (open_spiel/python/pytorch), `jax` (open_spiel/python/jax, R-NaD, PSRO RL oracle), `misc` (cvxpy + matplotlib, imported by `lp_solver` / PSRO), `jax-cuda` (Linux + NVIDIA). Plain `uv sync` is exact and drops extras you don't list; `uv run` doesn't.

What runs on Coup (`training/tests/`, 2 players unless noted):

| Algorithm | Status |
| --- | --- |
| Game API, random playouts 2-6p, pickle | works |
| Outcome-sampling MCCFR (C++ `pyspiel.OutcomeSamplingMCCFRSolver`, Python) | works |
| ISMCTS (C++ `pyspiel.ISMCTSBot`, Python `algorithms/ismcts.py`) | works (needs `ResampleFromInfostate`) |
| PyTorch DQN, NFSP, policy gradient (a2c/rpg/qpg/rm) via `rl_environment` | works |
| PyTorch PPO | works vs a random seat 1 (`VsRandomEnv` in the test): OpenSpiel's PPO is single-agent |
| JAX DQN, NFSP, policy gradient | works (jax NFSP needs a guard for an upstream `len(None)` bug, see test) |
| PSRO v2 with RL (policy-gradient) oracle | works; the exact best-response oracle needs the full tree |
| R-NaD | works with `OBSERVATION` and `INFO_SET` representations. It was removed from upstream OpenSpiel, so `training/rnad/rnad.py` is a vendored copy (`from training.rnad import rnad`). Set `trajectory_max` to cover a whole game |
| Deep CFR (torch/jax), ESCHER, external-sampling MCCFR | impractical. They use external-sampling traversals that expand every traverser action, and one 2p traversal is > 1e8 nodes (measured). The tests only construct Deep CFR and query its policy; the ES-MCCFR test is skipped |
| Tabular CFR/CFR+, exact exploitability/best response, sequence-form LP | not feasible: they enumerate the full game tree |

DGX / CUDA: on Linux x86_64 the PyPI `torch==2.9.1` wheel already bundles CUDA 12.8. On aarch64 (Grace / DGX Spark), or for a different CUDA, add a `[[tool.uv.index]]` for `https://download.pytorch.org/whl/cu128` (or cu129/cu130) with `explicit = true` plus `[tool.uv.sources] torch = { index = "pytorch-cu128" }`. For JAX GPU use `uv sync --extra jax --extra jax-cuda` (`jax[cuda12]` pulls the CUDA plugin wheels). Then `make pyspiel` and `make test-py PYEXTRAS="--extra torch --extra jax --extra misc --extra jax-cuda"`. pyspiel itself is CPU C++ and needs only cmake + a C++17 compiler + Python headers (uv's managed Python ships them).

## Scripts

### OpenSpiel MCCFR (hello world, not tuned)

```bash
make mccfr-example                 # from repo root (C++)
uv run openspiel_hello.py [iters]  # wrapper: builds + runs the same example
```

Runs outcome-sampling MCCFR on 2-player Coup through the OpenSpiel adapter in `cpp_engine/` and prints player 0's average opening policy per starting hand. If `make pyspiel` has been run, the script instead does a random playout through pyspiel.
