from setuptools import setup
from distutils.core import Extension
from glob import glob

SOURCES = glob('source/*.c') + ['platforms/python/m3module.c']

setup(
    name='wasm3',
    version='0.0.1',
    ext_modules=[
        Extension('m3', sources=SOURCES, include_dirs=['source'],
        extra_compile_args=['-g', '-O0'])]
)