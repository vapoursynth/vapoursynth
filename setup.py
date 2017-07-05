#!/usr/bin/env python

from platform import architecture
from os import curdir
from os.path import join
from setuptools import setup, Extension

is_win = (architecture()[1] == "WindowsPE")
is_64 = (architecture()[0] == "64bit")

library_dirs = [curdir, "build"]
if is_win:
    if is_64:
        library_dirs.append(join("msvc_project", "x64", "Release"))
        lib_suffix = "lib64"
    else:
        library_dirs.append(join("msvc_project", "Release"))
        lib_suffix = "lib32"

    #
    # This code detects the library directory by querying the Windows Registry
    # for the current VapourSynth directory location.
    #

    import winreg
    REGISTRY_PATH = r"SOFTWARE\VapourSynth"
    REGISTRY_KEY = r"Path"

    def query(hkey, path, key):
        reg_key = None
        try:
            reg_key = winreg.OpenKey(hkey, path, 0, winreg.KEY_READ)
            value, _ = winreg.QueryValueEx(reg_key, key)
        finally:
            if reg_key is not None:
                winreg.CloseKey(reg_key)

        return value
    
    try:
        base_dir = query(winreg.HKEY_LOCAL_MACHINE, REGISTRY_PATH, REGISTRY_KEY)
    except (IOError, FileNotFoundError):
        pass
    else:
        library_dirs.append(base_dir)
        library_dirs.append(join(base_dir, "sdk", lib_suffix))


setup(
    name = "VapourSynth",
    description = "A frameserver for the 21st century",
    url = "http://www.vapoursynth.com/",
    download_url = "https://github.com/vapoursynth/vapoursynth",
    author = "Fredrik Mellbin",
    author_email = "fredrik.mellbin@gmail.com",
    license = "LGPL 2.1 or later",
    version = "39",
    long_description = "A portable replacement for Avisynth",
    platforms = "All",
    ext_modules = [Extension("vapoursynth", [join("src", "cython", "vapoursynth.pyx")],
                             libraries = ["vapoursynth"],
                             library_dirs = library_dirs,
                             include_dirs = [curdir, join("src", "cython")],
                             cython_c_in_temp = 1)],
    install_requires=[
        'setuptools>=36.0',
        "Cython",
    ]
)
