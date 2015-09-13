#!/usr/bin/env python

from platform import architecture
from os import curdir, pardir
from os.path import join
from distutils.core import setup
from Cython.Distutils import Extension, build_ext

is_win = (architecture()[1] == "WindowsPE")
is_64 = (architecture()[0] == "64bit")

library_dirs = [curdir, "build"]
if is_win:
    if is_64:
        library_dirs.append(join("msvc_project", "x64", "Release"))
    else:
        library_dirs.append(join("msvc_project", "Release"))

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
                             library_dirs = library_dirs,
                             include_dirs = [curdir, join("src", "cython")],
                             cython_c_in_temp = 1)]
)
