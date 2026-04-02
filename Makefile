.PHONY: test test-c test-text test-cpp build-puffer test-puffer test-cross clean

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
	uv run python setup.py build_ext --inplace

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

# Clean build artifacts
clean:
	rm -f c_engine/test_core c_engine/test_prng c_engine/test_text_render c_engine/*.o
	rm -rf cpp_engine/build
	rm -rf build/ *.egg-info
	find . -name "*.so" -delete
	find . -name "__pycache__" -type d -exec rm -rf {} +
