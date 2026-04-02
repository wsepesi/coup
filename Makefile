.PHONY: test test-c test-text test-cpp build-puffer test-puffer test-cross clean bench bench-c bench-puffer

# C engine tests
test-c:
	cc -O2 -std=c11 -o c_engine/test_core c_engine/test_core.c c_engine/coup_core.c -I c_engine/ -lm
	./c_engine/test_core
	cc -O2 -o c_engine/test_prng c_engine/test_prng.c -I c_engine/
	./c_engine/test_prng

# Text renderer tests
test-text:
	cc -O2 -std=c11 -o c_engine/test_text_render c_engine/test_text_render.c c_engine/text_render.c c_engine/coup_core.c -I c_engine/ -lm
	./c_engine/test_text_render

# C++ engine + OpenSpiel tests via CMake
test-cpp:
	mkdir -p cpp_engine/build
	cd cpp_engine/build && cmake .. && make coup_game_test
	./cpp_engine/build/coup_game_test

# Build PufferLib C extension
build-puffer:
	uv run --with setuptools --with numpy python setup.py build_ext --inplace

# PufferLib performance test
test-puffer: build-puffer
	uv run python pufferlib/test_perf.py

# Cross-framework tests
test-cross:
	uv run python tests/test_prng_identity.py
	uv run python shared/test_constants.py
	# These require both engines built:
	# uv run python tests/test_cross_determinism.py
	# uv run python tests/test_observation_parity.py

# Run all tests
test: test-c test-text test-cross

# Profiling / benchmarks
bench-c:
	cc -O3 -march=native -flto -std=c11 -o profiling/bench_c profiling/bench_c.c c_engine/coup_core.c -I c_engine/ -lm -lpthread
	./profiling/bench_c --players 2 --policy random --threads 1 --duration 10
	./profiling/bench_c --players 6 --policy heuristic --threads 1 --duration 10

bench-puffer: build-puffer
	uv run --with setuptools --with numpy python profiling/bench_pufferlib.py --players 2 --policy random --duration 10
	uv run --with setuptools --with numpy python profiling/bench_pufferlib.py --players 6 --policy heuristic --duration 10

bench:
	./profiling/run_benchmarks.sh --duration 10

# Clean build artifacts
clean:
	rm -f c_engine/test_core c_engine/test_prng c_engine/test_text_render c_engine/*.o
	rm -rf cpp_engine/build
	rm -rf build/ *.egg-info
	find . -name "*.so" -delete
	find . -name "__pycache__" -type d -exec rm -rf {} +
