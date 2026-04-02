#!/usr/bin/env python3
"""PufferLib benchmark measuring games/second.

Uses the coup_binding C extension directly (no gymnasium/pufferlib dependency)
to measure raw vectorized environment throughput.

Usage:
    python profiling/bench_pufferlib.py [--players 2|6] [--policy random|heuristic]
                                        [--num-envs 1000] [--duration 10]
"""
import argparse
import sys
import os
import time

import numpy as np


def main():
    parser = argparse.ArgumentParser(description="PufferLib Coup benchmark")
    parser.add_argument("--players", type=int, default=2, choices=[2, 3, 4, 5, 6])
    parser.add_argument("--policy", default="random", choices=["random", "heuristic"])
    parser.add_argument("--num-envs", type=int, default=1000)
    parser.add_argument("--duration", type=float, default=10.0)
    args = parser.parse_args()

    # Import the binding directly (avoid gymnasium dependency)
    # The .so lives in the project root after build_ext --inplace
    root_dir = os.path.join(os.path.dirname(__file__), "..")
    sys.path.insert(0, os.path.abspath(root_dir))
    try:
        import coup_binding as binding
    except ImportError:
        print("Error: coup_binding not found. Run: make build-puffer", file=sys.stderr)
        sys.exit(1)

    num_envs = args.num_envs
    obs_size = 407 + 32  # engine obs + action mask

    # Allocate numpy buffers directly
    observations = np.zeros((num_envs, obs_size), dtype=np.float32)
    actions = np.zeros(num_envs, dtype=np.int32)
    rewards = np.zeros(num_envs, dtype=np.float32)
    terminals = np.zeros(num_envs, dtype=np.uint8)
    truncations = np.zeros(num_envs, dtype=np.uint8)

    binding.vec_init(observations, actions, rewards, terminals, truncations,
                     num_envs, 42, args.players)

    use_heuristic = args.policy == "heuristic"
    total_games = 0
    total_steps = 0

    # Realistic training loop simulation:
    # 1. Previous vec_step generated observations (already done by vec_init)
    # 2. "Policy" produces actions (C-side random or heuristic)
    # 3. vec_step steps the game AND generates next observations
    # This matches what real RL training does: obs → policy → step → obs → ...
    start = time.time()
    while time.time() - start < args.duration:
        if use_heuristic:
            binding.vec_step_heuristic()
        else:
            binding.vec_fill_random_actions()
            binding.vec_step()

        total_steps += num_envs
        total_games += int(terminals.sum())

    elapsed = time.time() - start
    gps = total_games / elapsed if elapsed > 0 else 0
    avg_len = total_steps / total_games if total_games > 0 else 0

    print(f"engine=pufferlib players={args.players} policy={args.policy} "
          f"threads=1 games={total_games} duration={elapsed:.2f} "
          f"gps={gps:.1f} avg_len={avg_len:.1f} num_envs={num_envs}")

    binding.vec_close()


if __name__ == "__main__":
    main()
