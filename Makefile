.PHONY: test test-c test-text test-san test-cpp pyspiel test-py puffer-install puffer-cpu test-puffer bench-puffer mccfr-example clean bench bench-c

# Flags for the C test targets (override for sanitizer runs, see test-san)
CTESTFLAGS ?= -O2 -std=c11 -Wall -Wextra

# C engine tests
test-c:
	cc $(CTESTFLAGS) -o c_engine/test_core c_engine/test_core.c c_engine/coup_core.c -I c_engine/ -lm
	./c_engine/test_core
	cc $(CTESTFLAGS) -o c_engine/test_prng c_engine/test_prng.c -I c_engine/
	./c_engine/test_prng

# Text renderer tests
test-text:
	cc $(CTESTFLAGS) -o c_engine/test_text_render c_engine/test_text_render.c c_engine/text_render.c c_engine/coup_core.c -I c_engine/ -lm
	./c_engine/test_text_render

# C tests under AddressSanitizer + UndefinedBehaviorSanitizer
test-san:
	$(MAKE) test-c test-text CTESTFLAGS="-O1 -g -std=c11 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=all"

# OpenSpiel adapter (cpp_engine/, wraps the C engine) via CMake.
# Builds OpenSpiel core + basic_tests from the lib/OpenSpiel submodule.
test-cpp:
	mkdir -p cpp_engine/build
	cd cpp_engine/build && cmake .. && cmake --build . --target coup_game_test -j
	./cpp_engine/build/coup_game_test

# Outcome-sampling MCCFR demo on 2-player Coup (OpenSpiel C++)
mccfr-example:
	mkdir -p cpp_engine/build
	cd cpp_engine/build && cmake .. && cmake --build . --target mccfr_example -j
	./cpp_engine/build/mccfr_example 20000

# OpenSpiel's real `pyspiel` Python extension with coup compiled in, built
# from the lib/OpenSpiel submodule against the uv venv's Python, in its own
# build dir (does not touch cpp_engine/build). Writes a .pth into .venv so
# `uv run python -c "import pyspiel"` and `import open_spiel.python...` work.
PYSPIEL_BUILD := cpp_engine/build-py
VENV_PY := .venv/bin/python
# uv extras for test-py (DGX: PYEXTRAS="--extra torch --extra jax --extra misc --extra jax-cuda")
PYEXTRAS ?= --extra torch --extra jax --extra misc
pyspiel:
	test -x $(VENV_PY) || uv sync
	cmake -S cpp_engine -B $(PYSPIEL_BUILD) -DCOUP_BUILD_PYSPIEL=ON \
		-DPython3_EXECUTABLE=$(CURDIR)/$(VENV_PY)
	cmake --build $(PYSPIEL_BUILD) --target pyspiel -j
	site=$$($(VENV_PY) -c 'import sysconfig; print(sysconfig.get_paths()["purelib"])') && \
		printf '%s\n%s\n' "$(CURDIR)/lib/OpenSpiel" "$(CURDIR)/$(PYSPIEL_BUILD)/python" \
		> "$$site/coup_openspiel.pth" && echo "wrote $$site/coup_openspiel.pth"
	uv run --no-sync python -c "import pyspiel; g = pyspiel.load_game('coup'); print('pyspiel OK:', g)"

# Python smoke tests: pyspiel + OpenSpiel C++/PyTorch/JAX algorithms on coup.
test-py: pyspiel
	uv run $(PYEXTRAS) pytest -q training/tests

# PufferLib 5.0 env (puffer/coup.h). Links the env into the lib/PufferLib
# submodule; PufferLib's build.sh needs bash >= 4 (macOS: brew install bash libomp).
PUFFER_DIR := lib/PufferLib
PUFFER_BASH := $(shell command -v /opt/homebrew/bin/bash || command -v bash)
ifeq ($(shell uname -s),Darwin)
RAYLIB_DIR := $(PUFFER_DIR)/raylib-5.5_macos
RAYLIB_LDFLAGS := -framework Cocoa -framework IOKit -framework CoreVideo -framework OpenGL
else
RAYLIB_DIR := $(PUFFER_DIR)/raylib-5.5_linux_amd64
RAYLIB_LDFLAGS := -lGL -lpthread -ldl
endif
PUFFER_TEST_FLAGS := -std=gnu11 -Wall -Wno-unused-function -I puffer -I c_engine \
	-I $(PUFFER_DIR)/src -I $(RAYLIB_DIR)/include \
	puffer/test_coup_env.c $(RAYLIB_DIR)/lib/libraylib.a $(RAYLIB_LDFLAGS) -lm

puffer-install:
	./puffer/install.sh

# Standalone eval binary (lib/PufferLib/coup); also downloads raylib.
puffer-cpu: puffer-install
	cd $(PUFFER_DIR) && $(PUFFER_BASH) ./build.sh coup --cpu

$(RAYLIB_DIR)/lib/libraylib.a:
	$(MAKE) puffer-cpu

# Env invariant tests (fake Agent buffers, random masked play)
test-puffer: puffer-install $(RAYLIB_DIR)/lib/libraylib.a
	cc -O1 -g -fsanitize=address,undefined -o puffer/test_coup_env $(PUFFER_TEST_FLAGS)
	./puffer/test_coup_env

# Single-thread env throughput (agent-steps/sec)
bench-puffer: puffer-install $(RAYLIB_DIR)/lib/libraylib.a
	cc -O2 -o puffer/test_coup_env_bench $(PUFFER_TEST_FLAGS)
	./puffer/test_coup_env_bench bench 3

# Run all tests (test-cpp is separate: it builds OpenSpiel from source)
test: test-c test-text

# Profiling / benchmarks
bench-c:
	cc -O3 -march=native -flto -std=c11 -o profiling/bench_c profiling/bench_c.c c_engine/coup_core.c -I c_engine/ -lm -lpthread
	./profiling/bench_c --players 2 --policy random --threads 1 --duration 10
	./profiling/bench_c --players 6 --policy heuristic --threads 1 --duration 10
	./profiling/bench_c --players 6 --policy random --threads 1 --duration 10 --mode env

bench:
	./profiling/run_benchmarks.sh --duration 10

# Clean build artifacts
clean:
	rm -f c_engine/test_core c_engine/test_prng c_engine/test_text_render c_engine/*.o profiling/bench_c
	rm -f puffer/test_coup_env puffer/test_coup_env_bench
	rm -rf cpp_engine/build cpp_engine/build-py
	find . -name "__pycache__" -type d -exec rm -rf {} +
