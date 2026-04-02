"""
setup.py — Build the Coup PufferLib C extension.

Build with:
    pip install -e .
or:
    python setup.py build_ext --inplace
"""

from setuptools import setup, Extension
import numpy as np

coup_ext = Extension(
    "coup_binding",
    sources=[
        "pufferlib/binding.c",
        "c_engine/coup_core.c",
    ],
    include_dirs=[
        "c_engine/",
        np.get_include(),
    ],
    extra_compile_args=["-std=c11", "-O2", "-Wall"],
)

setup(
    name="coup",
    version="0.1.0",
    ext_modules=[coup_ext],
    packages=["pufferlib"],
    package_dir={"pufferlib": "pufferlib"},
)
