Installation Instructions
=========================

The installation contains two main steps:

1. Install VapourSynth core library.
2. Install the Python wrapper of VapourSynth.

After you completed the second step, you can test it by opening a Python command line
and type this::

   from vapoursynth import core
   print(core.version())

After pressing return at the final line, you should see the version printed along with a
few other lines describing the options used when instantiating the Core object.
In fact, these lines should be the same as the output result of ``vspipe --version``.

Windows Installation Instructions
#################################

Prerequisites
*************

First download and install the prerequisites:
   * `Python 3.9.x <http://www.python.org/>`_  -- 32 or 64 bit version depending on which version of VapourSynth you want to install
   
Note that VapourSynth and Python have to be matched so both are either installed
for all users or for only for the current user.

Also note that per user installs will not install the required Visual Studio
2019 runtimes.

Installation
************

Simply run the `VapourSynth installer <https://github.com/vapoursynth/vapoursynth/releases>`_.
It should automatically detect and install everything, including the Python wrapper.

If the tests mentioned at the beginning fails, there may be a bug in the installer or there are
old copies of vapoursynth.pyd and vapoursynth.dll lying around.

Windows Portable Instructions
#############################

First download and decompress the prerequisites:
   * `Python 3.9.x <http://www.python.org/>`_  -- 32 or 64 bit embeddable version
   
Simply decompress the `portable VapourSynth archive <https://github.com/vapoursynth/vapoursynth/releases>`_
into the Python dir and overwrite all existing files. Done.

You can also use the VapourSynth Editor by decompressing it into the same directory.

OS X Installation from Packages
###############################

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager
   
Simply run these commands in a terminal and wait for them to complete::

   brew install vapoursynth

Linux Installation from Packages
################################

Several distributions have VapourSynth packages. Note that those packages are usually OUT OF DATE.
 
Debian
******
The VapourSynth packages are provided by `deb-multimedia repository <https://www.deb-multimedia.org/>`_.
You need to add the repository first following the guide on the official website.

Fedora, CentOS and RHEL
***********************
For Fedora, the VapourSynth packages can be downloaded from official repository directly.
For CentOS and RHEL, you should install EPEL (Extra Packages for Enterprise Linux) repository first.

Gentoo
******
There is an `unofficial Portage tree <https://github.com/4re/vapoursynth-portage>`_ with all VapourSynth related ebuilds.
Check the Github link for more information and instructions.

Arch Linux
**********
`VapourSynth-related packages <https://www.archlinux.org/packages/?q=vapoursynth>`_ are provided by the Community repository.

Linux and OS X Compilation Instructions
#######################################

These are the requirements:
   * Autoconf, Automake, and Libtool, probably recent versions

   * pkg-config

   * GCC 4.8 or newer, or Clang

   * `zimg v3.0 branch <https://github.com/sekrit-twc/zimg/releases>`_

   * Python 3

   * Cython 0.28 or later installed in your Python 3 environment

   * Sphinx for the documentation (optional)

   * iconv, libass, and ffmpeg for the Subtext plugin (optional)

   * ImageMagick 7 for the Imwri plugin (optional)

   * Tesseract 3 for the OCR plugin (optional)

Note: **any version of Python 3 will do.** A specific version is only
required when using the official Windows binaries.

Required packages (OS X)
************************

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager

Installation of the required packages is very easy. Simply run these
commands in a terminal and wait for them to complete::

   brew install python3 ffmpeg libass zimg imagemagick
   pip3 install cython
   
If you've already installed all the required packages and instead want
to update them, simply run::

   brew update && brew upgrade
   pip3 install --upgrade cython

Compilation
***********

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

Install Python wrapper via pip (PyPI)
#####################################

The Windows installer will install Python wrapper automatically.
Some Linux distros (e.g. Fedora & CentOS series) also provide pre-built Python wrapper package.
If you do not use them, you can install the Python wrapper using pip.

Install `vapoursynth <https://pypi.org/project/VapourSynth/>`_ by using this command::

    pip install VapourSynth

Please note that you need a working installation of VapourSynth beforehand.
On non-Windows systems, the installer will compile the module before installing.

