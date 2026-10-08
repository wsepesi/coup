/*
 * bench_c.c — Standalone C benchmark for the Coup game engine.
 *
 * Measures games/second with configurable player count, policy, thread count,
 * and duration. Supports OpenMP or pthreads for multi-threaded benchmarks.
 *
 * Usage:
 *   ./bench_c --players 2|6 --policy random|heuristic|counting --threads N
 *             --duration S [--mode game|env]
 *
 * Modes:
 *   game  raw engine throughput: policy + step_with_rng only.
 *   env   RL-env-shaped loop: every step additionally computes, for EVERY
 *         seat, coup_valid_actions_rel() + coup_obs_write() and records the
 *         event in a CoupObsTracker (what puffer/ does). This is the number
 *         that matters for training throughput.
 *
 * Output (one line, machine-parseable):
 *   engine=c players=2 policy=random threads=4 games=123456 duration=10.00 gps=12345.6 avg_len=42.3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "../c_engine/coup_core.h"
#include "../c_engine/heuristic.h"
#include "../c_engine/coup_obs.h"

/* ---------- Portable wall-clock time ---------- */

#if defined(_OPENMP)
#include <omp.h>
static double wall_time(void) { return omp_get_wtime(); }
#else
static double wall_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/* ---------- Threading abstraction ---------- */

#if !defined(_OPENMP)
#include <pthread.h>
#endif

typedef enum { POLICY_RANDOM, POLICY_HEURISTIC, POLICY_COUNTING } Policy;
typedef enum { MODE_GAME, MODE_ENV } Mode;

typedef struct {
    int num_players;
    Policy policy;
    Mode mode;
    double duration;
    uint64_t seed;
    /* outputs */
    uint64_t games;
    uint64_t total_steps;
} WorkerArgs;

/* Safety net only: MAX_TURNS already bounds every game. */
#define MAX_STEPS_PER_GAME 10000

static inline int choose(Game *g, Policy policy) {
    switch (policy) {
    case POLICY_HEURISTIC: return heuristic_choose_action_level(g, HEURISTIC_HONEST);
    case POLICY_COUNTING:  return heuristic_choose_action_level(g, HEURISTIC_COUNTING);
    default:               return heuristic_choose_action_level(g, HEURISTIC_RANDOM);
    }
}

/* Play one complete game, return number of steps */
static inline int play_game(Game *g, Policy policy) {
    int steps = 0;
    while (!is_done(g) && steps < MAX_STEPS_PER_GAME) {
        step_with_rng(g, choose(g, policy));
        steps++;
    }
    return steps;
}

/* Sink so the compiler cannot drop the observation work. */
static volatile uint32_t bench_sink;

/* Same, but doing the per-step work of a multi-agent RL env. */
static inline int play_game_env(Game *g, Policy policy, CoupObsTracker *t,
                                uint8_t *obs) {
    int steps = 0;
    int np = get_num_players(g);
    uint32_t acc = 0;
    coup_obs_tracker_reset(t);
    while (!is_done(g) && steps < MAX_STEPS_PER_GAME) {
        for (int s = 0; s < np; s++) {
            acc += coup_valid_actions_rel(g, s);
            coup_obs_write(g, t, s, obs + s * COUP_OBS_SIZE);
        }
        acc += obs[(steps * 7) % (np * COUP_OBS_SIZE)];
        int actor = get_active_player(g);
        int a = choose(g, policy);
        coup_obs_tracker_record(t, g, actor, a);
        step_with_rng(g, a);
        steps++;
    }
    bench_sink += acc;
    return steps;
}

static void worker_run(WorkerArgs *args) {
    Game g;
    Xoshiro256 seed_rng;
    xoshiro256_seed(&seed_rng, args->seed);

    uint64_t ds = xoshiro256_next(&seed_rng);
    uint64_t ps = xoshiro256_next(&seed_rng);
    game_init(&g, args->num_players, ds, ps);

    uint64_t games = 0;
    uint64_t total_steps = 0;
    double start = wall_time();
    CoupObsTracker tracker;
    static _Thread_local uint8_t obs[MAX_PLAYERS * COUP_OBS_SIZE];

    while (wall_time() - start < args->duration) {
        int steps = args->mode == MODE_ENV
            ? play_game_env(&g, args->policy, &tracker, obs)
            : play_game(&g, args->policy);
        total_steps += (uint64_t)steps;
        games++;

        /* Re-init for next game */
        ds = xoshiro256_next(&g.rng);
        ps = xoshiro256_next(&g.rng);
        game_init(&g, args->num_players, ds, ps);
    }

    args->games = games;
    args->total_steps = total_steps;
}

#if !defined(_OPENMP)
static void *pthread_worker(void *arg) {
    worker_run((WorkerArgs *)arg);
    return NULL;
}
#endif

/* ---------- Main ---------- */

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [--players 2..6] [--policy random|heuristic|counting] "
                    "[--threads N] [--duration S] [--mode game|env]\n", prog);
    exit(1);
}

int main(int argc, char **argv) {
    int num_players = 2;
    Policy policy = POLICY_RANDOM;
    Mode mode = MODE_GAME;
    int num_threads = 1;
    double duration = 10.0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--players") == 0 && i + 1 < argc) {
            num_players = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "heuristic") == 0) policy = POLICY_HEURISTIC;
            else if (strcmp(argv[i], "counting") == 0) policy = POLICY_COUNTING;
            else if (strcmp(argv[i], "random") == 0) policy = POLICY_RANDOM;
            else usage(argv[0]);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "env") == 0) mode = MODE_ENV;
            else if (strcmp(argv[i], "game") == 0) mode = MODE_GAME;
            else usage(argv[0]);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            num_threads = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = atof(argv[++i]);
        } else {
            usage(argv[0]);
        }
    }

    if (num_players < 2 || num_players > 6) {
        fprintf(stderr, "Error: players must be 2-6\n");
        return 1;
    }

    /* Thread count 0 = all available cores */
    if (num_threads <= 0) {
#if defined(_OPENMP)
        num_threads = omp_get_max_threads();
#else
        num_threads = 4; /* safe default */
        {
            long n = sysconf(_SC_NPROCESSORS_ONLN);
            if (n > 0) num_threads = (int)n;
        }
#endif
    }

    /* Allocate worker args */
    WorkerArgs *workers = (WorkerArgs *)calloc((size_t)num_threads, sizeof(WorkerArgs));
    for (int t = 0; t < num_threads; t++) {
        workers[t].num_players = num_players;
        workers[t].policy = policy;
        workers[t].mode = mode;
        workers[t].duration = duration;
        workers[t].seed = (uint64_t)(t + 1) * 6364136223846793005ULL + 1442695040888963407ULL;
    }

    double start = wall_time();

#if defined(_OPENMP)
    omp_set_num_threads(num_threads);
    #pragma omp parallel for schedule(static)
    for (int t = 0; t < num_threads; t++) {
        worker_run(&workers[t]);
    }
#else
    if (num_threads == 1) {
        worker_run(&workers[0]);
    } else {
        pthread_t *threads = (pthread_t *)malloc((size_t)num_threads * sizeof(pthread_t));
        for (int t = 0; t < num_threads; t++) {
            pthread_create(&threads[t], NULL, pthread_worker, &workers[t]);
        }
        for (int t = 0; t < num_threads; t++) {
            pthread_join(threads[t], NULL);
        }
        free(threads);
    }
#endif

    double elapsed = wall_time() - start;

    /* Aggregate results */
    uint64_t total_games = 0;
    uint64_t total_steps = 0;
    for (int t = 0; t < num_threads; t++) {
        total_games += workers[t].games;
        total_steps += workers[t].total_steps;
    }

    double gps = (double)total_games / elapsed;
    double sps = (double)total_steps / elapsed;
    double avg_len = total_games > 0 ? (double)total_steps / (double)total_games : 0.0;
    static const char *policy_names[] = {"random", "heuristic", "counting"};

    printf("engine=c mode=%s players=%d policy=%s threads=%d games=%llu duration=%.2f "
           "gps=%.1f sps=%.0f avg_len=%.1f\n",
           mode == MODE_ENV ? "env" : "game",
           num_players,
           policy_names[policy],
           num_threads,
           (unsigned long long)total_games,
           elapsed,
           gps,
           sps,
           avg_len);

    free(workers);
    return 0;
}
