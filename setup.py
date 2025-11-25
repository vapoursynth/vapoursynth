#!/usr/bin/env python

from os import curdir
from os.path import dirname, exists, join
from pathlib import Path
from platform import architecture
from shutil import which
from sys import version_info

from setuptools import Extension, setup

is_win = (architecture()[1] == "WindowsPE")
is_mac = (architecture()[1] == "Mach-O")

limited_api_build = (version_info.minor >= 12)

extra_data = {}

if (limited_api_build):
    extra_data["options"] = {'bdist_wheel': {'py_limited_api' : 'cp312'}}

library_dirs = [curdir, "build"]

self_path = Path(__file__).resolve()
CURRENT_RELEASE = self_path.parent.joinpath('VAPOURSYNTH_VERSION').read_text('utf8').split(' ')[-1].strip().split('-')[0]

if is_win:
    library_dirs.append("C:\\vapoursynth\\msvc_project\\x64\\Release")

    # Locate the vapoursynth dll inside the library directories first
    # should we find it, it is a clear indicator that VapourSynth
    # has been compiled by the user.
    for path in library_dirs:
        dll_path = join(path, "vapoursynth.dll")
        if exists(dll_path):
            break

    # Make sure the setup process copies the VapourSynth.dll into the site-package folder
    if not dll_path:
        raise OSError("Couldn't detect vapoursynth.dll source path")
    print("Found VapourSynth.dll at:", dll_path)

    extra_data["data_files"] = [(r"Lib\site-packages", [dll_path])]
elif is_mac:
    library_dirs.append("/opt/homebrew/lib")


setup(
    name="VapourSynth",
    description="A frameserver for the 21st century",
    url="https://www.vapoursynth.com/",
    download_url="https://github.com/vapoursynth/vapoursynth",
    author="Fredrik Mellbin",
    author_email="fredrik.mellbin@gmail.com",
    license="LGPL 2.1 or later",
    version=CURRENT_RELEASE,
    long_description="A modern replacement for Avisynth",
    platforms="All",
    ext_modules=[
        Extension(
            "vapoursynth", [join("src", "cython", "vapoursynth.pyx")],
            define_macros=[("Py_LIMITED_API" if limited_api_build else "VS_UNUSED_CYTHON_BUILD_MACRO", 0x030C0000), ("VS_USE_LATEST_API", None), ("VS_GRAPH_API", None), ("VS_CURRENT_RELEASE", CURRENT_RELEASE)],
            py_limited_api=limited_api_build,
            libraries=["vapoursynth"],
            library_dirs=library_dirs,
            include_dirs=[
                curdir,
                join("src", "cython"),
                join("src", "vsscript")
            ]
        )
    ],
    exclude_package_data={"": ("VAPOURSYNTH_VERSION",)},
    **extra_data
)
