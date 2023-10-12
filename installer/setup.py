# Always prefer setuptools over distutils
from setuptools import setup
from platform import architecture

import os
from os import path
from pathlib import Path

self_path = Path(__file__).resolve()
CURRENT_RELEASE = next(path for path in (self_path.with_name('VAPOURSYNTH_VERSION'), *(folder / 'VAPOURSYNTH_VERSION' for folder in self_path.parents)) if path.exists()).read_text('utf8').split(' ')[-1].strip().split('-')[0]

try:
    from wheel.bdist_wheel import bdist_wheel as _bdist_wheel
    class bdist_wheel(_bdist_wheel):
        def finalize_options(self):
            _bdist_wheel.finalize_options(self)
            self.root_is_pure = False

        def get_tag(self):
            python, abi, plat = _bdist_wheel.get_tag(self)
            # We don't contain any python source
            python, abi = 'py2.py3', 'none'
            return python, abi, plat
except ImportError:
    bdist_wheel = None


is_win = (architecture()[1] == "WindowsPE")
is_64 = (architecture()[0] == "64bit")
here = path.abspath(path.dirname(__file__))

if not is_win:
    raise OSError("VapourSynth Portable is currently only supported on Windows Systems.")

if not os.path.exists(os.path.join(here, "VapourSynth.dll")):
    if is_64:
        subdir = "buildp64"
    else:
        subdir = "buildp32"
    build_dir = os.path.join(here, subdir)

    if not os.path.exists(os.path.join(build_dir, "VapourSynth.dll")):
        raise OSError("Failed to detect VapourSynth-portable build directory.")
else:
    subdir = "."
    build_dir = here

if is_64:
    plugin_subdir = 'vapoursynth64'
else:
    plugin_subdir = 'vapoursynth32'
plugin_dir = os.path.join(subdir, plugin_subdir)

setup(
    name = "VapourSynth-portable",
    version=CURRENT_RELEASE,
    description = "A frameserver for the 21st century",
    url = "http://www.vapoursynth.com/",
    download_url = "https://github.com/vapoursynth/vapoursynth",
    author = "Fredrik Mellbin",
    author_email = "fredrik.mellbin@gmail.com",

    cmdclass={'bdist_wheel': bdist_wheel},

    packages=[],
    install_requires=["vapoursynth==" + CURRENT_RELEASE],
    data_files = [
        ("Lib\\site-packages", [
            os.path.join(build_dir, p)
            for p in os.listdir(build_dir)
            if p.endswith("140.dll") or \
               p.endswith(".vs")
        ]),

        ("Scripts", [
            os.path.join(build_dir, "VSPipe.exe"),
            os.path.join(build_dir, "VSScript.dll"),
            os.path.join(build_dir, "portable.vs")
        ]),

        ("Lib\\site-packages\\%s\\coreplugins"%plugin_subdir, [
            os.path.join(plugin_dir, 'coreplugins', p)
            for p in os.listdir(os.path.join(plugin_dir, 'coreplugins'))
            if p.endswith(".dll")
        ]),

        ("Lib\\site-packages\\%s\\plugins"%plugin_subdir, [
            os.path.join(plugin_dir, 'plugins', p)
            for p in os.listdir(os.path.join(plugin_dir, 'plugins'))
            if p.endswith(".dll") or p.endswith(".keep")
        ])
    ]
)
