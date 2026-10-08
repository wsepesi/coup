// Invariant tests + single-thread throughput for puffer/coup.h.
// Fake Agent buffers are wired the way src/puffercpu.c and the trainer do it.
//   make test-puffer   /   make bench-puffer
#include <time.h>
#include "coup.h"

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n  ", __FILE__, __LINE__, #cond); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
        exit(1); \
    } \
} while (0)

typedef struct {
    Env env;
    obs_t obs[MAX_PLAYERS * OBS_SIZE];
    float actions[MAX_PLAYERS];
    float rewards[MAX_PLAYERS];
    float terminals[MAX_PLAYERS];
    unsigned char masks[MAX_PLAYERS * COUP_NUM_ACTIONS];
    Dict kw;
} Harness;

static void harness_init(Harness* h, int num_players, int num_agents, int bot_policy,
        float reward_influence, unsigned int seed) {
    memset(h, 0, sizeof(*h));
    dict_set(&h->kw, "num_players", num_players);
    dict_set(&h->kw, "num_agents", num_agents);
    dict_set(&h->kw, "bot_policy", bot_policy);
    dict_set(&h->kw, "reward_influence", reward_influence);
    h->env.rng = seed;
    puf_init(&h->env, &h->kw);
    for (int s = 0; s < h->env.num_agents; s++) {
        Agent* a = &h->env.agents[s];
        a->observations = h->obs + s * OBS_SIZE;
        a->actions = h->actions + s;
        a->rewards = h->rewards + s;
        a->terminals = h->terminals + s;
        a->action_mask = h->masks + s * COUP_NUM_ACTIONS;
        a->policy = 0;
    }
    puf_reset(&h->env);
}

static uint32_t mask_bits(Harness* h, int s) {
    uint32_t m = 0;
    for (int i = 0; i < COUP_NUM_ACTIONS; i++) {
        CHECK(h->masks[s * COUP_NUM_ACTIONS + i] <= 1, "mask byte not 0/1");
        m |= (uint32_t)h->masks[s * COUP_NUM_ACTIONS + i] << i;
    }
    return m;
}

static int pick(uint32_t m, unsigned int* rng) {
    int k = rand_r(rng) % __builtin_popcount(m);
    while (k-- > 0) {
        m &= m - 1;
    }
    return __builtin_ctz(m);
}

// The observation of seat p must not change when anything p cannot see does:
// opponents' live cards and another player's exchange draw.
static void check_no_leak(Env* env, int p, const obs_t* obs, unsigned int* rng) {
    Game g2 = env->game;
    for (int trial = 0; trial < 4; trial++) {
        for (int q = 0; q < env->num_players; q++) {
            if (q == p) {
                continue;
            }
            if (player_card0_alive(&g2, q)) {
                set_player_card0_type(&g2, q, rand_r(rng) % 5);
            }
            if (player_card1_alive(&g2, q)) {
                set_player_card1_type(&g2, q, rand_r(rng) % 5);
            }
        }
        if (get_phase(&g2) == PHASE_EXCHANGE_DISCARD && get_turn_player(&g2) != p) {
            set_exchange_card0(&g2, rand_r(rng) % 5);
            set_exchange_card1(&g2, rand_r(rng) % 5);
        }
        obs_t o2[OBS_SIZE];
        coup_obs_write(&g2, &env->tracker, p, o2);
        CHECK(memcmp(obs, o2, OBS_SIZE) == 0, "obs of seat %d depends on hidden cards", p);
    }
    for (int i = 0; i < OBS_SIZE; i++) {
        CHECK(obs[i] <= 1, "obs byte %d = %d not 0/1", i, obs[i]);
    }
    // Own seat is relative 0 and shows own live card types.
    int t0 = player_card0_type(&env->game, p);
    CHECK(obs[2] == player_card0_alive(&env->game, p) && obs[3 + t0] == 1,
        "own card0 not visible");
}

static void check_relative_actions(void) {
    for (int np = 2; np <= MAX_PLAYERS; np++) {
        for (int me = 0; me < np; me++) {
            for (int a = 0; a < COUP_NUM_ACTIONS; a++) {
                int abs_a = coup_action_to_abs(a, me, np);
                CHECK(coup_action_to_rel(abs_a, me, np) == a, "rel/abs roundtrip a=%d", a);
                if (coup_action_is_targeted(a) && (a - ACT_COUP_P0) % 6 < np) {
                    int rel = (a - ACT_COUP_P0) % 6;
                    int tgt = (abs_a - ACT_COUP_P0) % 6;
                    CHECK(tgt == (me + rel) % np, "target seat np=%d me=%d a=%d", np, me, a);
                } else if (!coup_action_is_targeted(a)) {
                    CHECK(abs_a == a, "untargeted action changed");
                }
            }
        }
    }
    printf("ok   relative action conversion (np 2..6, all seats, all actions)\n");
}

// Run full-table selfplay with random masked actions and check every step.
static void check_selfplay(int np, int episodes, float reward_influence) {
    Harness* h = (Harness*)calloc(1, sizeof(Harness));
    harness_init(h, np, MAX_PLAYERS, BOT_HONEST, reward_influence, 1000 + np);
    Env* env = &h->env;
    CHECK(env->num_agents == np, "num_agents %d != num_players %d", env->num_agents, np);
    unsigned int rng = 7 + np;
    int done = 0;
    int first_mover_counts = 0;
    int agent0_first = 0;
    float ep_sum[MAX_PLAYERS] = {0};
    long steps = 0;
    while (done < episodes) {
        Game* g = &env->game;
        int active = get_active_player_ext(g);
        CHECK(!is_done(g) && active >= 0, "env exposed a finished/chance state");
        if (g->turn_count == 0 && get_phase(g) == PHASE_MAIN_ACTION
                && get_pending_action(g) == 0 && env->tracker.len == 0) {
            first_mover_counts++;
            agent0_first += env->rot == 0;
        }
        int live = 0;
        for (int s = 0; s < np; s++) {
            int p = (s + env->rot) % np;
            uint32_t m = mask_bits(h, s);
            if (p == active) {
                CHECK(m == coup_valid_actions_rel(g, p), "active mask mismatch");
                CHECK(__builtin_popcount(m) == __builtin_popcount(get_valid_actions(g)),
                    "relative mask lost actions");
                for (int a = 0; a < COUP_NUM_ACTIONS; a++) {
                    if ((m >> a) & 1) {
                        int abs_a = coup_action_to_abs(a, p, np);
                        CHECK((get_valid_actions(g) >> abs_a) & 1, "rel action invalid");
                    }
                }
            } else {
                CHECK(m == 1u << ACT_PASS, "inactive seat mask is not PASS-only");
            }
            live += m != 1u << ACT_PASS;
            check_no_leak(env, p, h->obs + s * OBS_SIZE, &rng);
            h->actions[s] = pick(m, &rng);
        }
        CHECK(live == 1, "%d seats with a non-PASS mask", live);
        puf_step(env);
        steps++;
        int term = 0;
        float sum = 0;
        for (int s = 0; s < np; s++) {
            term += h->terminals[s] > 0.5f;
            sum += h->rewards[s];
            ep_sum[s] += h->rewards[s];
        }
        CHECK(term == 0 || term == np, "only %d of %d seats terminal", term, np);
        if (term == 0) {
            for (int s = 0; s < np; s++) {
                CHECK(h->rewards[s] <= 0.0f, "positive mid-game reward");
                CHECK(reward_influence != 0.0f || h->rewards[s] == 0.0f,
                    "mid-game reward without shaping");
            }
            continue;
        }
        if (reward_influence == 0.0f) {
            CHECK(fabsf(sum) < 1e-5f, "terminal rewards sum to %f", sum);
            int winners = 0;
            for (int s = 0; s < np; s++) {
                if (h->rewards[s] == 1.0f) {
                    winners++;
                } else {
                    CHECK(fabsf(h->rewards[s] + 1.0f / (np - 1)) < 1e-6f,
                        "loser reward %f", h->rewards[s]);
                }
            }
            CHECK(winners == 1, "%d winners", winners);
        } else {
            // Episode return: winner 1, loser -1/(n-1), minus shaping per card.
            int winners = 0;
            for (int s = 0; s < np; s++) {
                winners += ep_sum[s] > 0.0f;
                CHECK(ep_sum[s] >= -1.0f / (np - 1) - 2 * reward_influence - 1e-5f,
                    "episode return %f too low", ep_sum[s]);
            }
            CHECK(winners == 1, "%d positive returns", winners);
        }
        memset(ep_sum, 0, sizeof(ep_sum));
        done++;
    }
    Log* l = &env->log;
    CHECK((int)l->n == episodes, "log.n %f != %d", l->n, episodes);
    CHECK(l->invalid == 0, "masked random play produced %f invalid", l->invalid);
    float win = l->perf / l->n;
    float first = (float)agent0_first / first_mover_counts;
    printf("ok   selfplay np=%d shaping=%.2f: %d games, %.1f steps/game, %.1f turns, "
        "agent0 win %.3f (1/n=%.3f), agent0 first %.3f, timeouts %.4f, "
        "challenge %.3f, bluff %.3f\n",
        np, reward_influence, episodes, (float)steps / episodes, l->turns / l->n, win,
        1.0f / np, first, l->timeout_rate / l->n, l->challenges / l->challenge_opps,
        l->bluffs / l->claims);
    CHECK(fabsf(win - 1.0f / np) < 0.05f, "agent 0 win rate far from 1/n");
    CHECK(fabsf(first - 1.0f / np) < 0.05f, "seat rotation biased");
    free(h);
}

// Learner vs bots: only agent 0 exists and it is always the seat to act.
static void check_bots(int np, int bot_policy, int episodes) {
    Harness* h = (Harness*)calloc(1, sizeof(Harness));
    harness_init(h, np, 1, bot_policy, 0.0f, 99 + np);
    Env* env = &h->env;
    CHECK(env->num_agents == 1, "bot table should have 1 agent");
    unsigned int rng = 3;
    while (env->log.n < episodes) {
        CHECK(get_active_player_ext(&env->game) == env->rot, "agent 0 not to act");
        uint32_t m = mask_bits(h, 0);
        CHECK(m == coup_valid_actions_rel(&env->game, env->rot), "bot-table mask");
        h->actions[0] = pick(m, &rng);
        puf_step(env);
        if (h->terminals[0] > 0.5f) {
            CHECK(h->rewards[0] == 1.0f || fabsf(h->rewards[0] + 1.0f / (np - 1)) < 1e-6f,
                "bot-table terminal reward %f", h->rewards[0]);
        }
    }
    static const char* names[] = {"random", "honest", "counting"};
    printf("ok   random agent vs %s bots np=%d: win %.3f over %d games\n",
        names[bot_policy], np,
        env->log.perf / env->log.n, episodes);
    free(h);
}

// Unmasked garbage actions: substituted, counted, never corrupt the game.
static void check_invalid(int np, int steps) {
    Harness* h = (Harness*)calloc(1, sizeof(Harness));
    harness_init(h, np, MAX_PLAYERS, BOT_RANDOM, 0.0f, 5);
    Env* env = &h->env;
    unsigned int rng = 11;
    for (int t = 0; t < steps; t++) {
        for (int s = 0; s < np; s++) {
            h->actions[s] = (float)(rand_r(&rng) % 40) - 4.0f;
        }
        puf_step(env);
        Game* g = &env->game;
        int cards = deck_total(g);
        for (int p = 0; p < np; p++) {
            cards += 2;
        }
        int in_exchange = get_phase(g) == PHASE_EXCHANGE_DISCARD;
        CHECK(cards + 2 * in_exchange == 15 || cards == 15,
            "card conservation broken: %d", cards);
        CHECK(!is_done(g), "finished game not reset");
    }
    Log* l = &env->log;
    float rate = (l->invalid + env->ep.invalid) / (l->decisions + env->ep.decisions);
    CHECK(rate > 0.5f, "invalid actions not counted (rate %f)", rate);
    printf("ok   unmasked random actions np=%d: %d steps, invalid rate %.3f, %0.f games\n",
        np, steps, rate, l->n);
    free(h);
}

static void check_vec_init(int total, int buffers, int policies, int np, int na) {
    Dict vk = {0};
    Dict ek = {0};
    dict_set(&vk, "total_agents", total);
    dict_set(&vk, "num_buffers", buffers);
    dict_set(&vk, "num_policies", policies);
    dict_set(&ek, "num_players", np);
    dict_set(&ek, "num_agents", na);
    dict_set(&ek, "bot_policy", 1);
    dict_set(&ek, "reward_influence", 0);
    int starts[16];
    int counts[16];
    int num_envs = 0;
    Env* envs = my_vec_init(&num_envs, starts, counts, &vk, &ek);
    int sizes[MAX_PLAYERS + 1] = {0};
    for (int b = 0; b < buffers; b++) {
        int agents = 0;
        for (int e = starts[b]; e < starts[b] + counts[b]; e++) {
            Env* env = &envs[e];
            Env* ref = &envs[e - starts[b]];
            CHECK(env->num_agents == ref->num_agents && env->num_players == ref->num_players,
                "buffer %d layout differs", b);
            for (int s = 0; s < env->num_agents; s++) {
                CHECK(env->agents[s].policy == ref->agents[s].policy, "policy layout");
                CHECK(env->agents[s].policy < (policies > 1 ? policies : 2), "policy id");
            }
            CHECK(env->num_agents >= 1 && env->num_agents <= env->num_players, "agents");
            agents += env->num_agents;
            sizes[env->num_players]++;
        }
        CHECK(agents == total / buffers, "buffer %d has %d agents, want %d",
            b, agents, total / buffers);
    }
    printf("ok   my_vec_init total=%d buffers=%d policies=%d np=%d na=%d: %d envs "
        "(tables 2..6: %d %d %d %d %d)\n", total, buffers, policies, np, na, num_envs,
        sizes[2], sizes[3], sizes[4], sizes[5], sizes[6]);
    free(envs);
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// Policy stand-in: active seat picks a random legal action, others PASS.
static void bench(int np, int num_agents, int bot_policy, double seconds) {
    enum { ENVS = 256 };
    Harness* hs = (Harness*)calloc(ENVS, sizeof(Harness));
    for (int i = 0; i < ENVS; i++) {
        harness_init(&hs[i], np, num_agents, bot_policy, 0.0f, i + 1);
    }
    unsigned int rng = 1;
    long env_steps = 0;
    long agent_steps = 0;
    double t0 = now();
    double t = t0;
    while (t - t0 < seconds) {
        for (int i = 0; i < ENVS; i++) {
            Harness* h = &hs[i];
            int s = coup_agent_of(&h->env, get_active_player_ext(&h->env.game));
            h->actions[s] = pick(mask_bits(h, s), &rng);
            puf_step(&h->env);
            agent_steps += h->env.num_agents;
        }
        env_steps += ENVS;
        t = now();
    }
    double dt = t - t0;
    float games = 0;
    for (int i = 0; i < ENVS; i++) {
        games += hs[i].env.log.n;
    }
    printf("bench np=%s agents=%s bots=%-9s %6.2fM env-steps/s  %6.2fM agent-steps/s  "
        "%7.0fK games/s\n",
        np ? (char[2]){'0' + np, 0} : "mix", num_agents >= 6 ? "all" : "1",
        num_agents >= 6 ? "-" : bot_policy == BOT_COUNTING ? "counting" : "other",
        env_steps / dt / 1e6, agent_steps / dt / 1e6, games / dt / 1e3);
    free(hs);
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "bench") == 0) {
        double secs = argc > 2 ? atof(argv[2]) : 3.0;
        printf("single thread, 256 envs, random masked actions (includes action pick)\n");
        for (int np = 2; np <= MAX_PLAYERS; np += 2) {
            bench(np, MAX_PLAYERS, BOT_RANDOM, secs);
        }
        bench(0, MAX_PLAYERS, BOT_RANDOM, secs);
        bench(4, 1, BOT_COUNTING, secs);
        return 0;
    }
    check_relative_actions();
    for (int np = 2; np <= MAX_PLAYERS; np++) {
        check_selfplay(np, 3000, 0.0f);
    }
    check_selfplay(4, 500, 0.1f);
    for (int np = 2; np <= MAX_PLAYERS; np += 2) {
        for (int bot = 0; bot < HEURISTIC_NUM_LEVELS; bot++) {
            check_bots(np, bot, 2000);
        }
    }
    check_invalid(3, 20000);
    check_vec_init(8192, 2, 2, 0, 6);
    check_vec_init(8192, 4, 3, 5, 6);
    check_vec_init(1000, 2, 2, 4, 6);
    check_vec_init(4096, 1, 1, 6, 1);
    check_vec_init(6, 1, 1, 0, 6);
    printf("all coup env tests passed\n");
    return 0;
}
