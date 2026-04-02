# Architecture Notes: Incomplete Info Multi-Agent Games

## Goal

Build environments for two games:
1. **Sequential** hidden-info stochastic game (turn-based)
2. **Simultaneous** hidden-info stochastic game

Train agents with strong play, potentially using game-theoretic algorithms. Evaluate by having trained agents play against each other.

---

## PufferLib Capabilities

### What it provides
- Fast vectorized environment stepping via shared memory buffers
- PettingZoo integration for multi-agent (`PettingZooPufferEnv`)
- Agent padding/masking for variable agent counts
- PPO training loop (default, but not mandatory)
- `Multiprocessing` backend with `RawArray` shared memory + semaphore spin-wait

### Two code paths (important performance distinction)
- **Native `PufferEnv` (C):** Env logic runs in C, numpy buffers passed as raw pointers, thousands of envs stepped in a single C call. This is where millions of SPS come from.
- **Wrapped Gymnasium/PettingZoo:** Pure Python wrappers. Shared buffer allocation + zero-copy recv, but env logic is still Python, stepped one-at-a-time in a loop. No compilation or downconversion. The wrapper is convenience + infrastructure, not a performance transformer.

### Hidden info handling
PufferLib is agnostic -- each agent gets whatever observation you give it. No information set tracking, no belief modeling. Hidden info is just "smaller observation."

---

## Algorithm Options

### 1. PPO + LSTM
- Agent sees `o_t`, LSTM hidden state `h_t` carries history implicitly
- PufferLib supports this natively via `bptt_horizon`
- **Limitation:** Truncated BPTT means gradients only flow through `bptt_horizon` steps. LSTM can carry state further but can't *learn from* longer dependencies. Forgets over long games.
- Good for games where recent history dominates.

### 2. PPO + Full History in State
- Observation encodes entire action-observation history (zero-padded to max length)
- Feedforward network (MLP, CNN, or transformer) over the history
- No vanishing gradient / truncation problem -- full history is in the input
- **Limitation:** Obs size scales with game length. Lots of padding waste.
- Good for short-to-medium games (<100 steps).

### 3. PPO + Transformer (replacing LSTM)
- Like GTrXL: transformer attends over a rolling context window of last K observations
- Attention doesn't forget within the window -- can attend to any past step equally
- **Requires custom training loop** -- PufferLib's PPO is built around LSTM-style `bptt_horizon`. Need to:
  1. Maintain per-agent context buffer of last K (obs, action) pairs
  2. Feed full context window to transformer at each step
  3. Handle rollout batching (each sample is a sequence)
- Use PufferLib for vectorization only, write own training loop on top
- Compute: O(K^2) per step from self-attention. K=200 with small transformer (2-3 layers, 128-dim) is cheap for card/board games.
- **Best option for RL approach** on games with meaningful long-range history.

### 4. Decision Transformer
- Sequence model: `(return-to-go, o_1, a_1, ..., o_t) -> a_t`
- Trained via **supervised learning on offline data**, not PPO
- Needs a trajectory dataset first (chicken-and-egg problem)
- Return conditioning doesn't account for opponent adaptation
- More natural for single-agent / cooperative settings than adversarial.

### 5. Deep CFR
- Game-theoretic: iterates over game tree, maintains regret values per information set
- Neural networks approximate regret tables (simple MLPs on info-state features)
- **Converges to Nash equilibrium** in 2-player zero-sum games
- **Requires game tree traversal** -- needs `legal_actions()`, `child(action)`, `chance_outcomes()`. OpenSpiel provides this; PufferLib's `step()` API is insufficient (CFR needs to branch/explore counterfactuals, not just roll forward).
- Architecture is simple because the algorithm handles strategic reasoning. Network just generalizes across similar information sets.

### 6. ReBeL
- Extends Deep CFR ideas: value network `V(public_state, belief)` + depth-limited search with CFR subgame solving
- Belief tracking via Bayesian updates (computed analytically, not learned)
- Still MLP architectures -- algorithm complexity, not architecture complexity
- Still 2-player zero-sum.

### 7. piKL / Cicero (multiplayer)
- **Not** a natural extension of CFR lineage. Parallel evolution from policy optimization.
- `max_pi [Expected Value] - lambda * KL(pi || pi_0)` -- regularized toward a prior policy
- Cicero (Diplomacy): transformers throughout, supervised pretraining on 50k human games, language model for negotiation, piKL search at decision time
- Required because **CFR's Nash convergence breaks for >2 players**. In multiplayer, CFR converges to coarse correlated equilibria, not Nash.

---

## Key Architectural Pattern

> The more the algorithm handles game-theoretic reasoning explicitly, the simpler the network can be. The more you lean on pure RL, the more you ask of the architecture.

| Approach | Architecture | Algorithm does... |
|---|---|---|
| Deep CFR / ReBeL | MLP | Regret minimization, belief tracking, search |
| Cicero (piKL) | Transformers | Regularized search, but anchored to human data |
| PPO + Transformer | Transformer | Nothing game-theoretic -- network does everything |

---

## Multiplayer Gap

There is a real theoretical gap between 2-player zero-sum and general multiplayer:

- **2p zero-sum:** CFR -> MCCFR -> Deep CFR -> ReBeL. Clean theory, Nash convergence.
- **Multiplayer:** No clean extension. Options are:
  - Run CFR anyway (no guarantees, converges to coarse correlated equilibrium)
  - piKL (anchor to human data, regularize, pragmatic but not principled)
  - Population methods (alpha-Rank, PSRO -- train diverse population, select game-theoretically)

---

## Evaluation: Agents Playing Against Each Other

Simple game loop -- no PufferLib needed at eval time:

```python
env = OpenSpielEnv("game")
state = env.reset()
agents = {
    "player_0": PufferPPOAgent(model_path="..."),
    "player_1": CFRAgent(strategy_path="..."),
}
while not done:
    current_player = env.current_player()
    obs = transform_state(state, current_player)  # -> agent's trained obs format
    action = agents[current_player].act(obs)
    state, rewards, done, info = env.step(action)
```

Key: `transform_state` must produce the same observation format the agent saw during training.

For simultaneous games, collect all actions before stepping:
```python
actions = {p: agents[p].act(transform_state(state, p)) for p in env.agents}
state, rewards, done, info = env.step(actions)
```

---

## Recommended Stack (for 2-player zero-sum hidden-info games)

| Component | Choice | Reason |
|---|---|---|
| Env framework | OpenSpiel (C++) | Game tree traversal for CFR; explicit info sets |
| Vectorization | PufferLib (for RL approaches) | Fast batched stepping with shared memory |
| RL algorithm | PPO + Transformer context window | Best RL approach for long-range history |
| Game-theoretic algorithm | Deep CFR or ReBeL | Nash convergence guarantee |
| Training | Custom PyTorch loop | PufferLib's built-in PPO won't cover CFR or transformer context |
| Evaluation | Simple Python game loop | Speed doesn't matter; just need obs format compatibility |

If the simultaneous game is also 2-player zero-sum, same stack applies. If multiplayer or general-sum, consider piKL-style regularized search or population methods (PSRO).

