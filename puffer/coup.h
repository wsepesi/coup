// Coup for PufferLib 5.0. One Env is one table of 2-6 players. Every learning
// seat is an Agent on every step: the seat to act gets its legal actions in the
// mask, every other seat gets a mask with only PASS (23), so its row is a
// zero-entropy no-op that still feeds the RNN every public event. Seats past
// num_agents are scripted bots (bot_policy) that act inside puf_step.
//
// Observations and actions are egocentric (c_engine/coup_obs.h): seat-relative
// targets, so one policy plays any seat at any table size. install.sh links
// this file and the c_engine sources into ocean/coup/.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"
typedef unsigned char obs_t;
#define PUF_HAS_BOT_POLICY
#include "pufferenv.h"

#include "coup_core.c"
#include "heuristic.h"
#include "coup_obs.h"

// coup_core.h's OBS_SIZE (407) is the legacy float observation.
#undef OBS_SIZE
#define OBS_SIZE COUP_OBS_SIZE
#define ACT_SIZES {COUP_NUM_ACTIONS}
#define NUM_ATNS 1
#define PUF_STEPS_PER_SEC 4
#define MY_VEC_INIT

// Scripted opponents, weakest first: the [env] bot_policy ids and the rungs of
// [selfplay] eval_bots are c_engine/heuristic.h levels (0 random, 1 honest,
// 2 counting: honest + challenges provable bluffs).
#define BOT_RANDOM HEURISTIC_RANDOM
#define BOT_HONEST HEURISTIC_HONEST
#define BOT_COUNTING HEURISTIC_COUNTING

// Flat float struct. The trainer sums it over envs and divides by n (games).
struct Log {
    float perf;            // agent 0 win rate (vs bots in the eval ladder)
    float score;
    float policy_0_score;  // agent 0 is policy 0 in match/historical envs
    float draw_rate;       // always 0: the MAX_TURNS tiebreak picks a winner
    float win_over_chance; // mean of win * num_players; 1.0 = chance level
    float hist_wins;       // agent 0 wins in envs with a historical opponent
    float hist_n;
    float episode_return;  // agent 0
    float episode_length;  // puf_step calls
    float turns;           // main actions
    float timeout_rate;    // games decided by the MAX_TURNS tiebreak
    float num_players;
    float decisions;       // learning-agent decisions (non-dummy steps)
    float invalid;         // decisions whose action was outside the mask
    float challenge_opps;
    float challenges;
    float claims;          // role claims by learning agents
    float bluffs;          // claims without the role in hand
    float n;
};

struct Env {
    Log log;
    Agent agents[MAX_PLAYERS];
    int tag;
    int boundary_reached;
    int num_agents;
    unsigned int rng;
    Xoshiro256 bot_rng;
    Game game;
    CoupObsTracker tracker;
    int num_players;
    int bot_policy;
    int rot;  // agent s sits in game seat (s + rot) % num_players
    float reward_influence;
    Log ep;   // this game's counters, folded into log at game end
};

static inline void puf_set_bot_policy(Env* env, int bot_policy) {
    assert(bot_policy >= 0 && bot_policy < HEURISTIC_NUM_LEVELS);
    env->bot_policy = bot_policy;
}

static int coup_agent_of(Env* env, int seat) {
    return (seat - env->rot + env->num_players) % env->num_players;
}

static void coup_apply(Env* env, int p, int action) {
    Game* g = &env->game;
    int before[MAX_PLAYERS];
    for (int q = 0; q < env->num_players; q++) {
        before[q] = player_alive_cards(g, q);
    }
    coup_obs_tracker_record(&env->tracker, g, p, action);
    step_with_rng(g, action);
    if (env->reward_influence == 0.0f) {
        return;
    }
    for (int q = 0; q < env->num_players; q++) {
        int s = coup_agent_of(env, q);
        float r = -env->reward_influence * (before[q] - player_alive_cards(g, q));
        if (s >= env->num_agents || r == 0.0f) {
            continue;
        }
        env->agents[s].rewards[0] += r;
        if (s == 0) {
            env->ep.episode_return += r;
        }
    }
}

// Play scripted seats until a learning agent must act or the game ends.
static void coup_run_bots(Env* env) {
    Game* g = &env->game;
    while (!is_done(g)) {
        int p = get_active_player_ext(g);
        assert(p >= 0);
        if (coup_agent_of(env, p) < env->num_agents) {
            return;
        }
        coup_apply(env, p, heuristic_choose_action_rng(g, env->bot_policy, &env->bot_rng));
    }
}

static void coup_new_game(Env* env) {
    uint64_t seed = ((uint64_t)rand_r(&env->rng) << 31) ^ rand_r(&env->rng);
    uint64_t deal_seed = splitmix64(&seed);
    game_init(&env->game, env->num_players, deal_seed, splitmix64(&seed));
    // Bots draw from their own stream so they never perturb the card draws.
    xoshiro256_seed(&env->bot_rng, splitmix64(&seed));
    coup_obs_tracker_reset(&env->tracker);
    env->rot = rand_r(&env->rng) % env->num_players;
    memset(&env->ep, 0, sizeof(env->ep));
    coup_run_bots(env);
}

static void coup_write_obs(Env* env) {
    Game* g = &env->game;
    int active = get_active_player_ext(g);
    for (int s = 0; s < env->num_agents; s++) {
        Agent* a = &env->agents[s];
        int p = (s + env->rot) % env->num_players;
        coup_obs_write(g, &env->tracker, p, a->observations);
        uint32_t mask = p == active ? coup_valid_actions_rel(g, p) : 1u << ACT_PASS;
        for (int i = 0; i < COUP_NUM_ACTIONS; i++) {
            a->action_mask[i] = (mask >> i) & 1;
        }
    }
}

void puf_reset(Env* env) {
    for (int s = 0; s < env->num_agents; s++) {
        env->agents[s].rewards[0] = 0.0f;
        env->agents[s].terminals[0] = 0.0f;
    }
    coup_new_game(env);
    coup_write_obs(env);
}

static void coup_end_game(Env* env) {
    Game* g = &env->game;
    int n = env->num_players;
    int winner = get_winner(g);
    for (int s = 0; s < env->num_agents; s++) {
        float r = (s + env->rot) % n == winner ? 1.0f : -1.0f / (n - 1);
        env->agents[s].rewards[0] += r;
        env->agents[s].terminals[0] = 1.0f;
    }
    float win = env->rot == winner;
    Log* ep = &env->ep;
    ep->perf = win;
    ep->score = win;
    ep->policy_0_score = win;
    ep->win_over_chance = win * n;
    ep->hist_wins = env->tag > 0 ? win : 0.0f;
    ep->hist_n = env->tag > 0;
    ep->episode_return += win ? 1.0f : -1.0f / (n - 1);
    ep->turns = g->turn_count;
    ep->timeout_rate = g->turn_count >= MAX_TURNS;
    ep->num_players = n;
    ep->n = 1.0f;
    float* dst = (float*)&env->log;
    float* src = (float*)ep;
    for (int i = 0; i < (int)(sizeof(Log) / sizeof(float)); i++) {
        dst[i] += src[i];
    }
    if (env->tag > 0) {
        env->boundary_reached = 1;
    }
    coup_new_game(env);
}

void puf_step(Env* env) {
    Game* g = &env->game;
    for (int s = 0; s < env->num_agents; s++) {
        env->agents[s].rewards[0] = 0.0f;
        env->agents[s].terminals[0] = 0.0f;
    }
    env->ep.episode_length += 1.0f;
    int p = get_active_player_ext(g);
    int s = coup_agent_of(env, p);
    assert(p >= 0 && s < env->num_agents && !is_done(g));
    uint32_t mask = coup_valid_actions_rel(g, p);
    int a = env->agents[s].actions[0];
    if (a < 0 || a >= COUP_NUM_ACTIONS || !((mask >> a) & 1)) {
        env->ep.invalid += 1.0f;
        int k = rand_r(&env->rng) % __builtin_popcount(mask);
        for (a = __builtin_ctz(mask); k > 0; k--) {
            mask &= mask - 1;
            a = __builtin_ctz(mask);
        }
    }
    int abs_a = coup_action_to_abs(a, p, env->num_players);
    int role = coup_obs_action_role(abs_a);
    env->ep.decisions += 1.0f;
    env->ep.challenge_opps += (coup_valid_actions_rel(g, p) >> ACT_CHALLENGE) & 1;
    env->ep.challenges += a == ACT_CHALLENGE;
    env->ep.claims += role >= 0;
    env->ep.bluffs += role >= 0 && !player_has_card(g, p, role);
    coup_apply(env, p, abs_a);
    coup_run_bots(env);
    if (is_done(g)) {
        coup_end_game(env);
    }
    coup_write_obs(env);
}

// Shared by puf_init and my_vec_init. Seats past num_agents are bots.
static void coup_setup(Env* env, Dict* kwargs, int num_players, int num_agents,
        int hist_policy) {
    assert(num_players >= 2 && num_players <= MAX_PLAYERS);
    assert(num_agents >= 1 && num_agents <= num_players);
    env->num_players = num_players;
    env->num_agents = num_agents;
    puf_set_bot_policy(env, dict_get(kwargs, "bot_policy"));
    env->reward_influence = dict_get(kwargs, "reward_influence");
    // Agent 0 is the learner. In the hist_policy_percent tail the trainer
    // gives the other seats to a frozen checkpoint; elsewhere it forces 0.
    for (int s = 0; s < num_agents; s++) {
        env->agents[s].policy = s == 0 ? 0 : hist_policy;
    }
}

// num_players = 0 draws a table size in 2..6 per env (fixed for its life).
void puf_init(Env* env, Dict* kwargs) {
    int np = dict_get(kwargs, "num_players");
    if (np == 0) {
        np = 2 + rand_r(&env->rng) % 5;
    }
    int na = dict_get(kwargs, "num_agents");
    coup_setup(env, kwargs, np, na < np ? na : np, 1);
}

// Table sizes vary, so the default loop (puf_init until total_agents) can
// overshoot a buffer. Plan one buffer whose agents sum to exactly
// total_agents / num_buffers and repeat it, so every buffer has the same
// per-policy layout. Odd remainders become a smaller table.
Env* my_vec_init(int* num_envs_out, int* buffer_env_starts, int* buffer_env_counts,
        Dict* vec_kwargs, Dict* env_kwargs) {
    int num_buffers = dict_get(vec_kwargs, "num_buffers");
    int apb = (int)dict_get(vec_kwargs, "total_agents") / num_buffers;
    int num_hist = (int)dict_get(vec_kwargs, "num_policies") - 1;
    int cfg_np = dict_get(env_kwargs, "num_players");
    int cfg_na = dict_get(env_kwargs, "num_agents");
    assert(apb * num_buffers == (int)dict_get(vec_kwargs, "total_agents"));
    assert(apb >= 2 || cfg_na < 2);
    int* plan_np = (int*)calloc(apb, sizeof(int));
    int* plan_na = (int*)calloc(apb, sizeof(int));
    unsigned int plan_rng = 1;
    int per_buf = 0;
    int resized = 0;
    for (int left = apb; left > 0; per_buf++) {
        int np = cfg_np ? cfg_np : 2 + rand_r(&plan_rng) % 5;
        int na = cfg_na < np ? cfg_na : np;
        if (na == np) {
            if (left <= MAX_PLAYERS && (np > left || left - np == 1)) {
                np = left;
            } else if (left - np == 1) {
                np--;
            }
            resized += cfg_np && np != cfg_np;
            na = np;
        } else if (na > left) {
            na = left;
        }
        plan_np[per_buf] = np;
        plan_na[per_buf] = na;
        left -= na;
    }
    if (resized) {
        printf("coup: %d table(s) per buffer resized to fit %d agents/buffer\n",
            resized, apb);
    }
    Env* envs = (Env*)calloc(num_buffers * per_buf, sizeof(Env));
    for (int b = 0; b < num_buffers; b++) {
        buffer_env_starts[b] = b * per_buf;
        buffer_env_counts[b] = per_buf;
        for (int k = 0; k < per_buf; k++) {
            Env* env = &envs[b * per_buf + k];
            env->rng = b * per_buf + k;
            int hist = num_hist > 0 ? 1 + k % num_hist : 1;
            coup_setup(env, env_kwargs, plan_np[k], plan_na[k], hist);
        }
    }
    free(plan_np);
    free(plan_na);
    *num_envs_out = num_buffers * per_buf;
    return envs;
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "policy_0_score", log->policy_0_score);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "win_over_chance", log->win_over_chance);
    dict_set(out, "hist_win_rate", log->hist_n > 0 ? log->hist_wins / log->hist_n : 0);
    dict_set(out, "hist_frac", log->hist_n);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "turns", log->turns);
    dict_set(out, "timeout_rate", log->timeout_rate);
    dict_set(out, "num_players", log->num_players);
    dict_set(out, "invalid_rate",
        log->decisions > 0 ? log->invalid / log->decisions : 0);
    dict_set(out, "challenge_rate",
        log->challenge_opps > 0 ? log->challenges / log->challenge_opps : 0);
    dict_set(out, "bluff_rate", log->claims > 0 ? log->bluffs / log->claims : 0);
    dict_set(out, "claims_per_game", log->claims);
    dict_set(out, "n", log->n);
}

static const char* COUP_CARD_NAMES[5] = {"Duke", "Assassin", "Captain", "Ambassador",
    "Contessa"};
static const char* COUP_PHASE_NAMES[10] = {"deal", "redraw", "exchange draw", "main action",
    "challenge action", "block", "challenge block", "lose card", "exchange discard",
    "resolve"};

static const char* coup_action_name(int a, char* buf, int n) {
    static const char* names[] = {"income", "foreign aid", "tax", "exchange"};
    static const char* tail[] = {"challenge", "pass", "block (contessa)",
        "block (captain)", "block (ambassador)", "block (duke)"};
    if (a == COUP_EVENT_HIDDEN) {
        snprintf(buf, n, "(discards in secret)");
    } else if (a < ACT_COUP_P0) {
        snprintf(buf, n, "%s", names[a]);
    } else if (a < ACT_STEAL_P0) {
        snprintf(buf, n, "coup P%d", a - ACT_COUP_P0);
    } else if (a < ACT_ASSASSINATE_P0) {
        snprintf(buf, n, "steal from P%d", a - ACT_STEAL_P0);
    } else if (a < ACT_CHALLENGE) {
        snprintf(buf, n, "assassinate P%d", a - ACT_ASSASSINATE_P0);
    } else if (a < ACT_DISCARD_SLOT0) {
        snprintf(buf, n, "%s", tail[a - ACT_CHALLENGE]);
    } else {
        snprintf(buf, n, "discard slot %d", a - ACT_DISCARD_SLOT0);
    }
    return buf;
}

// Spectator view: every hand face up, dead cards in red, agent 0 marked.
void puf_render(Env* env) {
    if (!IsWindowReady()) {
        InitWindow(720, 520, "PufferLib Coup");
        SetTargetFPS(30);
    }
    Game* g = &env->game;
    char line[160];
    char act[48];
    BeginDrawing();
    ClearBackground((Color){6, 24, 24, 255});
    snprintf(line, sizeof(line), "Turn %d   phase: %s", g->turn_count,
        COUP_PHASE_NAMES[get_phase(g)]);
    DrawText(line, 20, 16, 20, RAYWHITE);
    int active = get_active_player_ext(g);
    for (int p = 0; p < env->num_players; p++) {
        int s = coup_agent_of(env, p);
        int y = 56 + 40 * p;
        Color c = p == active ? YELLOW : RAYWHITE;
        snprintf(line, sizeof(line), "%sP%d %s  %2d coins", p == active ? ">" : " ",
            p, s == 0 ? "(agent 0)" : s < env->num_agents ? "(agent)" : "(bot)",
            player_coins(g, p));
        DrawText(line, 20, y, 20, c);
        int t0 = player_card0_type(g, p);
        int t1 = player_card1_type(g, p);
        DrawText(COUP_CARD_NAMES[t0], 360, y, 20,
            player_card0_alive(g, p) ? (Color){0, 187, 187, 255} : RED);
        DrawText(COUP_CARD_NAMES[t1], 520, y, 20,
            player_card1_alive(g, p) ? (Color){0, 187, 187, 255} : RED);
    }
    CoupObsTracker* t = &env->tracker;
    for (int i = 0; i < t->len; i++) {
        CoupEvent* e = &t->hist[(t->head + COUP_OBS_HIST - 1 - i) % COUP_OBS_HIST];
        snprintf(line, sizeof(line), "P%d: %s", e->actor,
            coup_action_name(e->action, act, sizeof(act)));
        DrawText(line, 20, 56 + 40 * MAX_PLAYERS + 22 * i, 18, i == 0 ? RAYWHITE : GRAY);
    }
    EndDrawing();
}

void puf_close(Env* env) {
    if (IsWindowReady()) {
        CloseWindow();
    }
}
