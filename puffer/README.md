# Coup env for PufferLib 5.0

`coup.h` is a PufferLib 5.0 ocean env (one C header, no Python package).
`install.sh` links it, the c_engine sources and `coup.ini` into the
`lib/PufferLib` submodule (branch `5.0`), where PufferLib's `build.sh` compiles
the CUDA trainer (`./puffer`) or a CPU eval binary (`./coup`).

## Files

- `coup.h`: the env. Includes `coup_core.c`, `heuristic.h` and `coup_obs.h`.
- `coup.ini`: the env's config (it becomes `lib/PufferLib/config/coup.ini`).
- `install.sh`: idempotent symlinks plus submodule `info/exclude` entries.
- `test_coup_env.c`: invariant tests and a throughput benchmark (`make test-puffer`,
  `make bench-puffer`).

## Env design

- One Env is one table of `num_players` (2-6, or `0` for a random size per env).
  Every learning seat is an Agent on every step. The seat to act gets its legal
  actions as the mask. Every other seat gets PASS (23) as its only legal action,
  so its row has zero entropy and zero policy gradient. Each seat still observes
  every public event, so the recurrent policy integrates the whole game from
  its own seat.
- Observations are `uint8`, `COUP_OBS_SIZE` = 587, egocentric (`c_engine/coup_obs.h`).
  Actions are in relative space: the target index means "k seats to my left".
  Only the active seat's action is applied. An action outside the mask is
  replaced by a random legal action and counted in `env/invalid_rate`.
- Rewards are 0 during play. When the game ends, every seat gets its reward on
  the same step it becomes terminal: +1 for the winner and -1/(n-1) for each
  loser. The env then resets itself. `[env] reward_influence` (default 0) adds
  -coef for each card a seat loses.
- Seats are rotated randomly each game, so agent 0 has no first-player edge.
- Seats from `num_agents` onward are bots that act inside `puf_step`:
  `bot_policy` is a `c_engine/heuristic.h` level: 0 = random, 1 = honest (never
  bluffs or challenges), 2 = counting (honest, and challenges provable bluffs).
- Selfplay: agent 0 is policy 0. In the `hist_policy_percent` tail of envs,
  every other seat is a frozen checkpoint. The trainer sets these slots from
  `agents[s].policy`, the same way chess does.
- `my_vec_init` plans each buffer's tables so their agents add up to exactly
  `total_agents / num_buffers`, with the same layout in every buffer. This
  matters for mixed table sizes.

Logged under `env/`:

| Metric | Meaning |
|---|---|
| `perf`, `score`, `policy_0_score` | Agent 0 win rate |
| `win_over_chance` | Mean of win × players; 1.0 is chance level |
| `hist_win_rate`, `hist_frac` | Agent 0 win rate vs frozen checkpoints, and the share of games played against them |
| `episode_return` | Agent 0 return |
| `episode_length` | Env steps per game |
| `turns` | Main actions per game |
| `timeout_rate` | Share of games decided by the `MAX_TURNS` tiebreak |
| `num_players` | Average table size |
| `invalid_rate`, `challenge_rate`, `bluff_rate`, `claims_per_game` | Behaviour of the learning agents |
| `draw_rate` | Always 0. PufferLib's match mode reads it. |

## Local checks (macOS or Linux, no GPU)

```bash
make test-puffer     # installs, builds the CPU eval binary (downloads raylib), runs invariants
make bench-puffer    # single-thread agent-steps/sec
make puffer-cpu      # just build lib/PufferLib/coup
cd lib/PufferLib && ./coup --headless --base.eval_episodes=10000   # untrained smoke run
cd lib/PufferLib && ./coup                                         # windowed spectator view
```

On macOS, PufferLib's `build.sh` needs bash 4 or newer (it uses `${ENV^^}`)
and OpenMP: run `brew install bash libomp`. The Makefile already invokes
Homebrew's bash. If you run `build.sh` by hand, use
`/opt/homebrew/bin/bash ./build.sh coup --cpu`.

## Training on a DGX (Linux + CUDA)

Requirements: CUDA toolkit with `nvcc` on `PATH` (or `CUDA_HOME` set), NCCL,
`clang`, `ccache` and libomp. `build.sh` calls `ccache nvcc` unconditionally,
so install ccache even if you don't want caching.

```bash
sudo apt install -y clang ccache libomp-dev        # if not already present
git clone --recurse-submodules <this repo> coup && cd coup
# existing checkout instead: git submodule update --init lib/PufferLib
./puffer/install.sh
cd lib/PufferLib
./build.sh coup                       # -> ./puffer (CUDA trainer, coup compiled in)
./puffer train                        # uses config/coup.ini (= puffer/coup.ini)
```

Override any config key from the CLI, for example:

```bash
./puffer train --env.num_players=2 --vec.total_agents=16384 --train.total_timesteps=1e9
./puffer train --train.gpus=8         # NCCL data-parallel on one node
```

Checkpoints go to `checkpoints/coup/...` and logs to `logs/`.

Evaluation:

```bash
# Agent 0 = checkpoint, the other seats = bots (perf = win rate vs the bot)
./puffer eval <ckpt.bin> --headless --base.eval_episodes=10000 \
    --env.num_agents=1 --env.bot_policy=2 --env.num_players=4 --vec.num_policies=1
# Head-to-head: agent 0 = A, every other seat = B (score = A's win rate)
./puffer match --headless --base.load_model_path=<A.bin> --base.load_enemy_model_path=<B.bin>
# CPU spectator view of a checkpoint (on any machine, after `make puffer-cpu`)
./coup <ckpt.bin>        # or ./coup latest
```

`[selfplay] eval_bot_games` / `eval_bots = 0, 1, 2` run the bot ladder after
training. It forces `num_agents = 1` and reports the mean `env/perf` across the
rungs as the run's final score, which is the metric to sweep on. In pure
selfplay, `perf` stays near 1/num_players by symmetry. Watch `hist_win_rate`
during training instead.

Notes and risks:

- Sample cost: each env step produces one agent row per seat, but only one of
  those rows is a real decision. With mixed tables about 1 row in 4 is real.
  Count `total_timesteps` with that in mind, or train `num_players=2` first.
- Seats 1..n-1 in a historical env all use the same frozen bank. With
  `num_policies > 2`, banks are spread across envs.
- The bot ladder is only three rungs (random, honest, counting). None of the
  bots bluff, and they rarely or never challenge, so a policy that bluffs
  freely can beat them without being strong. Use selfplay `hist_win_rate` and
  `match` as well.
- The CUDA build has not been run, because no GPU was available while this
  env was written. The header compiles cleanly as C++17 (`clang++
  -fsyntax-only`), which is how nvcc treats it, and it touches only the Env
  fields the trainer reads (`log`, `agents`, `tag`, `boundary_reached`,
  `num_agents`, `rng`).
