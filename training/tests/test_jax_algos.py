"""OpenSpiel JAX algorithms, PSRO and the vendored R-NaD on 2p Coup (tiny runs)."""
import os
import sys

import numpy as np
import pytest

jax = pytest.importorskip("jax")

import pyspiel  # noqa: E402
from open_spiel.python import rl_environment  # noqa: E402

from coup_helpers import has_infostate_tensor  # noqa: E402

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", ".."))

N_EPISODES = 10


def run_selfplay(env, agents, episodes=N_EPISODES):
  for _ in range(episodes):
    time_step = env.reset()
    while not time_step.last():
      player = time_step.observations["current_player"]
      time_step = env.step([agents[player].step(time_step).action])
    for agent in agents:
      agent.step(time_step)
    assert abs(sum(time_step.rewards)) < 1e-9


@pytest.fixture
def env():
  e = rl_environment.Environment("coup")
  e.seed(0)
  return e


def test_jax_dqn(env):
  from open_spiel.python.jax import dqn
  size = env.observation_spec()["info_state"][0]
  agents = [
      dqn.DQN(p, state_representation_size=size, num_actions=32,
              hidden_layers_sizes=[16], replay_buffer_capacity=100,
              min_buffer_size_to_learn=8, batch_size=8, learn_every=4,
              allow_checkpointing=False)
      for p in range(2)
  ]
  run_selfplay(env, agents)


def test_jax_nfsp(env):
  from open_spiel.python.jax import nfsp

  class GuardedNFSP(nfsp.NFSP):
    # Upstream bug (jax/nfsp.py _learn): len(None) if a learn step fires
    # before any best-response-mode step has created the reservoir buffer.
    def _learn(self):
      if self._reservoir_buffer is None:
        return None
      return super()._learn()

  size = env.observation_spec()["info_state"][0]
  agents = [
      GuardedNFSP(p, state_representation_size=size, num_actions=32,
                  hidden_layers_sizes=[16], reservoir_buffer_capacity=100,
                  anticipatory_param=0.5, min_buffer_size_to_learn=8,
                  batch_size=8, learn_every=4, allow_checkpointing=False)
      for p in range(2)
  ]
  run_selfplay(env, agents)


def test_jax_policy_gradient(env):
  from open_spiel.python.jax import policy_gradient
  size = env.observation_spec()["info_state"][0]
  agents = [
      policy_gradient.PolicyGradient(
          player_id=p, info_state_size=size, num_actions=32, loss_str="a2c",
          hidden_layers_sizes=[16], num_critic_before_pi=2)
      for p in range(2)
  ]
  run_selfplay(env, agents)


@pytest.mark.parametrize("representation", ["OBSERVATION", "INFO_SET"])
def test_rnad(coup2, representation):
  from training.rnad import rnad
  if representation == "INFO_SET" and not has_infostate_tensor(coup2):
    pytest.skip("coup does not provide information_state_tensor yet")
  config = rnad.RNaDConfig(
      game_name="coup",
      state_representation=getattr(rnad.StateRepresentation, representation),
      trajectory_max=64,  # covers (almost) every 2p game; see rnad.py note
      batch_size=4,
      policy_network_layers=(16,),
      entropy_schedule_size=(10,))
  solver = rnad.RNaDSolver(config)
  for _ in range(2):
    solver.step()
  state = coup2.new_initial_state()
  while state.is_chance_node():
    state.apply_action(state.chance_outcomes()[0][0])
  probs = solver.action_probabilities(state)
  assert sum(probs.values()) == pytest.approx(1.0, abs=1e-4)


def test_psro_rl_oracle(env):
  """PSRO with a policy-gradient RL oracle (BR oracle needs the full tree)."""
  from open_spiel.python.algorithms.psro_v2 import psro_v2
  from open_spiel.python.algorithms.psro_v2 import rl_oracle
  from open_spiel.python.algorithms.psro_v2 import rl_policy
  from open_spiel.python.algorithms.psro_v2 import strategy_selectors
  size = env.observation_spec()["info_state"][0]
  kwargs = dict(info_state_size=size, num_actions=32, loss_str="qpg",
                loss_class=False, hidden_layers_sizes=[16])
  oracle = rl_oracle.RLOracle(env, rl_policy.PGPolicy, kwargs,
                              number_training_episodes=4,
                              self_play_proportion=0.0, sigma=0.0)
  agents = [rl_policy.PGPolicy(env, p, **kwargs) for p in range(2)]
  for agent in agents:
    agent.freeze()
  solver = psro_v2.PSROSolver(
      env.game, oracle, initial_policies=agents, sims_per_entry=4,
      training_strategy_selector=strategy_selectors.probabilistic,
      meta_strategy_method="prd", prd_iterations=100,
      sample_from_marginals=True)
  solver.iteration()
  meta_game = solver.get_meta_game()
  assert np.asarray(meta_game[0]).shape == (2, 2)


def test_jax_deep_cfr_construct(coup2):
  """JAX Deep CFR: same external-sampling traversal blow-up as the torch one."""
  if not has_infostate_tensor(coup2):
    pytest.skip("coup does not provide information_state_tensor yet")
  from open_spiel.python.jax import deep_cfr
  deep_cfr.DeepCFRSolver(
      coup2, policy_network_layers=(8,), advantage_network_layers=(8,),
      num_iterations=1, num_traversals=1, memory_capacity=100)
