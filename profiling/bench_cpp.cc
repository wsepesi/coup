/*
 * bench_cpp.cc — Standalone C++ benchmark for the Coup game via OpenSpiel.
 *
 * Measures games/second with configurable player count, policy, thread count,
 * and duration.
 *
 * Usage:
 *   ./bench_cpp --players 2|6 --policy random|heuristic --threads N --duration S
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "open_spiel/spiel.h"

// Action priority for heuristic (highest to lowest).
// Coup > Assassinate > Tax > Steal > Exchange > Foreign Aid > Block > Discard > Income > Pass
// Never challenge (22 omitted).
static const int kHeuristicPriority[] = {
    4, 5, 6, 7, 8, 9,          // Coup targets
    16, 17, 18, 19, 20, 21,    // Assassinate targets
    2,                          // Tax
    10, 11, 12, 13, 14, 15,    // Steal targets
    3,                          // Exchange
    1,                          // Foreign Aid
    24, 25, 26, 27,            // Block actions
    28, 29, 30, 31,            // Discard slots
    0,                          // Income
    23,                         // Pass
};
static const int kHeuristicPrioritySize =
    sizeof(kHeuristicPriority) / sizeof(kHeuristicPriority[0]);

static int heuristic_pick(const std::vector<open_spiel::Action>& legal) {
    // Build a set (using a bitmask since actions are 0-31)
    uint32_t mask = 0;
    for (auto a : legal) mask |= (1u << a);

    for (int i = 0; i < kHeuristicPrioritySize; i++) {
        if (mask & (1u << kHeuristicPriority[i]))
            return kHeuristicPriority[i];
    }
    return legal[0];  // fallback
}

enum Policy { RANDOM, HEURISTIC };

struct WorkerResult {
    uint64_t games = 0;
    uint64_t steps = 0;
};

static void worker_fn(const std::shared_ptr<const open_spiel::Game>& game,
                       Policy policy, double duration, uint64_t seed,
                       WorkerResult* result) {
    std::mt19937 rng(seed);
    uint64_t games = 0;
    uint64_t total_steps = 0;

    auto start = std::chrono::steady_clock::now();
    auto deadline = start + std::chrono::duration<double>(duration);

    while (std::chrono::steady_clock::now() < deadline) {
        auto state = game->NewInitialState();
        uint64_t steps = 0;

        while (!state->IsTerminal()) {
            if (state->IsChanceNode()) {
                auto outcomes = state->ChanceOutcomes();
                // Sample from weighted outcomes
                double r = std::uniform_real_distribution<double>(0.0, 1.0)(rng);
                double cumulative = 0.0;
                open_spiel::Action chosen = outcomes.back().first;
                for (const auto& [action, prob] : outcomes) {
                    cumulative += prob;
                    if (r <= cumulative) {
                        chosen = action;
                        break;
                    }
                }
                state->ApplyAction(chosen);
            } else {
                auto legal = state->LegalActions();
                open_spiel::Action action;
                if (policy == HEURISTIC) {
                    action = heuristic_pick(legal);
                } else {
                    std::uniform_int_distribution<int> dist(0, (int)legal.size() - 1);
                    action = legal[dist(rng)];
                }
                state->ApplyAction(action);
            }
            steps++;
        }

        games++;
        total_steps += steps;
    }

    result->games = games;
    result->steps = total_steps;
}

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " [--players 2|6] [--policy random|heuristic]"
              << " [--threads N] [--duration S]\n";
    std::exit(1);
}

int main(int argc, char** argv) {
    int num_players = 2;
    Policy policy = RANDOM;
    int num_threads = 1;
    double duration = 10.0;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--players") == 0 && i + 1 < argc) {
            num_players = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            i++;
            if (std::strcmp(argv[i], "heuristic") == 0) policy = HEURISTIC;
            else if (std::strcmp(argv[i], "random") == 0) policy = RANDOM;
            else usage(argv[0]);
        } else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            num_threads = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration = std::atof(argv[++i]);
        } else {
            usage(argv[0]);
        }
    }

    if (num_threads <= 0) {
        num_threads = (int)std::thread::hardware_concurrency();
        if (num_threads <= 0) num_threads = 4;
    }

    auto game = open_spiel::LoadGame(
        "coup", {{"players", open_spiel::GameParameter(num_players)}});

    std::vector<WorkerResult> results(num_threads);
    std::vector<std::thread> threads;

    auto wall_start = std::chrono::steady_clock::now();

    for (int t = 0; t < num_threads; t++) {
        uint64_t seed = (uint64_t)t * 6364136223846793005ULL + 1442695040888963407ULL;
        threads.emplace_back(worker_fn, std::cref(game), policy, duration,
                             seed, &results[t]);
    }

    for (auto& t : threads) t.join();

    auto wall_end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(wall_end - wall_start).count();

    uint64_t total_games = 0, total_steps = 0;
    for (const auto& r : results) {
        total_games += r.games;
        total_steps += r.steps;
    }

    double gps = (double)total_games / elapsed;
    double avg_len = total_games > 0 ? (double)total_steps / (double)total_games : 0.0;

    std::cout << "engine=cpp players=" << num_players
              << " policy=" << (policy == HEURISTIC ? "heuristic" : "random")
              << " threads=" << num_threads
              << " games=" << total_games
              << " duration=" << std::fixed << elapsed
              << " gps=" << std::fixed << gps
              << " avg_len=" << std::fixed << avg_len
              << "\n";

    return 0;
}
