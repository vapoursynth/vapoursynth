Installing and Compiling
========================

General Installation
####################

The recommended way to install VapourSynth is through pip. There are currently binary wheels available for Windows, Linux, and OSX.

1. Install Python 3.12 or later
2. Run ``pip install vapoursynth``
3. Run ``vapoursynth config``
4. (Windows only) Update the Visual Studio 2015-2026 Redistributable if told to

Optional on Windows:

5. Run ``vapoursynth register-install`` to set the VSSCRIPT_PATH environment variable to allow other applications to find the library
6. Run ``vapoursynth register-legacy-install`` to write installation information to the registry so applications not aware of R74 and later still work
7. Run ``vapoursynth register-vfw`` to register the VFW module

Note that you can easily switch multiple installs in virtual environments by using these commands.

Optional on MacOS, Linux and similar:

5. Set the VSSCRIPT_PATH environment variable to allow other applications to find the library, you can get the path by running ``vapoursynth get-vsscript``

Installation is now done. If you want to use VSRepo you can simply install it with ``pip install vsrepo``

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

It will perform the general installation steps and offer a few additional options.

Windows Portable
****************

Download and run the automatic script called *Install-Portable-VapourSynth-RXX.ps1*.
It will then automatically download and set up embedded Python, pip, VapourSynth and VSRepo in a subdirectorey called *vapoursynth-portable* by default.
It's possible to pass arguments to it to specify the installed Python version in addition to an option to run it in unattended mode.

Note that Python by design hardcodes its current path in all exe files in the *Scripts* directory which technically makes the install not portable at all. To counteract
this the portable install script deletes all of these and instead provides a set of bat files (vspipe.bat, vsrepo.bat, pip.bat) in the root of the portable install
to offset this inconvenience. You can also still call just about all python modules using ``python -m <module> <arguments>`` instead.

Unofficial Packages
###################

REPORT ALL PACKAGING ISSUES TO THE RESPECTIVE MAINTAINERS AND NOT ON THE VAPOURSYNTH BUG TRACKER!

Several package managers have VapourSynth packages maintained by other people. The ones listed here are generally kept up to date with fairly recent versions.

OSX
***

Homebrew has VapourSynth packages maintained by other people that are generally kept up to date.

REPORT ALL PACKAGING ISSUES TO THE RESPECTIVE MAINTAINERS AND NOT ON THE VAPOURSYNTH BUG TRACKER!

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager

Simply run these commands in a terminal and wait for them to complete::

   brew install vapoursynth

Debian
******

VapourSynth packages are provided by the `deb-multimedia repository <https://www.deb-multimedia.org/>`_.
You need to add the repository first following the guide on the official website.

Fedora, CentOS and RHEL
***********************

For Fedora, the VapourSynth packages can be downloaded from official repository directly.
For CentOS and RHEL, you should install the EPEL (Extra Packages for Enterprise Linux) repository first.

Gentoo
******

There is an `unofficial Portage tree <https://github.com/4re/vapoursynth-portage>`_ with all VapourSynth related ebuilds.
Check the Github link for more information and instructions.

Arch Linux
**********

`VapourSynth-related packages <https://www.archlinux.org/packages/?q=vapoursynth>`_ are provided by the Community repository.

Nix and NixOS
*************
``vapoursynth`` is available on nixpkgs, either via ``nixpkgs#vapoursynth`` or via ``nixpkgs#python3Packages.vapoursynth`` (currently on unstable only).
Be aware that the derivation is broken on MacOS.

VapourSynth releases are not backported to the current stable branch.
To get the newest version use the unstable branch.

Compiling it Yourself
#####################

Windows Compilation
*******************

Preparing the Build Environment on Windows
------------------------------------------

Default install paths are assumed in all projects and scripts, be prepared to adjust many things if you changed them

Required applications:

* Needs `Visual Studio 2026 <https://visualstudio.microsoft.com/vs/>`_
* It also needs `64bit <https://www.python.org/>`_ Python 3.14.x (the msvc project assumes that you installed python for all users.)
* `InnoSetup <http://www.jrsoftware.org/isdl.php>`_ is needed to create the installer (default installation path assumed)

Preparing things
----------------

* Clone VapourSynth
* Run ``install_deps.bat``

This will put everything in the correct location assuming you have VapourSynth installed. Or alternatively you can do every step manually:

* Clone VapourSynth repository
* Clone zimg into the VapourSynth dir (``git clone https://github.com/sekrit-twc/zimg --recurse-submodules``)
* Clone libp2p into the VapourSynth dir (``git clone https://github.com/sekrit-twc/libp2p``)

Compilation
-----------

* Run ``compile_all.bat``.

Linux, OS X and Others Compilation
**********************************

These are the requirements:

* Meson 1.3.0 or later
* ninja-build
* pkg-config
* GCC or Clang, must be recent enough to support C++17
* `zimg <https://github.com/sekrit-twc/zimg>`_
* Python 3.12 or later
* Cython 3.1.x or later installed in your Python 3 environment
* Sphinx for the documentation (optional)

Compilation
-----------

* Clone VapourSynth repository

Enter the VapourSynth directory and run these commands to compile and install::

   python -m build --wheel

You can then install the wheel found in the dist directory.

Alternatively, you can let pip directly install from GitHub without cloning the repository first::

   pip install git+https://github.com/vapoursynth/vapoursynth.git

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

Simply run ``vsrepo install <namespace or identifier>`` to install them.

If you need a list of known plugins and scripts you can run ``vsrepo available`` or visit `vsdb.top <http://vsdb.top/>`_.

For more information, visit `vsrepo's repository <https://github.com/vapoursynth/vsrepo>`_

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