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

# C++ / OpenSpiel
uv run bench_openspiel.py

# PufferLib
uv run bench_pufferlib.py
```

## Results

See `results.md` for latest numbers. On an M4 Mac Mini:

- **C engine**: ~2.97M games/sec (1 core), ~21M games/sec (10 cores)
- **PufferLib**: ~1.2M SPS
- **OpenSpiel**: see `performance-openspiel.md`

## Files

- `bench_c.c` -- C engine microbenchmark
- `bench_cpp.cc` -- C++ engine microbenchmark
- `bench_openspiel.py` -- OpenSpiel framework benchmark
- `bench_pufferlib.py` -- PufferLib SPS benchmark
- `results.md` -- Consolidated results
- `performance-*.md` -- Framework-specific notes
