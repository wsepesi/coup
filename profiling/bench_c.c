/*
 * bench_c.c — Standalone C benchmark for the Coup game engine.
 *
 * Measures games/second with configurable player count, policy, thread count,
 * and duration. Supports OpenMP or pthreads for multi-threaded benchmarks.
 *
 * Usage:
 *   ./bench_c --players 2|6 --policy random|heuristic --threads N --duration S
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

typedef enum { POLICY_RANDOM, POLICY_HEURISTIC } Policy;

typedef struct {
    int num_players;
    Policy policy;
    double duration;
    uint64_t seed;
    /* outputs */
    uint64_t games;
    uint64_t total_steps;
} WorkerArgs;

/* Max steps before declaring a draw and re-dealing.
 * Prevents infinite loops from heuristic deadlocks (e.g., all-Captain games). */
#define MAX_STEPS_PER_GAME 10000

/* Play one complete game, return number of steps */
static inline int play_game(Game *g, Policy policy) {
    int steps = 0;
    while (!is_done(g) && steps < MAX_STEPS_PER_GAME) {
        int action;
        if (policy == POLICY_HEURISTIC) {
            action = heuristic_choose_action(g);
        } else {
            action = random_valid_action(g);
        }
        step_with_rng(g, action);
        steps++;
    }
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

    while (wall_time() - start < args->duration) {
        int steps = play_game(&g, args->policy);
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
    fprintf(stderr, "Usage: %s [--players 2|6] [--policy random|heuristic] "
                    "[--threads N] [--duration S]\n", prog);
    exit(1);
}

int main(int argc, char **argv) {
    int num_players = 2;
    Policy policy = POLICY_RANDOM;
    int num_threads = 1;
    double duration = 10.0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--players") == 0 && i + 1 < argc) {
            num_players = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "heuristic") == 0) policy = POLICY_HEURISTIC;
            else if (strcmp(argv[i], "random") == 0) policy = POLICY_RANDOM;
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
    double avg_len = total_games > 0 ? (double)total_steps / (double)total_games : 0.0;

    printf("engine=c players=%d policy=%s threads=%d games=%llu duration=%.2f gps=%.1f avg_len=%.1f\n",
           num_players,
           policy == POLICY_HEURISTIC ? "heuristic" : "random",
           num_threads,
           (unsigned long long)total_games,
           elapsed,
           gps,
           avg_len);

    free(workers);
    return 0;
}
