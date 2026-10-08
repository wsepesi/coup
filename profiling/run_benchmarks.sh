#!/usr/bin/env bash
#
# run_benchmarks.sh — Build and run the full Coup engine benchmark matrix.
#
# Usage:
#   ./profiling/run_benchmarks.sh [--machine "Mac Mini M4"] [--duration 10] [--skip-cpp] [--skip-puffer] [--skip-openspiel]
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---------- Defaults ----------
DURATION=10
MACHINE=""
SKIP_CPP=false
SKIP_PUFFER=false
SKIP_OPENSPIEL=false

# ---------- Parse args ----------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --machine)    MACHINE="$2"; shift 2 ;;
        --duration)   DURATION="$2"; shift 2 ;;
        --skip-cpp)   SKIP_CPP=true; shift ;;
        --skip-puffer) SKIP_PUFFER=true; shift ;;
        --skip-openspiel) SKIP_OPENSPIEL=true; shift ;;
        *) echo "Unknown arg: $1"; exit 1 ;;
    esac
done

# ---------- Platform detection ----------
OS=$(uname -s)
ARCH=$(uname -m)

if [[ "$OS" == "Darwin" ]]; then
    CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    CPU=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "unknown")
else
    CORES=$(nproc 2>/dev/null || echo 4)
    CPU=$(lscpu 2>/dev/null | grep "Model name" | sed 's/Model name:\s*//' || echo "unknown")
fi

CC_VERSION=$(cc --version 2>&1 | head -1)
DATE=$(date -u +"%Y-%m-%d %H:%M UTC")

if [[ -z "$MACHINE" ]]; then
    MACHINE=$(hostname)
fi

echo "=== Coup Engine Benchmark ==="
echo "Machine: $MACHINE"
echo "OS: $OS ($ARCH)"
echo "CPU: $CPU"
echo "Cores: $CORES"
echo "Compiler: $CC_VERSION"
echo "Duration: ${DURATION}s per benchmark"
echo ""

# ---------- Detect OpenMP ----------
HAS_OPENMP=false
OMP_FLAGS=""
TMP_OMP=$(mktemp /tmp/test_omp_XXXXXX.c)
cat > "$TMP_OMP" <<'EOF'
#include <omp.h>
int main() { return omp_get_max_threads(); }
EOF

if cc -fopenmp -o /dev/null "$TMP_OMP" 2>/dev/null; then
    HAS_OPENMP=true
    OMP_FLAGS="-fopenmp"
elif [[ "$OS" == "Darwin" ]] && command -v brew &>/dev/null; then
    LIBOMP_PREFIX=$(brew --prefix libomp 2>/dev/null || true)
    if [[ -n "$LIBOMP_PREFIX" ]] && [[ -d "$LIBOMP_PREFIX" ]]; then
        if cc -Xpreprocessor -fopenmp -I"$LIBOMP_PREFIX/include" -L"$LIBOMP_PREFIX/lib" -lomp -o /dev/null "$TMP_OMP" 2>/dev/null; then
            HAS_OPENMP=true
            OMP_FLAGS="-Xpreprocessor -fopenmp -I${LIBOMP_PREFIX}/include -L${LIBOMP_PREFIX}/lib -lomp"
        fi
    fi
fi
rm -f "$TMP_OMP" /tmp/test_omp_XXXXXX.c

if $HAS_OPENMP; then
    echo "OpenMP: available ($OMP_FLAGS)"
else
    echo "OpenMP: not available (using pthreads fallback)"
fi
echo ""

# ---------- Collect results ----------
RESULTS_FILE="$SCRIPT_DIR/results.md"
RESULTS=()

run_bench() {
    local label="$1"
    shift
    echo "  Running: $label"
    local output
    output=$("$@" 2>&1) || { echo "    FAILED: $output"; return; }
    echo "    $output"
    RESULTS+=("$output")
}

# ---------- Build & run C benchmark ----------
echo "--- Building C benchmark ---"
C_BENCH="$SCRIPT_DIR/bench_c"
# shellcheck disable=SC2086
cc -O3 -march=native -flto -std=c11 $OMP_FLAGS \
    -o "$C_BENCH" \
    "$SCRIPT_DIR/bench_c.c" "$ROOT_DIR/c_engine/coup_core.c" \
    -I "$ROOT_DIR/c_engine/" -lm -lpthread 2>&1 || {
    echo "FAILED to build C benchmark"
    exit 1
}
echo "  Built: $C_BENCH"
echo ""

echo "--- C engine benchmarks ---"
THREAD_COUNTS=(1 2 4)
# Add core count if > 4
if [[ "$CORES" -gt 4 ]]; then
    THREAD_COUNTS+=("$CORES")
fi

for players in 2 6; do
    for policy in random heuristic; do
        for threads in "${THREAD_COUNTS[@]}"; do
            run_bench "C p=${players} ${policy} t=${threads}" \
                "$C_BENCH" --players "$players" --policy "$policy" \
                --threads "$threads" --duration "$DURATION"
        done
    done
done
echo ""

# ---------- Build & run PufferLib 5.0 env benchmark ----------
# puffer/test_coup_env.c bench: single thread, 256 envs, agent-steps/sec.
# Printed only (different unit from the games/sec table below).
if ! $SKIP_PUFFER; then
    echo "--- PufferLib env benchmark (make bench-puffer) ---"
    (cd "$ROOT_DIR" && make -s bench-puffer 2>&1) || echo "  FAILED to build/run PufferLib env benchmark — skipping"
    echo ""
fi

# ---------- Build & run C++ benchmark ----------
if ! $SKIP_CPP; then
    echo "--- Building C++ benchmark ---"
    CPP_BUILD="$ROOT_DIR/cpp_engine/build"
    mkdir -p "$CPP_BUILD"
    if (cd "$CPP_BUILD" && cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1 && make bench_cpp 2>&1); then
        echo "  Built: $CPP_BUILD/bench_cpp"
        echo ""
        echo "--- C++ (OpenSpiel) benchmarks ---"
        for players in 2 6; do
            for policy in random heuristic; do
                for threads in "${THREAD_COUNTS[@]}"; do
                    run_bench "C++ p=${players} ${policy} t=${threads}" \
                        "$CPP_BUILD/bench_cpp" --players "$players" --policy "$policy" \
                        --threads "$threads" --duration "$DURATION"
                done
            done
        done
    else
        echo "  FAILED to build C++ benchmark — skipping"
        SKIP_CPP=true
    fi
    echo ""
fi


# ---------- Write results.md ----------
echo "--- Writing results ---"

cat > "$RESULTS_FILE" <<HEADER
# Coup Engine Benchmark Results

## Machine: ${MACHINE}
- **OS**: ${OS} (${ARCH})
- **CPU**: ${CPU}
- **Cores**: ${CORES}
- **Compiler**: ${CC_VERSION}
- **Date**: ${DATE}
- **Duration**: ${DURATION}s per benchmark
- **OpenMP**: $(if $HAS_OPENMP; then echo "yes"; else echo "no (pthreads fallback)"; fi)

## Results

| Engine | Players | Policy | Threads | Games/sec | Total Games | Avg Game Length |
|--------|---------|--------|---------|-----------|-------------|-----------------|
HEADER

for line in "${RESULTS[@]}"; do
    # Parse key=value format
    engine=$(echo "$line" | grep -o 'engine=[^ ]*' | cut -d= -f2)
    players=$(echo "$line" | grep -o 'players=[^ ]*' | cut -d= -f2)
    policy=$(echo "$line" | grep -o 'policy=[^ ]*' | cut -d= -f2)
    threads=$(echo "$line" | grep -o 'threads=[^ ]*' | cut -d= -f2)
    games=$(echo "$line" | grep -o 'games=[^ ]*' | cut -d= -f2)
    gps=$(echo "$line" | grep -o 'gps=[^ ]*' | cut -d= -f2)
    avg_len=$(echo "$line" | grep -o 'avg_len=[^ ]*' | cut -d= -f2)

    # Format engine name
    case "$engine" in
        c)         eng_display="C (raw)" ;;
        cpp)       eng_display="C++ (OpenSpiel)" ;;
        openspiel) eng_display="pyspiel" ;;
        *)         eng_display="$engine" ;;
    esac

    # Format games/sec with commas (handle decimal gps values)
    gps_int=$(echo "$gps" | cut -d. -f1)
    gps_formatted=$(printf "%'d" "$gps_int" 2>/dev/null || echo "$gps_int")

    echo "| ${eng_display} | ${players} | ${policy} | ${threads} | ${gps_formatted} | ${games} | ${avg_len} |" >> "$RESULTS_FILE"
done

cat >> "$RESULTS_FILE" <<'FOOTER'

## Methodology

- **C (raw)**: Direct `step_with_rng()` loop, no Python overhead. Compiled with `-O3 -march=native -flto`.
- **C++ (OpenSpiel)**: OpenSpiel `State` API with explicit chance node handling. More steps per game than C engine.
- **PufferLib**: not in this table. `make bench-puffer` (puffer/test_coup_env.c) reports agent-steps/sec for the 5.0 env.
- **pyspiel**: Python game loop via `pyspiel.load_game()`. Includes Python/SWIG overhead. Multi-process for threading.

### Player Policies
- **random**: Uniform random selection from valid actions.
- **heuristic**: Deterministic medium-bot strategy — honest play, never bluffs, never challenges, always blocks. Priorities: coup > assassinate > tax > steal > exchange > foreign aid > income.

### Notes
- Games/sec measures *complete games*, not individual steps.
- Average game length is in *player decisions* (chance nodes auto-resolved in C engine).
- Thread scaling uses independent game instances per thread (embarrassingly parallel).
FOOTER

echo "Results written to: $RESULTS_FILE"
echo ""
echo "=== Benchmark complete ==="
