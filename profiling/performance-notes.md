# Performance Improvement Notes

See the framework-specific notes:
- [PufferLib performance](performance-puffer.md) — observation generation bottleneck, incremental updates, what NOT to thread
- [OpenSpiel performance](performance-openspiel.md) — heap alloc overhead, explicit chance nodes, when to use which engine

## Raw C Engine

The raw C engine (bench_c) is already near-optimal:
- 56-byte game struct, zero heap allocation, xoshiro256** PRNG
- ~3M games/sec single-threaded (2p random), ~21M at 10 cores
- Scales linearly with threads (embarrassingly parallel)
- Only optimization: OpenMP on Linux (DGX Spark) for slightly better thread management than pthreads

The raw C benchmark exists to establish the throughput ceiling. The gap between raw C and PufferLib/OpenSpiel is framework overhead, not game logic.
