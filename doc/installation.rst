Windows Installation Instructions
=================================

Prerequisites
#############

First download and install the prerequisites:
   * `Python 3.7 <http://www.python.org/>`_  -- 32 and/or 64 bit version

Installation
############

Simply run the `VapourSynth installer <https://github.com/vapoursynth/vapoursynth/releases>`_.
It should automatically detect everything.

Test it by opening a Python command line (should be added to your start menu)
and type this::

   from vapoursynth import core
   print(core.version())

After typing the final line you should see the version printed along with a
few other lines describing the options used when instantiating the Core object.
If this for some reason fails, there may be a bug in the installer or there are
old copies of vapoursynth.pyd and vapoursynth.dll lying around.

Windows Portable Instructions
=============================

First download and decompress the prerequisites:
   * `Python 3.7 <http://www.python.org/>`_  -- 32 or 64 bit embeddable version
   
Simply decompress the `portable VapourSynth archive <https://github.com/vapoursynth/vapoursynth/releases>`_
into the Python dir and overwrite all existing files. Done.

You can also use the VapourSynth Editor by decompressing it into the same directory.

OS X Installation from Packages 
===============================

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager
   
Simply run these commands in a terminal and wait for them to complete::

   brew install vapoursynth

Linux Installation from Packages 
================================

Several distributions have packages:

   * `Ubuntu <https://launchpad.net/~djcj/+archive/ubuntu/vapoursynth>`_  -- PPA
   * `Gentoo <https://github.com/4re/vapoursynth-portage>`_  -- Portage overlay and instructions
   * `Arch Linux <https://www.archlinux.org/packages/?q=vapoursynth>`_  -- Packages

Installation via PIP (PyPI)
===========================

You can install the Python wrapper using pip.

Install `vapoursynth <https://pypi.org/project/VapourSynth/>`_ using Pip by using this command::

    pip install VapourSynth

Please note that you always need to have a working installation of VapourSynth beforehand. Note that on non-Windows systems, the installer will compile the module before installing.

Linux and OS X Compilation Instructions
=======================================

These are the requirements:
   * Autoconf, Automake, and Libtool, probably recent versions

   * pkg-config

   * GCC 4.8 or newer, or Clang

   * Nasm

   * `zimg v2.8 branch <https://github.com/sekrit-twc/zimg/releases>`_

   * Python 3

   * Cython 0.28 or later installed in your Python 3 environment

   * Sphinx for the documentation (optional)

   * iconv, libass, and ffmpeg for the Subtext plugin (optional)

   * ImageMagick 7 for the Imwri plugin (optional)

   * Tesseract 3 for the OCR plugin (optional)

Note: **any version of Python 3 will do.** A specific version is only
required when using the official Windows binaries.

Required packages (OS X)
########################

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager

Installation of the required packages is very easy. Simply run these
commands in a terminal and wait for them to complete::

   brew install python3 nasm ffmpeg libass zimg imagemagick
   pip3 install cython
   
If you've already installed all the required packages and instead want
to update them, simply run::

   brew update && brew upgrade
   pip3 install --upgrade cython
   
Required packages (Ubuntu)
##########################

In Ubuntu 16.10 the following command will install them, minus zimg,
which needs to be compiled from source::

   apt-get install build-essential nasm git libavcodec-dev libavformat-dev libavutil-dev libass-dev python3-pip python3-dev cython3 autoconf libtool libtesseract-dev
   
The packages may have different names in other distributions.

Compilation
###########

If you haven't checked out the source code before, use git to do so::

   git clone https://github.com/vapoursynth/vapoursynth.git
   
Or if you already have a copy of the source, update it with::

   git pull

Enter the VapourSynth directory and run these commands to compile and install::
   
   ./autogen.sh
   ./configure
   make
   make install
   
Depending on your operating system's configuration, VapourSynth may not
work out of the box with the default prefix of /usr/local. Two errors
may pop up when running ``vspipe --version``:

* "vspipe: error while loading shared libraries: libvapoursynth-script.so.0:
  cannot open shared object file: No such file or directory"

  This is caused by the non-standard location of libvapoursynth-script.so.0.
  Your dynamic loader is not configured to look in /usr/local/lib. One
  way to work around this error is to use the LD_LIBRARY_PATH environment
  variable::

     $ LD_LIBRARY_PATH=/usr/local/lib vspipe --version

* "Failed to initialize VapourSynth environment"

  This is caused by the non-standard location of the Python module,
  vapoursynth.so. Your Python is not configured to look in
  /usr/local/lib/python3.x/site-packages. One way to work around this
  error is to use the PYTHONPATH environment variable::

     $ PYTHONPATH=/usr/local/lib/python3.x/site-packages vspipe --version

  Replace "x" with the correct number.


The documentation can be built using its own Makefile::

   $ make -C doc/ html

The documentation can be installed using the standard program ``cp``.
