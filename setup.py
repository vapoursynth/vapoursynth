#!/usr/bin/env python

from os import curdir, pardir
from os.path import join
from distutils.core import setup
from Cython.Distutils import Extension, build_ext

setup(
    name = "VapourSynth",
    description = "A frameserver for the 21st century",
    url = "http://www.vapoursynth.com/",
    download_url = "https://github.com/vapoursynth/vapoursynth",
    author = "Fredrik Mellbin",
    author_email = "fredrik.mellbin@gmail.com",
    license = "LGPL 2.1 or later",
    version = "1.0.0",
    long_description = "A portable replacement for Avisynth",
    platforms = "All",
    cmdclass = {'build_ext': build_ext},
    ext_modules = [Extension("vapoursynth", [join("src", "cython", "vapoursynth.pyx")],
                             libraries = ["vapoursynth"],
                             library_dirs = [curdir, "build"],
                             include_dirs = [curdir, join("src", "cython")],
                             cython_c_in_temp = 1)]
)
