"""
coup_env.py — PufferLib environment wrapper for the Coup C engine.

Single-agent framing with player rotation: each step is one decision
by the active player. Many env instances run in parallel via PufferLib's
vectorization. All seats share one policy.

The observation is OBS_SIZE (407) floats from the engine + 32 floats
for the action validity mask = 439 total.
"""

import gymnasium
import numpy as np
import pufferlib

import coup_binding as binding

# Engine observation (407) + action mask (32)
OBS_SIZE = 407 + 32


class Coup(pufferlib.PufferEnv):
    """PufferLib environment for the Coup card game."""

    def __init__(self, num_envs=1, buf=None, seed=0, num_players=6):
        self.single_observation_space = gymnasium.spaces.Box(
            low=0.0, high=1.0, shape=(OBS_SIZE,), dtype=np.float32
        )
        self.single_action_space = gymnasium.spaces.Discrete(32)
        self.num_agents = num_envs
        self.num_players = num_players
        self.seed_val = seed
        super().__init__(buf)
        binding.vec_init(
            self.observations,
            self.actions,
            self.rewards,
            self.terminals,
            self.truncations,
            num_envs,
            seed,
            num_players,
        )

    def step(self):
        binding.vec_step()
        info = self._log()
        return (
            self.observations,
            self.rewards,
            self.terminals,
            self.truncations,
            info,
        )

    def reset(self, seed=None):
        # Initial state is already written by vec_init.
        # Auto-reset on terminal is handled inside vec_step.
        return self.observations, {}

    def close(self):
        binding.vec_close()

    def render(self):
        pass

    def _log(self):
        """Retrieve and reset episode statistics from the C layer."""
        log = binding.vec_log()
        if log.get("episode_return", 0.0) > 0.0:
            return log
        return {}
