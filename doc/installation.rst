Installation
============

General Installation
####################

The recommended way to install VapourSynth is through pip. There are currently binary wheels available for Windows, Linux, and OSX.

1. Install Python 3.12 or later
2. Run ``pip install vapoursynth``
3. Run ``vaporsynth-config``
4. (Windows only) Update the Visual Studio 2015-2026 Redistributable if told to

Installation is now done.

After you completed the second step, you can test it by opening a Python command line
and type this::

   from vapoursynth import core
   print(str(core))

After pressing return at the final line, you should see the version printed along with a
few other lines describing the options used when instantiating the Core object.
In fact, these lines should be the same as the output result of ``vspipe --version``.

Windows Installer
******************

Prerequisites
-------------

First download and install the prerequisites:
   * `Python 64 bit version <http://www.python.org/>`_ -- There is support for Python 3.12 and all later versions, including 3.13 and 3.14.

Installation
------------

Simply run the `VapourSynth installer <https://github.com/vapoursynth/vapoursynth/releases>`_.

It will perform the general installation steps and offer a few additional option. 

Windows Portable
****************

Download and run the automatic script called *Install-Portable-VapourSynth-RXX.ps1*.
It will then automatically download and set up embedded Python, pip and VapourSynth in a subdirectorey called *vapoursynth-portable* by default.
It's possible to pass arguments to it to specify the installed Python version in addition to an option to run it in unattended mode.

OS X Installation
*****************

Homebrew has VapourSynth packages maintained by other people that are generally kept up to date.

REPORT ALL PACKAGING ISSUES TO THE RESPECTIVE MAINTAINERS AND NOT ON THE VAPOURSYNTH BUG TRACKER!

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager

Simply run these commands in a terminal and wait for them to complete::

   brew install vapoursynth

Linux installation
******************

Several Linux distributions have VapourSynth packages maintained by other people. The ones listed here are generally kept up to date with fairly recent versions.

REPORT ALL PACKAGING ISSUES TO THE RESPECTIVE MAINTAINERS AND NOT ON THE VAPOURSYNTH BUG TRACKER!

Debian
------
The VapourSynth packages are provided by `deb-multimedia repository <https://www.deb-multimedia.org/>`_.
You need to add the repository first following the guide on the official website.

Fedora, CentOS and RHEL
-----------------------
For Fedora, the VapourSynth packages can be downloaded from official repository directly.
For CentOS and RHEL, you should install EPEL (Extra Packages for Enterprise Linux) repository first.

Gentoo
------
There is an `unofficial Portage tree <https://github.com/4re/vapoursynth-portage>`_ with all VapourSynth related ebuilds.
Check the Github link for more information and instructions.

Arch Linux
----------
`VapourSynth-related packages <https://www.archlinux.org/packages/?q=vapoursynth>`_ are provided by the Community repository.

Nix and NixOS
-------------
``vapoursynth`` is available on nixpkgs, either via ``nixpkgs#vapoursynth`` or via ``nixpkgs#python3Packages.vapoursynth`` (currently on unstable only).
Be aware that the derivation is broken on MacOS.

VapourSynth releases are not backported to the current stable branch.
To get the newest version use the unstable branch.

Windows Compilation
*******************

Preparing the Build Environment on Windows
------------------------------------------

Default install paths are assumed in all projects and scripts, be prepared to adjust many things if you changed them

Required languages and applications:

* Needs `Visual Studio 2026 <https://visualstudio.microsoft.com/vs/>`_
* It also needs `64bit <https://www.python.org/>`_ Python 3.14.x (the msvc project assumes that you installed python for all users.)
* `InnoSetup <http://www.jrsoftware.org/isdl.php>`_ is needed to create the installer (default installation path assumed)
* `7-zip <https://www.7-zip.org/>`_ is needed to compress the portable version (default installation path assumed)

Preparing things
----------------

* Clone VapourSynth
* Run ``install_deps.bat``

This will put everything in the correct location assuming you have VapourSynth installed. Or alternatively you can do every step manually:

* Clone VapourSynth repository
* Clone VSRepo into the VapourSynth dir (``git clone https://github.com/vapoursynth/vsrepo``)
* Clone zimg into the VapourSynth dir (``git clone https://github.com/sekrit-twc/zimg --recurse-submodules``)
* Clone libp2p into the VapourSynth dir (``git clone https://github.com/sekrit-twc/libp2p``)
* Place 7z.exe and 7z.dll from `7-zip <https://www.7-zip.org/>`_ into the ``installer`` dir

Compilation
-----------

* Run ``compile_all.bat`` for 64bit.

Linux and OS X Compilation
**************************

These are the requirements:

   * Meson 1.3.0 or later
    
   * ninja-build

   * pkg-config

   * GCC or Clang, with a version supporting C++17

   * `zimg <https://github.com/sekrit-twc/zimg>`_

   * Python 3.12 or later (may work on earlier versions but these are never fully tested)

   * Cython 3.1.x or later installed in your Python 3 environment

   * Sphinx for the documentation (optional)

Note: **any version of Python 3 will do.** A specific version is only
required when using the official Windows binaries.

Required packages (OS X)
------------------------

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager

Installation of the required packages is very easy. Simply run these
commands in a terminal and wait for them to complete::

   brew install python3 zimg
   pip3 install cython

If you've already installed all the required packages and instead want
to update them, simply run::

   brew update && brew upgrade
   pip3 install --upgrade cython

Compilation
-----------

If you haven't checked out the source code before, use git to do so::

   git clone https://github.com/vapoursynth/vapoursynth.git

Or if you already have a copy of the source, update it with::

   git pull

Enter the VapourSynth directory and run these commands to compile and install::

   meson setup build
   ninja -C build
   ninja -C build install

Depending on your operating system's configuration, VapourSynth may not
work out of the box with the default prefix of /usr/local. Two errors
may pop up when running ``vspipe --version``:

* "vspipe: error while loading shared libraries: libvapoursynth-script.so.0:
  cannot open shared object file: No such file or directory"

  This is caused by the non-standard location of libvapoursynth-script.so.0.
  Your dynamic loader is not configured to look in /usr/local/lib. One
  way to work around this error is to use the LD_LIBRARY_PATH environment
  variable (or DYLD_LIBRARY_PATH on macOS)::

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

Plugins and Scripts
###################

If you're looking for plugins and scripts then one of the most complete lists
available can be found at `vsdb.top <http://vsdb.top/>`_.

Installing with VSRepo
**********************

On windows you can use the included vsrepo.py to install and upgrade plugins and scripts.

Simply run ``vsrepo.py install <namespace or identifier>`` to install them.

If you need a list of known plugins and scripts you can run ``vsrepo.py available`` or visit `vsdb.top <http://vsdb.top/>`_.

For more reference, visit `vsrepo's repository <https://github.com/vapoursynth/vsrepo>`_

Installing Manually
*******************

You can put your plugin (``.dll``) and script (``.py``) to where you think it is convenient.

For plugins, you can use ``std.LoadPlugin`` function to load it. there is also a plugin autoloading mechanism to save your time, see blow.

For scripts, you should add a relative path to ``python<your_python_version>._pth``, then you can import it in your script.

Plugin Autoloading
******************

VapourSynth automatically recursively loads all the native plugins located in ``<site-packages>/vapoursynth/plugins``. Autoloading works just like manual loading, with the exception
that any errors encountered while loading a plugin are silently ignored.

An additional plugin path can be loaded by setting the VAPOURSYNTH_EXTRA_PLUGIN_PATH environment variable. It is loaded after the normal plugin path.