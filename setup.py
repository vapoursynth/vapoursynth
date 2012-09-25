#!/usr/bin/env python3

from os import pardir
from os.path import join
from distutils.core import setup
from distutils.extension import Extension
from Cython.Distutils import build_ext

setup(
    name = "VapourSynth",
    description = "A frameserver for the 21st century",
    url = "http://www.vapoursynth.com/",
    download_url = "http://code.google.com/p/vapoursynth/",
    author = "Fredrik Mellbin",
    author_email = "fredrik.mellbin@gmail.com",
    license = "LGPL 2.1 or later",
    version = "1.0.0",
    long_description = "A portable replacement for Avisynth",
    platforms = "All",
    cmdclass = {'build_ext': build_ext},
    ext_modules = [Extension('vapoursynth', ['src/cython/vapoursynthpp.pyx'],
                             libraries=["vapoursynth"],
                             library_dirs = ["build"],
                             include_dirs = ['.', join(pardir, 'src', 'cython')])]
)
