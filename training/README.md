# Training

Hello-world training scripts for both frameworks. These prove the pipeline works end-to-end but are not tuned for real training runs.

## Scripts

### PufferLib PPO (self-play)

```bash
uv run puffer_hello.py
```

Runs PPO self-play with a simple MLP policy (3-layer, 256 hidden) and action masking. Requires the PufferLib extension to be built first (`make build-puffer`).

### OpenSpiel MCCFR

```bash
uv run openspiel_hello.py
```

Runs external sampling MCCFR on 2-player Coup. Requires the C++ engine built and registered with OpenSpiel (`make test-cpp`).
