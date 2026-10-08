# C Engine

Pure C implementation of Coup and the only implementation of the rules in this repo. The game state is bit-packed into 56 bytes (24 bytes of fields plus 32 bytes of RNG). It is built for high-throughput RL training through PufferLib, and the OpenSpiel adapter, WASM and TUI all wrap it.

## Build

```bash
# Shared library (for TUI/WASM)
cc -shared -O3 -o libcoup.dylib coup_core.c text_render.c

# Tests
make test-c    # from repo root: core, PRNG and search-API tests
make test-san  # the same under ASan + UBSan
```

## Key Files

- `coup_core.h`: Game struct, enums, the 32-action space defines.
- `coup_core.c`: game logic (`step_deterministic()`, `step_with_rng()`, `get_valid_actions()`, chance nodes) and the legacy `observe()`.
- `coup_obs.h`: egocentric uint8 RL observation and `CoupObsTracker` (header-only).
- `coup_search.h` / `coup_search.c`: opt-in search API (see below).
- `text_render.c`: human-readable rendering of the game state.
- `prng.h`: xoshiro256** PRNG (header-only).
- `history.h`: ring buffer used by the legacy observation (header-only).
- `heuristic.h`: heuristic bots (header-only).

## Design

- Always 6 player slots; unused slots are pre-killed.
- One step = one decision by one player.
- Stochasticity is factored: `chance_outcomes()`/`apply_chance()` for CFR-style enumeration, `step_with_rng()` for sampling.
- Action masking uses the 32-bit bitmask from `get_valid_actions()`.

See the comments in `coup_core.h` for the state layout, phase machine and action indices, and `coup_obs.h` for the RL observation layout.

## Search API (`coup_search.h`)

These are building blocks for imperfect-information search directly on the engine, with no OpenSpiel needed. The API lives in its own translation unit and in side structs. It changes neither the `Game` layout nor the step hot path; link `coup_search.c` only if you use it. The OpenSpiel adapter uses it for its event log and resampler.

1. **History log.**
   - `CoupLogEvent` records, for each action, the phase before it, the actor, the action or card, and an info byte: the revealed card, the challenge outcome, the redraw slot or the discarded type. Challenges also record the claimant and the role.
   - `coup_event_make()` records the event against the state before the action. `coup_step_logged()` and `coup_step_logged_rng()` record and apply in one call; the `_rng` variant is bit-identical to `step_with_rng()`.
   - `CoupLog` is a fixed-capacity log of `COUP_LOG_CAPACITY` = 4812 events, a proven bound of `2n + MAX_TURNS * (3n + 3 + 3)`.
   - `coup_log_same_view()` checks whether two logs look identical to one player, the C equivalent of comparing infostate strings.
2. **Public state.**
   - `CoupPublicState` holds everything every player sees. Engine fields that are only meaningful in some phases are normalized.
   - `coup_public_pack()` gives an exact 126-bit packing and `coup_public_key()` a 64-bit hash.
   - `coup_public_valid_actions()` gives the legal actions. Legal masks never depend on hidden cards; the one exception is an exchanger's second pick, which depends on their own private first pick.
3. **Private hands.** A hand is the unordered multiset of a player's live hidden cards: 15, 5 or 1 hands for 2, 1 or 0 cards.
   - `coup_hand_index()`, `coup_hand_cards()`, `coup_hand_add()`, `coup_hand_remove()` and `coup_hand_multiplicity()` work on hand indices. `coup_game_hand()` reads a player's hand from a Game.
   - `coup_canonicalize_hands()` sorts each player's two live cards into slot order.
   - `coup_deck_from_hidden()` computes the deck: 3 per role, minus hidden cards, face-up cards and pending exchange draws.
4. **Construction.** `coup_game_from_public(pub, hands, exchange, &g)` and `coup_game_from_public_idx()` build a Game. They return -1 if the counts are inconsistent. Given the true assignment, the Game behaves exactly like the original; tests compare the round trip step by step to the end of the game.
5. **Belief helpers.** `coup_hand_challenge_consistent()` checks a hand against a challenge outcome. `coup_hand_remove()` applies a reveal (`{r,x}` minus `r` is `{x}`) and returns -1 if the hand is inconsistent.
6. **Resampling.** `coup_resample()` and `coup_resample_game()` sample a full history that one player cannot tell from the real one. They return the replayable action list or the Game itself. This is the algorithm that used to live in the C++ adapter, plus a fallback repair for very long, exchange-heavy histories. It takes a caller-supplied `double uniform(void*)`; `coup_xoshiro_uniform` adapts a `Xoshiro256*`. It takes about 1.6 µs per call on random 6-player games.

**Canonical slot order.** With two live cards, slot order is only a label. The rules are symmetric under swapping a player's two live slots. Three things do name physical slots:

- `LOSE_CARD` and exchange-discard actions name a slot.
- The public redraw slot is the slot that held the proven role (slot 0 if both did).
- An exchange keeps the remaining cards in slot order.

A canonicalized Game is therefore the original up to relabeling: map slot actions by card type with `coup_hand_slot_of()`, and never replay a recorded list of slot-naming actions onto it. Mid-exchange, after a first pick on a hand slot, the exchanger is left unsorted, because the ordered first-before-second pick encoding is not symmetric. Beliefs over unordered hands ignore the positional facts the real engine leaks: which slot was lost, and that slot 0 cannot hold the role when slot 1 is redrawn. `coup_resample` respects those facts exactly.

**How ReBeL / Player-of-Games style search would use this.**

- A search node is a `CoupPublicState` plus, for each player, a belief over the 15 two-card hands (5 or 1 hands once cards are lost). The search code owns the beliefs and policies.
- Legal actions come from the public state alone, so a policy is one row per (public state, hand).
- Decision transitions are the same for every hand except where the event reveals something. For those, update beliefs with the consistency helpers, and Bayes-update with the actor's policy.
- Chance and private transitions (redraws, exchange draws) are evaluated by instantiating Games for sampled or enumerated hand assignments with `coup_game_from_public_idx`, then calling `chance_outcomes()` or `step_deterministic()`. The deck is the complement of all hands, so joint beliefs are not products of marginals. Whether to sample joints or approximate them is the search's choice.
- IS-MCTS and Ataraxos-style determinization use `coup_resample_game()` for one player's view instead.
