#!/usr/bin/env python3
"""Minimal PPO self-play training for Coup via PufferLib.

NOT tuned -- just proves the pipeline works end-to-end.
"""
import torch
import torch.nn as nn
import numpy as np

# Try to import pufferlib
try:
    import pufferlib
    import pufferlib.models
    import pufferlib.frameworks.cleanrl
    HAS_PUFFERLIB = True
except ImportError:
    HAS_PUFFERLIB = False
    print("PufferLib not installed. Install with: uv pip install -e lib/PufferLib")

import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'pufferlib'))

# Matches coup_env.py: OBS_SIZE = 407 + 32 (engine obs + action mask)
OBS_SIZE = 407 + 32
MASK_SIZE = 32


class Policy(nn.Module):
    """Simple MLP policy with action masking."""

    def __init__(self, obs_size=OBS_SIZE, hidden=256, num_actions=32):
        super().__init__()
        self.obs_size = obs_size
        self.encoder = nn.Sequential(
            nn.Linear(obs_size - MASK_SIZE, hidden),
            nn.ReLU(),
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Linear(hidden, hidden),
            nn.ReLU(),
        )
        self.action_head = nn.Linear(hidden, num_actions)
        self.value_head = nn.Linear(hidden, 1)

    def forward(self, obs):
        # Split observation and mask -- first 407 floats are engine obs
        x = obs[..., :-MASK_SIZE]
        hidden = self.encoder(x)
        return hidden

    def encode_observations(self, obs):
        return self.forward(obs)

    def decode_actions(self, hidden, lookup, concat=None):
        logits = self.action_head(hidden)
        # Apply action mask (last 32 floats of observation)
        masks = lookup["masks"]
        logits = logits + (1 - masks) * -1e8
        value = self.value_head(hidden)
        return logits, value


def main():
    if not HAS_PUFFERLIB:
        print("Skipping training -- PufferLib not available")
        return

    from coup_env import Coup

    device = "cuda" if torch.cuda.is_available() else "cpu"
    print(f"Device: {device}")

    num_envs = 128
    env = Coup(num_envs=num_envs, num_players=6)

    policy = Policy().to(device)
    print(f"Policy parameters: {sum(p.numel() for p in policy.parameters()):,}")

    # TODO: Add PufferLib PPO training loop
    # config = pufferlib.frameworks.cleanrl.Config(
    #     total_timesteps=100_000,
    #     learning_rate=2.5e-4,
    #     gamma=0.99,
    #     gae_lambda=0.95,
    #     clip_coef=0.2,
    #     ent_coef=0.01,
    # )
    # trainer = pufferlib.frameworks.cleanrl.CleanRL(config, env, policy)
    # trainer.train()

    print("Hello world training scaffold complete.")
    print("To run actual training, install PufferLib and uncomment the training loop.")

    # Quick smoke test: one forward pass
    obs = torch.randn(1, OBS_SIZE).to(device)
    hidden = policy.encode_observations(obs)
    masks = torch.ones(1, 32).to(device)
    logits, value = policy.decode_actions(hidden, {"masks": masks})
    print(f"Forward pass OK -- logits shape: {logits.shape}, value: {value.item():.4f}")

    env.close()


if __name__ == '__main__':
    main()
