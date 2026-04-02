#!/usr/bin/env python3
import subprocess
import os
import tempfile

DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(DIR)

def compile_and_run(src, compiler, flags, include_dirs=None):
    with tempfile.NamedTemporaryFile(suffix='', delete=False) as f:
        binary = f.name
    cmd = [compiler] + flags + ['-o', binary, src]
    if include_dirs:
        for d in include_dirs:
            cmd.extend(['-I', d])
    r = subprocess.run(cmd, capture_output=True, text=True)
    assert r.returncode == 0, f"Compilation failed: {r.stderr}"
    r = subprocess.run([binary], capture_output=True, text=True)
    os.unlink(binary)
    return r.stdout

def test_prng_identity():
    c_out = compile_and_run(
        os.path.join(DIR, 'prng_c_print.c'),
        'cc', ['-std=c11', '-O2'],
        [os.path.join(ROOT, 'c_engine')])

    cpp_out = compile_and_run(
        os.path.join(DIR, 'prng_cpp_print.cpp'),
        'c++', ['-std=c++17', '-O2'], [])

    c_lines = c_out.strip().split('\n')
    cpp_lines = cpp_out.strip().split('\n')

    assert len(c_lines) == 1000, f"Expected 1000 C values, got {len(c_lines)}"
    assert len(cpp_lines) == 1000, f"Expected 1000 C++ values, got {len(cpp_lines)}"

    for i, (c, cpp) in enumerate(zip(c_lines, cpp_lines)):
        assert c == cpp, f"Mismatch at index {i}: C={c} C++={cpp}"

    print(f"PRNG identity test PASSED: {len(c_lines)} values bit-identical")

if __name__ == '__main__':
    test_prng_identity()
