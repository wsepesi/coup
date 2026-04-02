# Coup: Multi-Agent RL Environment — Complete Design Document

## Table of Contents

1. [Game Rules](#1-game-rules)
2. [Design Philosophy](#2-design-philosophy)
3. [State Space](#3-state-space)
4. [Action Space](#4-action-space)
5. [Phase State Machine](#5-phase-state-machine)
6. [Observation Design & Ring Buffer](#6-observation-design--ring-buffer)
7. [Performance Engineering](#7-performance-engineering)
8. [Dual-Framework Architecture](#8-dual-framework-architecture)
9. [Shared PRNG](#9-shared-prng)
10. [Chance Node Design](#10-chance-node-design)
11. [PufferLib Integration (C / PPO)](#11-pufferlib-integration)
12. [OpenSpiel Integration (C++ / CFR)](#12-openspiel-integration)
13. [LLM Text Interface](#13-llm-text-interface)
14. [Game Tree Analysis & Exploitability](#14-game-tree-analysis--exploitability)
15. [Cross-Framework Validation](#15-cross-framework-validation)
16. [Reward Design](#16-reward-design)
17. [Repository Structure & Build](#17-repository-structure--build)
18. [Implementation Priority](#18-implementation-priority)
19. [Open Questions & Gotchas](#19-open-questions--gotchas)

---

## 1. Game Rules

Coup is an incomplete-information bluffing game for 2–6 players. Each player starts with two face-down influence cards and two coins. The deck contains five character types (Duke, Assassin, Captain, Ambassador, Contessa) with three copies of each, totaling 15 cards. After dealing two cards to each player, the remaining cards form a central court deck (3–11 cards depending on player count).

On their turn, a player must take exactly one action. Some actions are always available: **Income** (take 1 coin), **Foreign Aid** (take 2 coins), and **Coup** (pay 7 coins, force a target to lose one influence). Others require claiming a character: **Tax** (Duke; take 3 coins), **Steal** (Captain; take 2 coins from a target), **Exchange** (Ambassador; draw 2 cards from the court deck, choose which to keep, return the rest), and **Assassinate** (Assassin; pay 3 coins, force a target to lose one influence). A player with 10 or more coins *must* Coup — no other action is permitted.

Any player may **challenge** a character claim. If challenged, the claimant must reveal the relevant card. If they have it, the challenger loses one influence, and the claimant shuffles the revealed card back into the deck and draws a replacement. If they don't have it, the claimant loses one influence. Challenges are resolved before the action takes effect.

Certain actions can be **blocked** by other players claiming a character: Foreign Aid can be blocked by anyone claiming Duke. Steal can be blocked by the target claiming Captain or Ambassador. Assassinate can be blocked by the target claiming Contessa. Blocks are themselves character claims and can therefore be challenged using the same challenge rules.

When a player loses influence, they choose which of their face-down cards to reveal. Revealed cards are public and permanent — that influence is dead. A player who loses both influences is eliminated. The last player standing wins.

The Ambassador Exchange deserves special detail: the player draws two cards from the court deck, looks at all available cards (their surviving hand plus the two drawn), and chooses which to keep. They return the unchosen cards face-down to the deck. This is the only action that interacts with the deck contents and provides the player with hidden information about what's in the deck. The exchange is implemented as two sequential discard steps: first the player picks one card to discard from the 4 available slots (own card 0, own card 1, drawn 0, drawn 1), then picks a second to discard from remaining slots with index strictly above the first pick. This canonical ordering eliminates duplicate outcomes and produces exactly C(4,2) = 6 unique results. When the player has already lost one influence, it reduces to C(3,1) = 3.

---

## 2. Design Philosophy

This environment is designed around two priorities: **simulation speed** (targeting C with C++/Python bindings) and **training simplicity** (easy batching, fixed tensor shapes, minimal representations). It must simultaneously support high-throughput RL (PPO via PufferLib) and game-theoretic analysis (CFR via OpenSpiel).

Core principles:

**Sim state and network state are fully decoupled.** The sim maintains the minimum complete state needed to advance the game deterministically. The network receives a projected observation via an `observe()` function. This naturally handles incomplete information, since each player's observation is a different projection of the same underlying state.

**The game struct is a pure value type.** No pointers, no heap allocations, no external dependencies. The entire struct is `memcpy`-able and `memcmp`-able. Forking a state for search (MCTS, CFR) is a single `memcpy`.

**Fixed action space, variable semantics via state, with masking for validity.** The network always outputs logits over the same fixed-width action space. A bitmask zeros out invalid actions before sampling. All branching complexity lives in the sim's internal state machine, not in the action space structure. Each action index has stable semantics — action 22 always means "challenge," action 25 always means "block with captain." The network never has to multiplex unrelated decisions through shared output neurons.

**One `step()` call = one decision by one player.** The sim tracks whose turn it is and what kind of decision they're making. This keeps the interface uniform even during irregular turn structures like challenge rounds.

**Stochasticity is factored for dual-framework use.** Every random event exposes `chance_outcomes()` (for CFR enumeration) and `apply_chance()` (for deterministic application). PufferLib samples internally via RNG; OpenSpiel enumerates explicitly. The game logic is identical in both paths.

---

## 3. State Space

### Sim State (the ground truth)

The sim state is a fixed-size packed struct targeting ≤32 bytes of game data (excluding RNG), designed to fit two game instances per cache line (64 bytes) when RNG is included.

#### Per-Player Data (12 bits × 6 players)

Each player's state packs into a `uint16_t` (12 bits used, 4 wasted for alignment):

| Field | Bits | Range | Notes |
|-------|------|-------|-------|
| `card0_type` | 3 | 0–4 | Duke=0, Assassin=1, Captain=2, Ambassador=3, Contessa=4 |
| `card0_alive` | 1 | 0–1 | Whether this influence is face-down |
| `card1_type` | 3 | 0–4 | Character type |
| `card1_alive` | 1 | 0–1 | Whether this influence is face-down |
| `coins` | 4 | 0–12 | Coin count (max 12 is sufficient) |

Storage: `uint16_t players[6]` = 12 bytes. A player is alive if `card0_alive || card1_alive`.

#### Deck State (10 bits)

The deck stores a count per card type rather than individual cards. With 3 copies max per type, 2 bits per type suffices:

| Field | Bits | Notes |
|-------|------|-------|
| `duke_count` | 2 | 0–3 copies remaining |
| `assassin_count` | 2 | 0–3 |
| `captain_count` | 2 | 0–3 |
| `ambassador_count` | 2 | 0–3 |
| `contessa_count` | 2 | 0–3 |

Storage: `uint16_t deck` = 2 bytes (10 bits used). Drawing a card means selecting a random type weighted by count, then decrementing. Returning a card means incrementing. No shuffle operation exists — the deck is an unordered multiset.

#### Phase / Turn Tracking (15 bits)

| Field | Bits | Range | Notes |
|-------|------|-------|-------|
| `phase` | 4 | 0–9 | Current game phase (includes chance phases for OpenSpiel) |
| `turn_player` | 3 | 0–5 | Whose turn initiated the current action sequence |
| `active_player` | 3 | 0–5 | Who must act next |
| `pending_action` | 6 | 0–31 | The action being resolved |

Storage: `uint16_t phase_state` = 2 bytes.

#### Auxiliary State (16+ bits)

| Field | Bits | Notes |
|-------|------|-------|
| `responded_mask` | 6 | Who has passed on current challenge/block opportunity |
| `exchange_card0` | 3 | Type of first drawn card during Ambassador exchange |
| `exchange_card1` | 3 | Type of second drawn card during Ambassador exchange |
| `first_discard` | 3 | Index of first discarded slot (7 = not yet chosen) |
| `blocker` | 3 | Player ID of whoever is blocking |
| `block_card` | 3 | Which character claimed for the block |

Storage: `uint16_t aux` + `uint16_t aux2` = 4 bytes. Many fields are only relevant in specific phases and can overlap since they're mutually exclusive.

Dead players should have their bits pre-set to 1 in `responded_mask` so cycling logic naturally skips them.

#### RNG State

`uint64_t rng_state` = 8 bytes. Uses xoshiro256** (see section 9). Only consumed in PufferLib mode. In OpenSpiel mode, the RNG field is unused — stochasticity is handled via explicit chance nodes.

#### Total Layout

```c
struct Game {
    uint16_t players[6];   // 12 bytes — per-player packed state
    uint16_t deck;         //  2 bytes — card counts per type
    uint16_t phase_state;  //  2 bytes — phase + turn + active + pending
    uint16_t aux;          //  2 bytes — responded mask + exchange temporaries
    uint16_t aux2;         //  2 bytes — blocker info, block card type, misc
    uint16_t _pad;         //  2 bytes — alignment padding
    uint64_t rng_state;    //  8 bytes — PRNG state (PufferLib only)
};
// Total: 32 bytes. Two instances per 64-byte cache line.
```

Every field is a fixed-width integer. No pointers. `memcpy` is a perfect snapshot. `memcmp` is a valid equality check. Forking for MCTS/CFR is a single `memcpy`.

### Network Observation (what the agent sees)

The observation is a fixed-size tensor produced by `observe(game_state, player_id)`. It projects the full sim state into what a given player is allowed to know: their own cards (type and alive status, one-hot encoded), other players' revealed cards and alive/dead status, all coin counts, phase/active/turn player encodings, the pending action, the responded mask, exchange cards (if applicable), and the history ring buffer. The observation function is a pure function with no side effects.

---

## 4. Action Space

### Layout (32 actions, fits in `uint32_t` bitmask)

```
Index   Action                          Phase(s) Valid
─────   ──────                          ─────────────
 0      income                          MAIN_ACTION
 1      foreign_aid                     MAIN_ACTION
 2      tax (claim Duke)                MAIN_ACTION
 3      exchange (claim Ambassador)     MAIN_ACTION
 4–9    coup → player 0–5              MAIN_ACTION
10–15   steal → player 0–5             MAIN_ACTION
16–21   assassinate → player 0–5       MAIN_ACTION
22      challenge                       CHALLENGE_ACTION, CHALLENGE_BLOCK
23      pass                            CHALLENGE_ACTION, CHALLENGE_BLOCK, BLOCK
24      block_contessa                  BLOCK
25      block_captain                   BLOCK
26      block_ambassador                BLOCK
27      block_duke                      BLOCK
28      discard_slot_0                  LOSE_CARD, EXCHANGE_DISCARD
29      discard_slot_1                  LOSE_CARD, EXCHANGE_DISCARD
30      discard_slot_2                  EXCHANGE_DISCARD only
31      discard_slot_3                  EXCHANGE_DISCARD only
```

### Key Design Decisions

**Flat, not factored.** A factored action space (pick action, then pick target) would require two forward passes or an autoregressive action head. Flat + mask is one forward pass, one softmax, one sample.

**Stable semantics per index.** Each index has a fixed meaning. Neuron 22 always means "challenge" and specializes for that concept. Overloading indices where the same output means different things depending on phase forces the network to multiplex unrelated decisions through shared weights, which is harder to learn and harder to debug.

**`pass` is shared across challenge/block phases.** The phase in the observation disambiguates, and the semantics — "I decline to act" — are genuinely the same.

**Discard slots are reused for card loss and Ambassador exchange.** The phase tells the network whether this is "you're dying" or "you're exchanging." The second exchange discard only offers slots above the first pick to enforce canonical ordering.

**32 actions is negligible.** The output layer is a single linear projection to 32 logits. The forward pass through the network body dominates.

### Mask Computation

The mask is computed as a pure function: `uint32_t get_valid_actions(const Game *g)`. No allocation, no state mutation. A helper `spread_targets(alive_mask, offset)` maps the 6-bit alive mask into correct action index positions. Self-targeting is removed by clearing the active player's bit.

The 10+ coin forced coup is purely a mask constraint — `get_valid_actions()` zeroes everything except coup target bits when `coins >= 10`.

---

## 5. Phase State Machine

```
                    ┌───────────────────────────────────────────────────────────┐
                    │                                                           │
                    ▼                                                           │
              MAIN_ACTION                                                      │
                    │                                                           │
          ┌────────┴────────┐                                                  │
          │                 │                                                   │
    unchallengeable    challengeable                                            │
    (income, coup)    (tax, steal, assassinate, exchange)                       │
          │                 │                                                   │
          │          CHALLENGE_ACTION ─── challenge ─── RESOLVE_CHALLENGE       │
          │                 │                                  │                │
          │            pass (all)                       ┌──────┴──────┐        │
          │                 │                        claimant     challenger    │
          │                 │                         loses         loses       │
          │                 │                           │              │        │
          │          ┌──────┴──────┐                LOSE_CARD     LOSE_CARD    │
          │          │             │                (action dies)  + reshuffle  │
          │     blockable     unblockable                          + redraw    │
          │          │             │                                   │        │
          │        BLOCK        RESOLVE                            RESOLVE     │
          │          │                                                │        │
          │    ┌─────┴─────┐                                          │        │
          │  block       pass (all)                                   │        │
          │    │             │                                         │        │
          │  CHALLENGE_BLOCK RESOLVE                                  │        │
          │    │                                                       │        │
          │  ┌─┴──┐                                                    │        │
          │ chal  pass                                                 │        │
          │  │      │                                                  │        │
          │  │   BLOCKED (action cancelled)                            │        │
          │  │                                                         │        │
          │  RESOLVE_CHALLENGE (of block)                              │        │
          │    │                                                       │        │
          │  ┌─┴──────┐                                                │        │
          │ blocker  challenger                                        │        │
          │ loses    loses                                             │        │
          │ (block   (block stands)                                    │        │
          │  fails)                                                    │        │
          │   │        │                                               │        │
          │ RESOLVE  BLOCKED                                           │        │
          │                                                            │        │
          ▼                                                            ▼        │
         RESOLVE ◄─────────────────────────────────────────────────────        │
             │                                                                  │
    ┌────────┼──────────────┐                                                  │
    │        │              │                                                   │
 coin     target         exchange                                              │
 update   LOSE_CARD      EXCHANGE_DISCARD × 2 → return to deck                │
    │        │              │                                                   │
    └────────┴──────────────┴─── ADVANCE_TURN ─────────────────────────────────┘
```

### Phase Enum

```c
enum Phase {
    // Chance phases (OpenSpiel: current_player = -1)
    DEAL              = 0,   // Initial card deal (sequential chance nodes)
    CHANCE_REDRAW     = 1,   // Challenge defense: shuffle back, draw replacement
    CHANCE_EXCHANGE   = 2,   // Ambassador: draw cards from deck

    // Player decision phases
    MAIN_ACTION       = 3,
    CHALLENGE_ACTION  = 4,
    BLOCK             = 5,
    CHALLENGE_BLOCK   = 6,
    LOSE_CARD         = 7,
    EXCHANGE_DISCARD  = 8,

    // Internal resolution (auto-advance, no player input)
    RESOLVE           = 9,
};
```

### Cycling Logic for Challenges and Blocks

When multiple players can respond (challenge opportunities, Foreign Aid block), the sim cycles one at a time. Set `active_player` to the next eligible responder (alive, hasn't responded, not the actor). On `pass`, set the responder's bit in `responded_mask` and advance. If all bits set, advance phase. If someone challenges or blocks, transition immediately. Cycling starts from the player after the turn player and wraps around.

---

## 6. Observation Design & Ring Buffer

### Why a Ring Buffer

Coup is fundamentally non-Markov. Optimal play requires tracking what characters other players have claimed or been challenged on, which cards have been revealed and shuffled back in, and patterns of bluffing. A single-frame observation is insufficient.

A ring buffer of recent transitions provides bounded history without variable-length tensors. This is a training-side concern — the sim itself is fully Markov. The ring buffer exists only in the observation pipeline.

### Design

Maintain a fixed-size circular buffer of the last N transitions:

```c
struct HistoryEntry {
    uint8_t acting_player;
    uint8_t action;
    uint8_t phase;
    uint8_t result;     // outcome flags (challenge success/fail, block, etc.)
};
// 4 bytes per entry
```

With N=64 entries, this is 256 bytes. The buffer is per-game, maintained alongside the sim state but not part of the core `Game` struct:

```c
struct GameWrapper {
    Game game;
    HistoryEntry history[64];
    uint8_t history_head;
    uint8_t history_len;
};
```

The `observe()` function reads both `game` and `history` to produce the observation tensor. Older entries beyond N are lost, which is acceptable — games of Coup rarely exceed ~50 meaningful decisions.

### Observation Tensor Layout (~410 floats)

```
Offset  Field                                  Size
──────  ─────                                  ────
0       own_card0 (one-hot type)               5
5       own_card0_alive                        1
6       own_card1 (one-hot type)               5
11      own_card1_alive                        1
12      other_players × {revealed, alive}      60
72      all_coins (normalized)                 6
78      alive_mask                             6
84      phase (one-hot)                        7
91      active_player (one-hot)                6
97      turn_player (one-hot)                  6
103     pending_action (one-hot)               32
135     responded_mask                         6
141     exchange_cards (one-hot × 2)           10
151     history[64] × 4 floats each            256
                                               ≈ 410 total
```

Fixed shape, always. Empty history slots are zeros.

---

## 7. Performance Engineering

### Memory Layout

The `Game` struct is 32 bytes. Two fit in a single 64-byte cache line. Allocate a flat array:

```c
Game *games = aligned_alloc(64, sizeof(Game) * N);
```

Stepping all games sequentially streams through memory with excellent cache behavior.

### No Dynamic Allocation

The game struct contains only fixed-width integers. No `malloc`, no `free`. This is critical for forking (`memcpy` is a perfect snapshot), parallelism (no shared heap, no locks), and determinism (same bytes → same future).

### RNG

xoshiro256** lives inside the game struct. Never use global `rand()` or thread-local RNG. For initialization, split into a **deal seed** (consumed during setup, discarded) and a **procedural seed** (stored in struct, consumed for mid-game events):

```c
void game_init(Game *g, uint64_t deal_seed, uint64_t proc_seed) {
    Xoshiro256 deal_rng;
    xoshiro256_seed(&deal_rng, deal_seed);
    deal_cards(g, &deal_rng);       // deal_rng consumed and discarded
    xoshiro256_seed((Xoshiro256*)&g->rng_state, proc_seed);
}
```

### Deck Operations

The deck is 5 counts packed into a `uint16_t`. Drawing a card: sum counts to get total, pick random index in `[0, total)`, walk counts to find which type, decrement. Returning a card: increment count. No arrays, no shuffling.

### Mask Computation

`get_valid_actions()` is a pure function: ~20 instructions of switches and bitwise ops. No allocation. Call fresh each time.

### Batched Stepping

```c
for (int i = 0; i < B; i++) {
    masks[i] = get_valid_actions(&games[i]);
}
// → GPU for batched inference → receive actions
for (int i = 0; i < B; i++) {
    step(&games[i], actions[i]);
    observe(&games[i], get_active_player(&games[i]), observations[i]);
    if (is_done(&games[i])) game_init(&games[i], new_seed(), new_seed());
}
```

The bottleneck is neural network inference, not sim stepping. Thousands of `step()` calls per millisecond is realistic.

### Why Not GPU Environments

Coup's 32-byte game state with a branchy state machine (switch on phase, conditional challenge cycling) is a poor fit for GPUs. Warp divergence would dominate — GPUs need uniform compute across thousands of threads. The C sim on CPU comfortably does 10M+ steps/sec, and the neural net inference is the actual bottleneck. GPU envs (e.g. via pgx/JAX) shine when the sim itself is expensive (physics, large grids, matrix ops), not a 20-instruction state machine.

---

## 8. Dual-Engine Architecture

**Decision: Two fully separate engines.** A pure C engine for PufferLib and a pure C++ engine for OpenSpiel. No shared engine code — each is an independent implementation of the same game spec (this document). Correctness parity is enforced by cross-framework fuzz tests, not shared source.

```
┌──────────────────────────────┐    ┌──────────────────────────────┐
│  c_engine/                    │    │  cpp_engine/                  │
│  coup_core.h / coup_core.c    │    │  coup_game.h / coup_game.cc   │
│                                │    │                                │
│  Pure C. No framework deps.    │    │  Pure C++. OpenSpiel classes.  │
│  Game struct, step, observe,   │    │  CoupState, CoupGame.          │
│  mask, chance, PRNG.           │    │  No RNG — all via chance nodes.│
│                                │    │                                │
│  step_with_rng() ← PufferLib   │    │  ChanceOutcomes() + Apply     │
│  step_deterministic() ← tests  │    │  InformationStateTensor()     │
│  chance_outcomes() ← replay    │    │  InformationStateString()     │
└──────────────┬────────────────┘    └──────────────┬───────────────┘
               │                                     │
       ┌───────▼───────┐                     ┌───────▼────────┐
       │  PufferLib     │                     │  OpenSpiel      │
       │  Pure C binding│                     │  Dynamic reg.   │
       │  (CPython ext) │                     │  via RegisterGame│
       │                │                     │                  │
       │  Internal RNG  │                     │  Explicit chance │
       │  Ring buffer   │                     │  nodes via       │
       │  obs → numpy   │                     │  State::history_ │
       └────────────────┘                     └──────────────────┘
```

**Rationale:** Pure C++ is the overwhelmingly dominant pattern in OpenSpiel (90+ games). The one C-wrapping precedent (`universal_poker`) adds complexity with no clear benefit when implementing from scratch. Separate engines are easy to cross-validate and let each use idiomatic patterns for its framework.

### The Critical Insight: Stochasticity

PufferLib and OpenSpiel handle randomness fundamentally differently:

**PufferLib (PPO/self-play):** The environment owns an RNG. Stochastic events are consumed internally. The agent never sees the randomness mechanism — fast, simple, standard for RL.

**OpenSpiel (CFR):** Stochastic events must be modeled as explicit chance nodes. CFR requires enumerating all outcomes and their probabilities to compute counterfactual values. Hiding randomness inside `step()` breaks CFR. The C++ engine has no RNG state at all.

**Cross-engine determinism:** Matching rollouts work by recording the chance outcomes the C engine's PRNG samples, then replaying those exact outcomes through the C++ engine's `DoApplyAction()` at chance nodes. A cross-framework fuzz test validates this for many seeds and player counts.

---

## 9. Shared PRNG

Both implementations use xoshiro256** with identical initialization via SplitMix64 seeding. The algorithm produces bit-identical sequences in C and C++.

```c
// prng.h — extern "C" compatible, header-only

#include <stdint.h>

typedef struct { uint64_t s[4]; } Xoshiro256;

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static inline uint64_t xoshiro256_next(Xoshiro256 *rng) {
    const uint64_t result = rotl(rng->s[1] * 5, 7) * 9;
    const uint64_t t = rng->s[1] << 17;
    rng->s[2] ^= rng->s[0];
    rng->s[3] ^= rng->s[1];
    rng->s[1] ^= rng->s[2];
    rng->s[0] ^= rng->s[3];
    rng->s[2] ^= t;
    rng->s[3] = rotl(rng->s[3], 45);
    return result;
}

static inline uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
}

static inline void xoshiro256_seed(Xoshiro256 *rng, uint64_t seed) {
    rng->s[0] = splitmix64(&seed);
    rng->s[1] = splitmix64(&seed);
    rng->s[2] = splitmix64(&seed);
    rng->s[3] = splitmix64(&seed);
}

// Uniform integer in [0, n) — rejection sampling, no modulo bias
static inline uint32_t xoshiro256_uniform(Xoshiro256 *rng, uint32_t n) {
    uint64_t r = xoshiro256_next(rng);
    uint64_t m = (uint64_t)n * (uint32_t)r;
    uint32_t l = (uint32_t)m;
    if (l < n) {
        uint32_t t = -n % n;
        while (l < t) {
            r = xoshiro256_next(rng);
            m = (uint64_t)n * (uint32_t)r;
            l = (uint32_t)m;
        }
    }
    return m >> 32;
}
```

**First test to write:** initialize instances in C and C++ with the same seed, generate 10,000 values, verify bit-identical output. This is the foundation of cross-framework reproducibility.

---

## 10. Chance Node Design

There are exactly three types of stochastic events in Coup:

**1. Initial Deal.** 2 cards dealt to each player from a 15-card deck. Modeled as sequential chance nodes (one per card dealt). Each node's distribution is uniform over remaining card types, weighted by count. For 6 players, that's 12 chance nodes at game start, each with at most 5 outcomes.

**2. Challenge Defense Redraw.** When a player successfully defends a challenge, they shuffle the revealed card back and draw a replacement. One chance node with distribution proportional to the updated deck counts.

**3. Ambassador Exchange Draw.** The player draws 2 cards from the deck. Two sequential chance nodes, each uniform over remaining deck contents.

### Encoding

Chance outcomes are card type indices 0–4. Probability of each is `count[type] / total_remaining`.

```c
#define MAX_CHANCE_OUTCOMES 5

typedef struct {
    int outcome;    // card type 0-4
    double prob;
} ChanceOutcome;

int chance_outcomes(const Game *g, ChanceOutcome *out) {
    int total = deck_total(g);
    int n = 0;
    for (int i = 0; i < 5; i++) {
        int count = deck_count(g, i);
        if (count > 0) {
            out[n].outcome = i;
            out[n].prob = (double)count / total;
            n++;
        }
    }
    return n;
}

void apply_chance(Game *g, int outcome) {
    switch (get_chance_phase(g)) {
    case DEAL:            assign_dealt_card(g, outcome); advance_deal(g); break;
    case CHANCE_REDRAW:   replace_revealed_card(g, outcome); deck_remove(g, outcome);
                          continue_after_challenge(g); break;
    case CHANCE_EXCHANGE: store_exchange_card(g, outcome); deck_remove(g, outcome);
                          advance_exchange_draw(g); break;
    }
}
```

PufferLib's wrapper resolves chance events transparently:

```c
void step_with_rng(Game *g, int action, Xoshiro256 *rng) {
    step_deterministic(g, action);
    while (is_chance_node(g)) {
        ChanceOutcome outcomes[MAX_CHANCE_OUTCOMES];
        int n = chance_outcomes(g, outcomes);
        int pick = sample_from_outcomes(outcomes, n, rng);
        apply_chance(g, pick);
    }
}
```

From the PufferLib agent's perspective, one call resolves all intermediate chance events. From OpenSpiel's perspective, each is a separate node in the game tree.

---

## 11. PufferLib Integration

### Installation

PufferLib is included as a **git submodule** at `lib/PufferLib/` (cloned from https://github.com/PufferAI/PufferLib) and installed **from source** in editable mode via `uv pip install -e lib/PufferLib`. All Python commands use `uv run` to ensure the correct virtual environment.

### Out-of-Tree Integration

The Coup environment lives **outside** PufferLib's tree, in the project's own `pufferlib/` directory. This avoids dirtying the submodule. The integration pattern (matching current PufferLib conventions) is:

1. Copy `env_binding.h` from PufferLib's `ocean/` directory into `pufferlib/`
2. Write `binding.c` (CPython C extension) that includes the C engine headers and `env_binding.h`
3. Write `coup_env.py` subclassing `pufferlib.PufferEnv`
4. Build via `setuptools.Extension` in the project's own `setup.py`

**No Cython.** PufferLib has moved to pure C bindings using CPython's C extension API with `env_binding.h` as shared glue. Numpy array data pointers are passed directly to C structs — zero-copy.

### Multi-Agent Approach

**Single-agent framing with player rotation.** Each PufferLib environment instance is one game of Coup. Each `step()` provides one action for one player (the active player). PufferLib sees `num_agents=1` per env, and many env instances run in parallel. This is simpler and likely faster for self-play than a multi-agent framing where 5 of 6 agents' actions are ignored each step. All seats share one policy; the observation encodes seat identity via relative positioning.

### File Layout

```
pufferlib/                       # In project root, NOT inside lib/PufferLib/
├── env_binding.h                # Copied from PufferLib ocean/ (CPython glue framework)
├── binding.c                    # CPython extension: init, step, log
├── coup_env.py                  # Python PufferEnv class
└── test_perf.py                 # SPS benchmark
```

### Environment Struct

```c
// In binding.c
typedef struct {
    // Required by env_binding.h
    float *observations;
    int *actions;
    float *rewards;
    uint8_t *terminals;
    Log log;

    // Game state
    Game game;
    HistoryBuffer history;   // Ring buffer (PufferLib owns this, not the core engine)
    int num_players;
    uint64_t seed;
} CoupEnv;
```

### History Ring Buffer

The ring buffer is owned by the PufferLib wrapper, NOT the core Game struct. The core engine is fully Markov. The ring buffer exists only to provide bounded history in the observation tensor for PPO training. OpenSpiel uses its own `State::history_` for full action history.

### Action Masking

PufferLib doesn't natively support action masking. The last 32 floats of the observation tensor are the action validity mask. The PyTorch policy applies the mask before sampling:

```python
def decode_actions(self, hidden, lookup):
    logits = self.action_head(hidden)
    masks = lookup["masks"]  # [batch, 32]
    logits = logits + (1 - masks) * -1e8
    value = self.value_head(hidden)
    return logits, value
```

### Python PufferEnv Sketch

```python
class Coup(pufferlib.PufferEnv):
    def __init__(self, num_envs=1, buf=None, seed=0, num_players=6):
        self.single_observation_space = gymnasium.spaces.Box(
            low=0, high=1, shape=(OBS_DIM,), dtype=np.float32)
        self.single_action_space = gymnasium.spaces.Discrete(32)
        self.num_agents = num_envs
        super().__init__(buf)
        # binding is the compiled CPython extension from binding.c
        binding.vec_init(
            self.observations, self.actions, self.rewards,
            self.terminals, self.truncations,
            num_envs, seed, num_players=num_players)
```

---

## 12. OpenSpiel Integration

### Architecture

OpenSpiel is included as a **git submodule** at `lib/OpenSpiel/`. The Coup game is a **pure C++ implementation** (no C dependency) that builds standalone against the OpenSpiel library via its own `CMakeLists.txt`. Registration is dynamic via `REGISTER_SPIEL_GAME()` — no modification of OpenSpiel's source tree is needed.

This follows the dominant pattern in OpenSpiel where all 90+ games are pure C++. The one C-wrapping precedent (`universal_poker`) is unnecessarily complex when implementing from scratch.

### Game Type

```cpp
const GameType kGameType{
    "coup", "Coup",
    GameType::Dynamics::kSequential,
    GameType::ChanceMode::kExplicitStochastic,
    GameType::Information::kImperfectInformation,
    GameType::Utility::kGeneralSum,  // Compatible with +1/-1; kZeroSum would also work for 2p
    GameType::RewardModel::kTerminal,
    /*max_num_players=*/6, /*min_num_players=*/2,
    /*provides_information_state_string=*/true,
    /*provides_information_state_tensor=*/true,
    /*provides_observation_string=*/true,
    /*provides_observation_tensor=*/true,
    /*parameter_specification=*/
    {{"players", GameParameter(2)}},  // Default 2, range 2-6
};

// Dynamic registration — no central file editing
REGISTER_SPIEL_GAME(kGameType, Factory);
```

### State Class Key Methods

```cpp
class CoupState : public State {
public:
    Player CurrentPlayer() const override;
    // Returns kChancePlayerId (-1) during deal/redraw/exchange draw
    // Returns 0-5 for player decisions
    // Returns kTerminalPlayerId (-4) when done

    std::vector<Action> LegalActions() const override;
    void DoApplyAction(Action action) override;
    std::vector<std::pair<Action, double>> ChanceOutcomes() const override;

    std::string InformationStateString(Player player) const override;
    void InformationStateTensor(Player player,
                                absl::Span<float> values) const override;

    bool IsTerminal() const override;
    std::vector<double> Returns() const override;
    std::unique_ptr<State> Clone() const override;

private:
    // Pure C++ internal state — no C Game struct dependency.
    // Can use clearer representations (arrays, structs) since
    // OpenSpiel doesn't need the C engine's bit-packing.
    // ...
};
```

`Clone()` via copy constructor — all state is value-typed, no heap allocations. The C++ engine has **no RNG state** — all stochasticity is expressed through chance nodes.

`InformationStateTensor()` must produce the same ~410-float encoding as the C engine's `observe()` function for identical game states. This is validated by the observation parity cross-framework test.

### File Layout

```
cpp_engine/
├── coup_game.h              # CoupGame + CoupState class declarations
├── coup_game.cc             # Implementation + REGISTER_SPIEL_GAME
├── coup_game_test.cc        # OpenSpiel-style tests
└── CMakeLists.txt           # Builds against lib/OpenSpiel/ submodule
```

### Information State for CFR

CFR requires that two game states informationally equivalent to player `p` produce the same `InformationStateString`. For player `p`, this includes their own cards, all public information (revealed cards, coin counts), and the full action history (via OpenSpiel's `State::history_`). It does NOT include other players' hidden cards or deck contents.

---

## 13. LLM Text Interface

A pure function `render_text(game, player_id, history)` produces a natural language prompt and a mapping from numbered text options back to action indices. This enables fine-tuning language models to play Coup.

### Output Format

For main actions:

```
You are Player 3 in a game of Coup. You have a Duke and a Captain
(both face-down). You have 5 coins.

Player 0: 2 coins, 1 influence (revealed: Contessa)
Player 1: 7 coins, 2 influences
Player 2: ELIMINATED (revealed: Duke, Assassin)
Player 4: 3 coins, 1 influence (revealed: Ambassador)
Player 5: 4 coins, 2 influences

Recent history:
- Player 1 claimed Duke for Tax. No one challenged. Gained 3 coins.
- Player 4 attempted to Steal from Player 5 claiming Captain.
  Player 5 blocked claiming Ambassador. No one challenged the block.

It is your turn. Choose an action:
1. Income (take 1 coin)
2. Foreign Aid (take 2 coins)
3. Tax (claim Duke, take 3 coins)
4. Coup Player 0 (pay 7 coins)
5. Steal from Player 1 (claim Captain)
...
```

For challenge/block phases:

```
Player 1 claims Captain to Steal from you.
1. Challenge
2. Pass
3. Block with Captain
4. Block with Ambassador
```

### Implementation

```c
typedef struct {
    char prompt[4096];
    int action_map[32];   // text_option_number → flat action index
    int num_options;
} TextRender;

void render_text(const Game *g, int player_id,
                 const HistoryEntry *history, int history_len,
                 TextRender *out);

int parse_text_action(const TextRender *render, int chosen_option);
```

The action map is generated dynamically from the mask — only valid actions are numbered and presented. The LLM never sees invalid options. History is converted to readable text via a lookup table per action index.

This is implemented in C so training data can be generated at scale in the same fast loop as the sim: render millions of game states into text for supervised fine-tuning from strong play (PPO or CFR agent trajectories).

---

## 14. Game Tree Analysis & Exploitability

### 2-Player Coup

**Deal space:** 15 cards, deal 2 to each player as sequential type-level draws. Roughly 500–600 distinct type-level dealt configurations.

**Per-deal tree:** A typical game runs 10–15 turns, each with variable decision points (challenge/pass cycles, block/pass cycles, card loss choices). Roughly 25–35 player decision nodes per game, average branching factor ~2.5 (most decisions are binary; main action choice is wider but less frequent).

$$\text{Tree per deal} \approx 2.5^{30} \approx 10^{12}$$

$$\text{Total tree} \approx 500 \times 10^{12} \approx 5 \times 10^{14}$$

For context, heads-up limit Texas Hold'em has ~$10^{14}$ game tree nodes and was solved by CFR+ in 2015 (months on a cluster). 2-player Coup is in the same neighborhood.

**Information set count:** smaller than the tree (many nodes map to the same info set since the opponent's hidden cards don't differentiate your info state). Rough estimate: $10^{10}$ to $10^{12}$ information sets. Each needs a regret vector of size ≤ 32. At 32 floats × $10^{11}$ info sets ≈ 12 TB of regret tables. Tight but possible with disk-backed MCCFR or abstraction.

**Useful abstractions for 2-player tabular:** Card isomorphism (permuting card types not yet publicly distinguished, up to 120× compression in theory, limited by asymmetric action structure). Coin bucketing (group into ranges like 0–2, 3–6, 7–9, 10+). History truncation (last N events instead of full history, aligns with ring buffer). Aggressive abstraction could bring info sets down to ~$10^8$, comfortably feasible for tabular MCCFR.

### 6-Player Coup

**Deal space:** 12 cards dealt from 15. Probably $10^5$ to $10^6$ distinct type-level configurations.

**Per-deal tree:** Main action has up to 26 options. Challenge round cycles through up to 5 opponents (binary each). Block round similar. A single challengeable+blockable turn can have ~11,000 tree nodes worst case. Games last 30–60 turns. Roughly 100–200 decision nodes per game, average branching ~4.

$$\text{Tree per deal} \approx 4^{150} \approx 10^{90}$$

$$\text{Total tree} \approx 10^6 \times 10^{90} = 10^{96}$$

Firmly in the "no exact methods" zone.

### Feasibility Summary

| Method | 2-player | 6-player |
|--------|----------|----------|
| Tabular CFR/CFR+ (exact) | Borderline; feasible with abstraction | No |
| Tabular MCCFR (sampling) | Yes, with abstraction (days–weeks) | No |
| Deep CFR | Yes (overkill) | Yes (primary CFR method) |
| Exact exploitability | Feasible with abstraction | No |
| Approximate exploitability | Yes | Via local best response |
| PPO self-play | Yes | Yes (primary training method) |

### Exploitability Metrics for 6-Player

**Local Best Response (most rigorous).** Fix 5 copies of the trained policy. Train a best-response agent for player 0 via RL against those 5 frozen copies. The gap between the BR's return and the base policy's expected return measures how much player 0 can gain by deviating. Repeat for each seat:

```python
for seat in range(6):
    frozen = load_trained_policy()
    br = train_best_response(seat=seat, opponents=[frozen]*5, method="PPO")
    base_return = evaluate(frozen, seat, opponents=[frozen]*5)
    br_return = evaluate(br, seat, opponents=[frozen]*5)
    exploit_gap = br_return - base_return
    print(f"Seat {seat}: exploit gap = {exploit_gap:.4f}")
```

If the BR agent can't improve much, the policy is approximately Nash.

**Cross-validation against 2-player exact solution.** Train a 2-player CFR solution (with abstraction). Train a 2-player PPO agent. Compare exploitabilities. This tells you how good PPO is at finding equilibria in Coup specifically and calibrates confidence in the 6-player PPO result.

**Tournament evaluation.** Pit the trained policy against diverse opponents: random, heuristic rule-based bots, older checkpoints, ablations (no history, no certain features). Track win rate by seat. Not a Nash metric but practically useful.

**Monte Carlo best response estimation.** Sample decision points, enumerate legal actions, estimate each action's value via rollouts with the base policy. The difference between the best action's value and the policy's chosen action's value, averaged over many samples, approximates exploitability cheaply.

### Recommended Pipeline

Phase 1: Build env, train PPO on 6-player, validate with tournament evals against heuristics.

Phase 2: Implement 2-player in OpenSpiel, train MCCFR with abstraction, compute exact abstract exploitability, compare to 2-player PPO. This validates the PPO pipeline.

Phase 3: Local best response on 6-player. Train BR agents per seat, report exploit gaps.

Phase 4 (optional): Deep CFR on 6-player via OpenSpiel. Compare its exploit gap to PPO's.

---

## 15. Cross-Framework Validation

### Deterministic Rollout Test

Given a fixed seed and a deterministic policy (e.g. always pick first legal action), both frameworks must produce identical player action sequences and final outcomes. OpenSpiel's trajectory includes chance nodes interleaved with player actions; PufferLib's has only player actions. Filter out chance from OpenSpiel and compare:

```python
def test_cross_framework_determinism():
    seed = 42

    # OpenSpiel path
    state_os = game_os.new_initial_state()
    rng = Xoshiro256(seed)
    os_actions = []
    while not state_os.is_terminal():
        if state_os.is_chance_node():
            action = sample_chance(state_os.chance_outcomes(), rng)
            state_os.apply_action(action)
        else:
            action = state_os.legal_actions()[0]
            os_actions.append((state_os.current_player(), action))
            state_os.apply_action(action)

    # PufferLib path
    env = CoupEnv(seed)
    pf_actions = []
    while not env.is_done():
        action = first_set_bit(env.get_valid_actions())
        pf_actions.append((env.active_player(), action))
        env.step(action)

    assert os_actions == pf_actions
    assert os_returns == pf_returns
```

### Exploitability Validation (2-Player)

Train PPO in PufferLib, export policy, wrap as OpenSpiel `Policy`, compute exploitability:

```python
game = pyspiel.load_game("coup(players=2)")
ppo_policy = load_pufferlib_policy_as_openspiel_policy(game, checkpoint)
exploit = exploitability.exploitability(game, ppo_policy)
```

---

## 16. Reward Design

### Terminal Rewards (both frameworks)

+1 for the winner, -1 for all losers, at game end. All intermediate rewards are 0. This is game-theoretically clean and works for both CFR and PPO.

### Optional Intermediate Shaping (PufferLib only)

Small shaping rewards can accelerate PPO training: negative per-step reward (encourages faster play), small bonus for winning challenges, small penalty for losing influence. Keep these small relative to terminal reward. Track both shaped and unshaped returns. These are PufferLib-only — they don't affect OpenSpiel or game-theoretic analysis.

---

## 17. Repository Structure & Build

```
coup/
├── Makefile                     # Top-level: delegates to sub-builds
├── pyproject.toml               # uv-managed project config
├── setup.py                     # Build C extension for PufferLib binding
├── dev-plan.md                  # Task tracking for concurrent subagents
│
├── c_engine/                    # Pure C engine (PufferLib target)
│   ├── coup_core.h              # Game struct, enums, function declarations
│   ├── coup_core.c              # Implementation
│   ├── prng.h                   # xoshiro256** (header-only)
│   ├── history.h                # Ring buffer struct + helpers (header-only)
│   └── test_core.c              # Standalone C tests
│
├── pufferlib/                   # PufferLib integration (out-of-tree)
│   ├── env_binding.h            # Copied from PufferLib (CPython glue framework)
│   ├── binding.c                # CPython extension: init, step, log
│   ├── coup_env.py              # PufferEnv subclass
│   └── test_perf.py             # SPS benchmark
│
├── cpp_engine/                  # Pure C++ engine (OpenSpiel target)
│   ├── coup_game.h              # CoupGame + CoupState class declarations
│   ├── coup_game.cc             # Implementation + REGISTER_SPIEL_GAME
│   ├── coup_game_test.cc        # OpenSpiel-style tests
│   └── CMakeLists.txt           # Builds against lib/OpenSpiel/ submodule
│
├── lib/
│   ├── PufferLib/               # Git submodule (https://github.com/PufferAI/PufferLib)
│   └── OpenSpiel/               # Git submodule (https://github.com/google-deepmind/open_spiel)
│
├── tests/
│   ├── test_cross_determinism.py  # Same action sequence, both engines, compare outcomes
│   ├── test_observation_parity.py # Same game state, compare observation tensors
│   ├── test_constants_sync.py     # Enum/action index parity between C and C++
│   └── test_prng_identity.py      # xoshiro256** bit-identity C vs C++
│
├── training/
│   ├── puffer_hello.py           # Minimal PPO self-play (hello world)
│   └── openspiel_hello.py        # Minimal MCCFR / exploitability (hello world)
│
└── plans/                        # Design documents
    ├── plan.md                   # Core design spec (this file)
    ├── model-architecture-plan.md
    ├── web.md
    └── tui-plan.md
```

### Build

All Python work uses **`uv`** as the package manager and virtual environment tool. No `pip`, `conda`, or bare `python` invocations. A top-level **Makefile** orchestrates all build targets.

```bash
# One-time setup: submodules + venv
git submodule add https://github.com/PufferAI/PufferLib lib/PufferLib
git submodule add https://github.com/google-deepmind/open_spiel lib/OpenSpiel
git submodule update --init --recursive
uv venv
uv pip install -e lib/PufferLib
uv pip install torch gymnasium

# Build and test everything
make test          # runs all targets below

# Individual targets
make test-c        # cc + run c_engine/test_core.c
make test-cpp      # cmake + run cpp_engine/coup_game_test
make build-puffer  # python setup.py build_ext --inplace
make test-puffer   # run pufferlib/test_perf.py
make test-cross    # run all cross-framework tests
make clean         # remove build artifacts
```

---

## 18. Implementation Priority

See `dev-plan.md` in the project root for the full task breakdown with dependency graph, parallelism map, and agent coordination protocol.

**Critical path (sequential):**
1. **prng.h** — xoshiro256**, header-only.
2. **c_engine/coup_core.h/c** — Full C game logic.
3. **c_engine observe()** — Observation tensor function.
4. **pufferlib/ binding + env** — CPython extension, PufferEnv subclass.
5. **pufferlib/ perf test** — Validate >1M SPS.
6. **training/puffer_hello.py** — Hello-world PPO self-play.

**Parallel with steps 2-4 above:**
- **cpp_engine/** — Pure C++ OpenSpiel engine (independent of C engine).
- **cpp_engine/ CMake + registration** — Build against OpenSpiel submodule.
- **All tests** — C standalone, C++ OpenSpiel-style, cross-framework.

**Deferred to later phases (not in current dev-plan.md):**
- text_render (LLM text interface)
- Real PPO training + tuning
- MCCFR / Deep CFR
- Exploitability analysis
- Web, TUI, and other downstream clients

---

## 19. Open Questions & Gotchas

### Resolved Questions

- **History encoding:** PufferLib owns a ring buffer (64 entries, training-only). OpenSpiel uses its own `State::history_` (full action sequence). They don't need to match — the observation parity test validates the tensor encoding is identical, not the history representation.
- **Self-play schedule:** V1 uses single shared policy with player rotation. Population/league is a later concern.
- **C vs C++ engines:** Fully separate implementations. No shared engine code. Parity enforced by cross-framework fuzz tests.
- **PufferLib integration:** Out-of-tree, pure C (no Cython), CPython extension via `env_binding.h`.
- **OpenSpiel integration:** Git submodule, dynamic `RegisterGame()`, standalone CMake build.

### Remaining Open Questions

- **6-player Deep CFR:** The game tree may be too large even for Deep CFR. Consider starting with 2-player for CFR validation and 6-player for PPO only.
- **Assassination coin timing:** Standard rules say coins deducted on declaration (even if blocked). Verify and ensure identical in both implementations.
- **Reward shaping for 6-player:** Terminal is +1/-1 for now. Placement-based rewards (e.g., last eliminated gets -0.2, first gets -1.0) and intermediate shaping are deferred but the reward path must be easy to modify.

### Things To Not Forget

- **Reveal-shuffle-redraw on successful challenge defense** is a state update inside `resolve_challenge()`, not a player decision. It consumes RNG (PufferLib) or is a chance node (OpenSpiel) and changes the defender's hidden card.
- **Dead players are skipped everywhere.** Pre-set their bits in `responded_mask`. Skip in turn advancement. Never present as targets. For games with <6 players, unused slots are pre-killed at init.
- **10+ coin forced coup** is purely a mask constraint.
- **Foreign Aid blocks can come from any player** claiming Duke, not just a target.
- **Ambassador exchange returns cards face-down.** The observation function must not expose which cards were returned.
- **The history ring buffer is not part of the sim state.** It lives in the PufferLib wrapper. The sim is Markov; the buffer is for the network.
- **Coins and revealed cards are public information.** Always include in all players' observations.
- **Discard slots 28–31 are reused across LOSE_CARD and EXCHANGE_DISCARD phases.** Mutually exclusive by phase, but replay/logging code must be phase-aware when interpreting action indices.
- **Block phase cycling for Foreign Aid** iterates all living non-acting players, not just a single target.
- **The observation tensor encoding must be identical between PufferLib's `observe()` and OpenSpiel's `InformationStateTensor()`** so that neural networks are portable between frameworks. This is validated by the observation parity cross-framework test, not by shared code.
