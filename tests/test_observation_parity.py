#!/usr/bin/env python3
"""Observation tensor parity test.

Plays a game simultaneously in both the C engine and OpenSpiel engine
with identical actions and chance outcomes. At each player decision
point, compares observation tensors from both engines element-by-element
within a floating-point epsilon.

Requires both:
  - The C engine Python wrapper (coup_c)
  - pyspiel with the Coup game registered
"""
import sys
import os
import math

DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(DIR)

SEEDS = [42, 123, 999, 0]
EPSILON = 1e-6


def try_import_engines():
    """Attempt to import both engines, returning (coup_c, pyspiel) or None."""
    try:
        import pyspiel
    except ImportError:
        print("SKIP: pyspiel not available (build OpenSpiel first)")
        return None

    try:
        sys.path.insert(0, os.path.join(ROOT, 'c_engine'))
        import coup_c
    except ImportError:
        print("SKIP: coup_c not available (build C engine Python wrapper first)")
        return None

    return coup_c, pyspiel


def tensors_close(t1, t2, eps=EPSILON):
    """Compare two flat float sequences element-wise within epsilon.

    Returns (match, first_mismatch_index, val1, val2) where match is
    True if all elements are within eps.
    """
    if len(t1) != len(t2):
        return False, -1, len(t1), len(t2)
    for i, (a, b) in enumerate(zip(t1, t2)):
        if not math.isclose(a, b, abs_tol=eps):
            return False, i, a, b
    return True, -1, None, None


def play_parallel_game(coup_c, pyspiel, seed):
    """Play one game in both engines simultaneously, comparing observations.

    Uses first-legal-action policy for determinism.
    Returns (num_steps, num_comparisons).
    """
    # Set up C engine
    c_game = coup_c.new_game(num_players=2, seed=seed)

    # Set up OpenSpiel
    os_game = pyspiel.load_game("coup(players=2)")
    os_state = os_game.new_initial_state()

    steps = 0
    obs_comparisons = 0

    while not coup_c.is_terminal(c_game) and not os_state.is_terminal():
        c_is_chance = coup_c.is_chance_node(c_game)
        os_is_chance = os_state.is_chance_node()
        assert c_is_chance == os_is_chance, (
            f"Step {steps}: chance node mismatch C={c_is_chance} OS={os_is_chance}"
        )

        if c_is_chance:
            # Use first chance outcome from C engine
            outcomes = coup_c.chance_outcomes(c_game)
            action = outcomes[0][0]
            coup_c.apply_action(c_game, action)
            os_state.apply_action(action)
        else:
            # Decision node: compare observations before choosing action
            c_player = coup_c.current_player(c_game)
            os_player = os_state.current_player()
            assert c_player == os_player, (
                f"Step {steps}: player mismatch C={c_player} OS={os_player}"
            )

            # Get observation tensors from both engines
            c_obs = coup_c.observation_tensor(c_game, c_player)
            os_obs = os_state.observation_tensor(os_player)

            match, idx, v1, v2 = tensors_close(c_obs, os_obs)
            if not match:
                if idx == -1:
                    raise AssertionError(
                        f"Step {steps}, player {c_player}: observation tensor "
                        f"length mismatch C={v1} OS={v2}"
                    )
                else:
                    raise AssertionError(
                        f"Step {steps}, player {c_player}: observation tensor "
                        f"mismatch at index {idx}: C={v1} OS={v2} "
                        f"(eps={EPSILON})"
                    )
            obs_comparisons += 1

            # First legal action policy
            c_legal = coup_c.legal_actions(c_game)
            action = c_legal[0]
            coup_c.apply_action(c_game, action)
            os_state.apply_action(action)

        steps += 1

    # Both should be terminal
    assert coup_c.is_terminal(c_game) and os_state.is_terminal(), (
        f"Terminal state mismatch at step {steps}"
    )

    # Compare final returns
    c_returns = coup_c.returns(c_game)
    os_returns = os_state.returns()
    for p, (cr, osr) in enumerate(zip(c_returns, os_returns)):
        assert math.isclose(cr, osr, abs_tol=EPSILON), (
            f"Return mismatch player {p}: C={cr} OS={osr}"
        )

    return steps, obs_comparisons


def test_observation_parity():
    result = try_import_engines()
    if result is None:
        sys.exit(0)

    coup_c, pyspiel = result
    total_comparisons = 0

    for seed in SEEDS:
        print(f"Seed {seed}:")
        steps, comparisons = play_parallel_game(coup_c, pyspiel, seed)
        total_comparisons += comparisons
        print(f"  {steps} steps, {comparisons} observation comparisons — PASS")

    print(f"\nAll {len(SEEDS)} seeds passed observation parity check "
          f"({total_comparisons} total tensor comparisons, eps={EPSILON}).")


if __name__ == '__main__':
    test_observation_parity()
