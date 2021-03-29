#!/usr/bin/env python3

from setuptools import setup
from distutils.core import Extension
from glob import glob

SOURCES = glob('m3/*.c') + ['m3module.c']

setup(
    name         = "pywasm3",
    version      = "0.4.9",
    description  = "The fastest WebAssembly interpreter",
    platforms    = "any",
    url          = "https://github.com/wasm3/wasm3",
    license      = "MIT",
    author       = "Volodymyr Shymanskyy",
    author_email = "vshymanskyi@gmail.com",
    
    long_description                = open("README.md").read(),
    long_description_content_type   = "text/markdown",

    ext_modules=[
        Extension('wasm3', sources=SOURCES, include_dirs=['m3'],
        extra_compile_args=['-g0', '-O3', '-march=native',
                            '-fomit-frame-pointer', '-fno-stack-check', '-fno-stack-protector',
                            '-DDEBUG', '-DNASSERTS'])
    ],

    classifiers  = [
        "Topic :: Software Development :: Libraries :: Python Modules",
        "Development Status :: 4 - Beta",
        "License :: OSI Approved :: MIT License",
        "Operating System :: POSIX :: Linux",
        "Operating System :: Microsoft :: Windows",
        "Operating System :: MacOS :: MacOS X"
    ]
)
