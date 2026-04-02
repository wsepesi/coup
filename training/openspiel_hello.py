#!/usr/bin/env python3
"""Minimal OpenSpiel hello world for Coup.

Demonstrates loading the game, running random playouts, and
basic game-theoretic analysis. NOT a real training run.

Requires: OpenSpiel built and pyspiel importable.
"""
import sys


def main():
    try:
        import pyspiel
    except ImportError:
        print("pyspiel not available. Build OpenSpiel first:")
        print("  cd cpp_engine/build && cmake .. && make")
        print("  # Then add to PYTHONPATH")
        sys.exit(1)

    # Load the game
    print("Loading Coup (2-player)...")
    game = pyspiel.load_game("coup(players=2)")
    print(f"  Game: {game.get_type().long_name}")
    print(f"  Players: {game.num_players()}")
    print(f"  Actions: {game.num_distinct_actions()}")
    print(f"  Max game length: {game.max_game_length()}")

    # Run a random playout
    print("\nRandom playout:")
    import random
    state = game.new_initial_state()
    steps = 0
    while not state.is_terminal():
        if state.is_chance_node():
            outcomes = state.chance_outcomes()
            actions, probs = zip(*outcomes)
            action = random.choices(actions, weights=probs)[0]
            state.apply_action(action)
        else:
            player = state.current_player()
            legal = state.legal_actions()
            action = random.choice(legal)
            print(f"  Player {player}: {state.action_to_string(player, action)}")
            state.apply_action(action)
        steps += 1

    print(f"\nGame ended in {steps} steps")
    print(f"Returns: {state.returns()}")

    # Run multiple random games
    print("\nRunning 1000 random games...")
    wins = [0] * game.num_players()
    for _ in range(1000):
        state = game.new_initial_state()
        while not state.is_terminal():
            if state.is_chance_node():
                outcomes = state.chance_outcomes()
                actions, probs = zip(*outcomes)
                action = random.choices(actions, weights=probs)[0]
            else:
                action = random.choice(state.legal_actions())
            state.apply_action(action)
        returns = state.returns()
        for p in range(game.num_players()):
            if returns[p] > 0:
                wins[p] += 1

    for p in range(game.num_players()):
        print(f"  Player {p} win rate: {wins[p]/1000:.1%}")

    # Try MCCFR if available
    print("\nAttempting MCCFR (may be slow for full game)...")
    try:
        from open_spiel.python.algorithms import external_sampling_mccfr as mccfr
        solver = mccfr.ExternalSamplingSolver(game)
        print("  Running 100 iterations...")
        for i in range(100):
            solver.iteration()
        print("  MCCFR completed 100 iterations")

        # Try exploitability
        try:
            from open_spiel.python.algorithms import exploitability
            conv = exploitability.exploitability(game, solver.average_policy())
            print(f"  Exploitability: {conv:.6f}")
        except Exception as e:
            print(f"  Exploitability computation failed (expected for large games): {e}")
    except ImportError:
        print("  MCCFR not available — install OpenSpiel Python bindings")
    except Exception as e:
        print(f"  MCCFR failed: {e}")

    # Where to go from here
    print("\n--- Next Steps ---")
    print("# Deep CFR: from open_spiel.python.algorithms import deep_cfr")
    print("# Policy export: save average_policy for cross-framework eval")
    print("# Abstraction: reduce info set count for tractable tabular CFR")


if __name__ == '__main__':
    main()
