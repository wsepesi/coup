#!/usr/bin/env python3
"""Cross-engine determinism test.

For each seed, runs the C engine with a "first legal action" policy,
recording (player, action) pairs and chance outcomes. Then replays
through the OpenSpiel engine feeding the same chance outcomes and
choosing the same actions. Asserts identical sequences.

Requires both:
  - The C engine Python wrapper (coup_c)
  - pyspiel with the Coup game registered
"""
import sys
import os

DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(DIR)

SEEDS = [42, 123, 999, 0]


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


def run_c_engine(coup_c, seed):
    """Run C engine with first-legal-action policy, return trace.

    Returns a list of (kind, player_or_none, action) tuples where
    kind is 'chance' or 'decision'.
    """
    game = coup_c.new_game(num_players=2, seed=seed)
    trace = []

    while not coup_c.is_terminal(game):
        if coup_c.is_chance_node(game):
            outcomes = coup_c.chance_outcomes(game)
            # Pick the first chance outcome deterministically
            action = outcomes[0][0]
            trace.append(('chance', None, action))
            coup_c.apply_action(game, action)
        else:
            player = coup_c.current_player(game)
            legal = coup_c.legal_actions(game)
            # First legal action policy
            action = legal[0]
            trace.append(('decision', player, action))
            coup_c.apply_action(game, action)

    returns = coup_c.returns(game)
    return trace, returns


def replay_openspiel(pyspiel, trace):
    """Replay a trace through OpenSpiel, verifying action legality.

    For chance nodes, feeds the recorded chance outcome.
    For decision nodes, verifies the action is legal and applies it.
    Returns the OpenSpiel game returns.
    """
    game = pyspiel.load_game("coup(players=2)")
    state = game.new_initial_state()
    trace_idx = 0

    while not state.is_terminal():
        assert trace_idx < len(trace), (
            f"OpenSpiel game still running but trace exhausted at step {trace_idx}"
        )
        kind, player, action = trace[trace_idx]

        if state.is_chance_node():
            assert kind == 'chance', (
                f"Step {trace_idx}: OpenSpiel sees chance node but trace has {kind}"
            )
            state.apply_action(action)
        else:
            assert kind == 'decision', (
                f"Step {trace_idx}: OpenSpiel sees decision node but trace has {kind}"
            )
            os_player = state.current_player()
            assert os_player == player, (
                f"Step {trace_idx}: player mismatch C={player} OS={os_player}"
            )
            legal = state.legal_actions()
            assert action in legal, (
                f"Step {trace_idx}: action {action} not legal in OpenSpiel "
                f"(legal: {legal})"
            )
            state.apply_action(action)

        trace_idx += 1

    assert trace_idx == len(trace), (
        f"OpenSpiel game ended at step {trace_idx} but trace has {len(trace)} entries"
    )
    return state.returns()


def test_cross_determinism():
    result = try_import_engines()
    if result is None:
        sys.exit(0)

    coup_c, pyspiel = result

    for seed in SEEDS:
        print(f"Seed {seed}:")

        # Run C engine, collect trace
        c_trace, c_returns = run_c_engine(coup_c, seed)
        print(f"  C engine: {len(c_trace)} steps, returns={c_returns}")

        # Replay through OpenSpiel
        os_returns = replay_openspiel(pyspiel, c_trace)
        print(f"  OpenSpiel: returns={os_returns}")

        # Compare returns
        assert len(c_returns) == len(os_returns), (
            f"Seed {seed}: return length mismatch"
        )
        for p, (cr, osr) in enumerate(zip(c_returns, os_returns)):
            assert cr == osr, (
                f"Seed {seed}: player {p} return mismatch C={cr} OS={osr}"
            )

        print(f"  PASS (identical sequence, {len(c_trace)} steps)")

    print(f"\nAll {len(SEEDS)} seeds passed cross-engine determinism check.")


if __name__ == '__main__':
    test_cross_determinism()
