# PufferLib Environment

[PufferLib](https://github.com/PufferAI/PufferLib) integration for RL training. Wraps the C engine as a vectorized Gym environment.

## Build

From the repo root:

```bash
make build-puffer
```

This compiles `binding.c` as a CPython extension.

## Usage

```python
from pufferlib.coup_env import CoupEnv

env = CoupEnv(num_envs=128, num_players=2)
```

## Design

- **Single-agent framing** with player rotation -- each `step()` is one decision by the active player
- **Observation**: 407 floats (game state) + 32 floats (action mask) = 439 total
- **Action space**: Discrete(32), masked via the last 32 observation floats
- Vectorized: runs N parallel game instances in C

## Files

- `binding.c` -- CPython extension wrapping the C engine
- `coup_env.py` -- `PufferEnv` subclass
- `test_perf.py` -- SPS benchmark (target: >1M steps/sec)
