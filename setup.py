#!/usr/bin/env python3

from os import pardir
from os.path import join
from distutils.core import setup
from distutils.extension import Extension
from Cython.Distutils import build_ext

setup(
    cmdclass = {'build_ext': build_ext},
    ext_modules = [Extension('vapoursynth', ['src/cython/vapoursynthpp.pyx'],
                             include_dirs = ['.', join(pardir, 'src', 'cython')])]
)
