"""pyspiel game API + C++ algorithms on Coup (no torch/jax needed)."""
import random

import numpy as np
import pytest

import pyspiel
from open_spiel.python.algorithms import outcome_sampling_mccfr as py_os_mccfr

from coup_helpers import has_infostate_tensor, resample_supported


def random_playout(state, rng):
  while not state.is_terminal():
    if state.is_chance_node():
      actions, probs = zip(*state.chance_outcomes())
      state.apply_action(rng.choices(actions, probs)[0])
    else:
      state.apply_action(rng.choice(state.legal_actions()))
  return state


@pytest.mark.parametrize("players", [2, 3, 4, 5, 6])
def test_load_and_random_playouts(players):
  game = pyspiel.load_game("coup", {"players": players})
  assert game.num_players() == players
  assert game.num_distinct_actions() == 32
  obs_shape = game.observation_tensor_shape()
  assert len(obs_shape) == 1 and obs_shape[0] > 0
  if has_infostate_tensor(game):
    assert len(game.information_state_tensor_shape()) == 1
  rng = random.Random(players)
  for _ in range(20):
    state = game.new_initial_state()
    # Spot-check tensor sizes at the first decision node.
    while state.is_chance_node():
      actions, probs = zip(*state.chance_outcomes())
      state.apply_action(rng.choices(actions, probs)[0])
    for p in range(players):
      assert len(state.observation_tensor(p)) == obs_shape[0]
      if has_infostate_tensor(game):
        assert (len(state.information_state_tensor(p)) ==
                game.information_state_tensor_shape()[0])
    random_playout(state, rng)
    returns = state.returns()
    assert abs(sum(returns)) < 1e-9
    assert max(returns) == pytest.approx(1.0)


def test_pickle_roundtrip(coup2):
  import pickle
  state = random_playout(coup2.new_initial_state(), random.Random(1))
  assert str(pickle.loads(pickle.dumps(state))) == str(state)


def test_outcome_sampling_mccfr_cpp(coup2):
  solver = pyspiel.OutcomeSamplingMCCFRSolver(coup2, seed=1)
  for _ in range(300):
    solver.run_iteration()
  policy = solver.average_policy()
  state = coup2.new_initial_state()
  while state.is_chance_node():
    state.apply_action(state.chance_outcomes()[0][0])
  probs = policy.get_state_policy(state)
  assert probs and sum(p for _, p in probs) == pytest.approx(1.0)


def test_outcome_sampling_mccfr_python(coup2):
  solver = py_os_mccfr.OutcomeSamplingSolver(coup2)
  for _ in range(50):
    solver.iteration()
  assert solver.average_policy() is not None


@pytest.mark.skip(reason=(
    "External sampling explores every traverser action at every traverser "
    "node; one iteration on 2p Coup takes > 1 min (tree too deep)."))
def test_external_sampling_mccfr_cpp(coup2):
  solver = pyspiel.ExternalSamplingMCCFRSolver(coup2, seed=1)
  solver.run_iteration()


def test_ismcts_vs_random(coup2):
  ok, why = resample_supported(coup2)
  if not ok:
    pytest.skip(why)
  evaluator = pyspiel.RandomRolloutEvaluator(1, 1)
  bots = [
      pyspiel.ISMCTSBot(seed=1, evaluator=evaluator, uct_c=2.0,
                        max_simulations=50),
      pyspiel.make_uniform_random_bot(1, 2),
  ]
  returns = pyspiel.evaluate_bots(coup2.new_initial_state(), bots, 3)
  assert abs(sum(returns)) < 1e-9


def test_python_ismcts_vs_random(coup2):
  ok, why = resample_supported(coup2)
  if not ok:
    pytest.skip(why)
  from open_spiel.python.algorithms import ismcts, mcts
  bot = ismcts.ISMCTSBot(
      game=coup2, evaluator=mcts.RandomRolloutEvaluator(1, np.random.RandomState(0)),
      uct_c=2.0, max_simulations=30, random_state=np.random.RandomState(0))
  rng = random.Random(2)
  state = coup2.new_initial_state()
  while not state.is_terminal():
    if state.is_chance_node():
      actions, probs = zip(*state.chance_outcomes())
      state.apply_action(rng.choices(actions, probs)[0])
    elif state.current_player() == 0:
      state.apply_action(bot.step(state))
    else:
      state.apply_action(rng.choice(state.legal_actions()))
  assert abs(sum(state.returns())) < 1e-9
