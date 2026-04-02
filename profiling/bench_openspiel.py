#!/usr/bin/env python3
"""OpenSpiel/pyspiel benchmark measuring games/second.

Usage:
    python profiling/bench_openspiel.py [--players 2|6] [--policy random|heuristic]
                                        [--threads 1] [--duration 10]
"""
import argparse
import sys
import time
import random
import multiprocessing


# Action priority for heuristic (highest priority first)
# Coup > Assassinate > Tax > Steal > Exchange > Foreign Aid > Income
# Block actions > Pass; Never challenge
HEURISTIC_PRIORITY = (
    # Coup targets (4-9)
    list(range(4, 10)) +
    # Assassinate targets (16-21)
    list(range(16, 22)) +
    # Tax (2)
    [2] +
    # Steal targets (10-15)
    list(range(10, 16)) +
    # Exchange (3)
    [3] +
    # Foreign Aid (1)
    [1] +
    # Block actions (24-27)
    list(range(24, 28)) +
    # Discard slots (28-31) — prefer lower slots
    [28, 29, 30, 31] +
    # Income (0)
    [0] +
    # Pass (23)
    [23]
    # Challenge (22) — never pick
)


def heuristic_pick(legal_actions):
    """Pick the highest-priority legal action."""
    legal_set = set(legal_actions)
    for action in HEURISTIC_PRIORITY:
        if action in legal_set:
            return action
    return legal_actions[0]  # fallback


def run_worker(args):
    """Worker function for multiprocessing."""
    num_players, policy, duration, seed = args

    try:
        import pyspiel
    except ImportError:
        return 0, 0

    rng = random.Random(seed)
    game = pyspiel.load_game(f"coup(players={num_players})")

    games_completed = 0
    total_steps = 0
    start = time.time()

    while time.time() - start < duration:
        state = game.new_initial_state()
        steps = 0
        while not state.is_terminal():
            if state.is_chance_node():
                outcomes = state.chance_outcomes()
                actions, probs = zip(*outcomes)
                action = rng.choices(actions, weights=probs)[0]
            else:
                legal = state.legal_actions()
                if policy == "heuristic":
                    action = heuristic_pick(legal)
                else:
                    action = rng.choice(legal)
            state.apply_action(action)
            steps += 1
        games_completed += 1
        total_steps += steps

    return games_completed, total_steps


def main():
    parser = argparse.ArgumentParser(description="OpenSpiel Coup benchmark")
    parser.add_argument("--players", type=int, default=2, choices=[2, 3, 4, 5, 6])
    parser.add_argument("--policy", default="random", choices=["random", "heuristic"])
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--duration", type=float, default=10.0)
    args = parser.parse_args()

    try:
        import pyspiel
    except ImportError:
        print("Error: pyspiel not available. Build OpenSpiel first.", file=sys.stderr)
        sys.exit(1)

    num_threads = args.threads
    if num_threads <= 0:
        num_threads = multiprocessing.cpu_count() or 4

    worker_args = [
        (args.players, args.policy, args.duration, i * 12345 + 67890)
        for i in range(num_threads)
    ]

    start = time.time()

    if num_threads == 1:
        total_games, total_steps = run_worker(worker_args[0])
    else:
        with multiprocessing.Pool(num_threads) as pool:
            results = pool.map(run_worker, worker_args)
        total_games = sum(r[0] for r in results)
        total_steps = sum(r[1] for r in results)

    elapsed = time.time() - start
    gps = total_games / elapsed if elapsed > 0 else 0
    avg_len = total_steps / total_games if total_games > 0 else 0

    print(f"engine=openspiel players={args.players} policy={args.policy} "
          f"threads={num_threads} games={total_games} duration={elapsed:.2f} "
          f"gps={gps:.1f} avg_len={avg_len:.1f}")


if __name__ == "__main__":
    main()
