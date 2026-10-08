# Profiling

Benchmarks for both game engines and their framework integrations.

## Run All Benchmarks

```bash
./run_benchmarks.sh
```

## Individual Benchmarks

```bash
# C engine (compile + run)
cc -O3 -o bench_c bench_c.c ../c_engine/coup_core.c -I../c_engine -lm
./bench_c

# C++ / OpenSpiel adapter (built by make test-cpp)
../cpp_engine/build/bench_cpp

# PufferLib 5.0 env (agent-steps/sec, single thread)
make -C .. bench-puffer
```

## Results

See `results.md` for latest numbers. On an M4 Mac Mini:

- **C engine**: ~2.97M games/sec (1 core), ~21M games/sec (10 cores)
- **PufferLib 5.0 env** (`puffer/coup.h`): ~9M agent-steps/sec single thread (all seats are agents; 4.5M env-steps/s at 2p, 1.5M at 6p)
- **OpenSpiel**: see `performance-openspiel.md`

## Files

- `bench_c.c` -- C engine microbenchmark
- `bench_cpp.cc` -- OpenSpiel adapter microbenchmark
- `../puffer/test_coup_env.c` -- PufferLib env tests + benchmark (`make bench-puffer`)
- `results.md` -- Consolidated results
- `performance-*.md` -- Framework-specific notes (`performance-puffer.md` describes the removed 3.0-era binding)
