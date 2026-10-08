"""OpenSpiel PyTorch algorithms (open_spiel/python/pytorch) on 2p Coup.

Tiny runs: they only check the algorithms execute end to end on the game.
"""
import random

import numpy as np
import pytest

torch = pytest.importorskip("torch")

import pyspiel  # noqa: E402
from open_spiel.python import rl_environment  # noqa: E402
from open_spiel.python.vector_env import SyncVectorEnv  # noqa: E402

from coup_helpers import has_infostate_tensor  # noqa: E402

N_EPISODES = 20


def run_selfplay(env, agents, episodes=N_EPISODES):
  for _ in range(episodes):
    time_step = env.reset()
    while not time_step.last():
      player = time_step.observations["current_player"]
      output = agents[player].step(time_step)
      time_step = env.step([output.action])
    for agent in agents:
      agent.step(time_step)
    assert abs(sum(time_step.rewards)) < 1e-9


@pytest.fixture
def env():
  e = rl_environment.Environment("coup")
  e.seed(0)
  return e


def test_rl_environment_spec(env):
  game = env.game
  size = env.observation_spec()["info_state"][0]
  if has_infostate_tensor(game):
    assert size == game.information_state_tensor_shape()[0]
  else:
    assert size == game.observation_tensor_shape()[0]
  assert env.action_spec()["num_actions"] == 32


def test_dqn(env):
  from open_spiel.python.pytorch import dqn
  size = env.observation_spec()["info_state"][0]
  agents = [
      dqn.DQN(p, state_representation_size=size, num_actions=32,
              hidden_layers_sizes=[32], replay_buffer_capacity=1000,
              min_buffer_size_to_learn=16, batch_size=16, learn_every=4)
      for p in range(2)
  ]
  run_selfplay(env, agents)


def test_nfsp(env):
  from open_spiel.python.pytorch import nfsp
  size = env.observation_spec()["info_state"][0]
  agents = [
      nfsp.NFSP(p, state_representation_size=size, num_actions=32,
                hidden_layers_sizes=[32], reservoir_buffer_capacity=1000,
                anticipatory_param=0.1, min_buffer_size_to_learn=16,
                batch_size=16, learn_every=4, replay_buffer_capacity=1000)
      for p in range(2)
  ]
  run_selfplay(env, agents)


@pytest.mark.parametrize("loss_str", ["a2c", "rpg", "qpg", "rm"])
def test_policy_gradient(env, loss_str):
  from open_spiel.python.pytorch import policy_gradient
  size = env.observation_spec()["info_state"][0]
  agents = [
      policy_gradient.PolicyGradient(
          player_id=p, info_state_size=size, num_actions=32,
          loss_str=loss_str, hidden_layers_sizes=[32], batch_size=4,
          num_critic_before_pi=2)
      for p in range(2)
  ]
  run_selfplay(env, agents)


class VsRandomEnv:
  """Single-agent view of 2p Coup for OpenSpiel's PPO.

  OpenSpiel's PPO acts on every time step as one fixed player_id and expects
  one reward per env, so seat 1 is played inside the env by a uniform-random
  policy and rewards are reduced to seat 0's.
  """

  def __init__(self, seed):
    self._env = rl_environment.Environment("coup")
    self._env.seed(seed)
    self._rng = random.Random(seed)

  def _advance(self, ts):
    while not ts.last() and ts.observations["current_player"] != 0:
      legal = ts.observations["legal_actions"][ts.observations["current_player"]]
      ts = self._env.step([self._rng.choice(legal)])
    return ts._replace(rewards=None if ts.rewards is None else [ts.rewards[0]])

  def reset(self):
    return self._advance(self._env.reset())

  def step(self, actions):
    return self._advance(self._env.step(actions))

  def get_time_step(self):
    return self._advance(self._env.get_time_step())

  def observation_spec(self):
    return self._env.observation_spec()

  @property
  def num_players(self):
    return 1


def test_ppo_vs_random():
  from open_spiel.python.pytorch.ppo import PPO, PPOAgent
  envs = SyncVectorEnv([VsRandomEnv(seed) for seed in range(2)])
  size = envs.observation_spec()["info_state"][0]
  steps_per_batch = 16
  agent = PPO(input_shape=(size,), num_actions=32, num_players=1,
              player_id=0, num_envs=len(envs), steps_per_batch=steps_per_batch,
              num_minibatches=2, update_epochs=1, agent_fn=PPOAgent)
  time_step = envs.reset()
  for _ in range(2):
    for _ in range(steps_per_batch):
      out = agent.step(time_step)
      time_step, reward, done, _ = envs.step(out, reset_if_done=True)
      agent.post_step(reward, done)
    agent.learn(time_step)


def test_deep_cfr_construct_and_query(coup2):
  """Deep CFR: networks + policy query only (traversals are infeasible).

  Deep CFR's traversal is external sampling: it expands every traverser
  action at every traverser node. One such traversal of 2p Coup visits
  > 1e8 nodes (measured), so even num_traversals=1 does not finish in a
  smoke test. Constructing the solver still checks the infostate-tensor
  plumbing (input size, legal-action masking).
  """
  if not has_infostate_tensor(coup2):
    pytest.skip("coup does not provide information_state_tensor yet")
  from open_spiel.python.pytorch import deep_cfr
  solver = deep_cfr.DeepCFRSolver(
      coup2, policy_network_layers=(16,), advantage_network_layers=(16,),
      num_iterations=1, num_traversals=1, memory_capacity=100)
  state = coup2.new_initial_state()
  rng = np.random.default_rng(0)
  while state.is_chance_node():
    actions, probs = zip(*state.chance_outcomes())
    state.apply_action(int(rng.choice(actions, p=probs)))
  probs = solver.action_probabilities(state)
  assert set(probs) == set(state.legal_actions())
  # Upstream quirk: the policy net softmaxes over all 32 actions and the
  # result is only sliced to legal ones (not renormalized), so no sum check.
  assert all(p >= 0 for p in probs.values())
