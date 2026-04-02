#!/usr/bin/env python3
"""PufferLib Coup environment performance benchmark."""
import time
import numpy as np


def main():
    try:
        from coup_env import Coup
    except ImportError:
        # Try alternate import paths
        import sys, os
        sys.path.insert(0, os.path.dirname(__file__))
        from coup_env import Coup

    num_envs = 1000
    num_players = 6

    print(f"Creating {num_envs} parallel Coup environments ({num_players} players)...")
    env = Coup(num_envs=num_envs, num_players=num_players)
    obs, info = env.reset()

    print("Benchmarking...")
    total_steps = 0
    duration = 10.0  # seconds

    start = time.time()
    while time.time() - start < duration:
        # Random actions
        actions = np.random.randint(0, 32, size=num_envs)
        # Write actions into the shared buffer
        env.actions[:] = actions
        obs, rewards, terminals, truncations, info = env.step()
        total_steps += num_envs

    elapsed = time.time() - start
    sps = total_steps / elapsed

    print(f"Total steps: {total_steps:,}")
    print(f"Elapsed: {elapsed:.2f}s")
    print(f"Steps/second: {sps:,.0f}")
    print(f"Target: >1,000,000 SPS")
    print(f"{'PASS' if sps > 1_000_000 else 'BELOW TARGET'}")

    env.close()


if __name__ == '__main__':
    main()
