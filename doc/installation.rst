Installation
============

Basic Program
#############

The installation contains two main steps:

1. Install VapourSynth core library.
2. Install the Python wrapper of VapourSynth.

After you completed the second step, you can test it by opening a Python command line
and type this::

   from vapoursynth import core
   print(str(core))

After pressing return at the final line, you should see the version printed along with a
few other lines describing the options used when instantiating the Core object.
In fact, these lines should be the same as the output result of ``vspipe --version``.

Windows Installation
********************

Prerequisites
-------------

First download and install the prerequisites:
   * `Python 3.13.x <http://www.python.org/>`_ or Python 3.8.x -- 64 bit version
   
Note that VapourSynth and Python have to be matched so both are either installed
for all users or for only for the current user.

Also note that per user installs will not install the required Visual Studio
2019 runtimes.

Installation
------------

Simply run the `VapourSynth installer <https://github.com/vapoursynth/vapoursynth/releases>`_.
It should automatically detect and install everything, including the Python wrapper.

If the tests mentioned at the beginning fails, there may be a bug in the installer or there are
old copies of vapoursynth.pyd and vapoursynth.dll lying around.

Windows Installation (Portable)
*******************************

By far the easiest way is to download and run the automatic script called *Install-Portable-VapourSynth-RXX.ps1*.
It will then automatically download and set up embedded Python, pip and VapourSynth in a subdirectorey called *vapoursynth-portable* by default. It's possible to pass arguments to it to specify the installed Python version in addition to an option to run it in unattended mode.

Or if you want to do it the manual and not recommended way follow these steps:
   * Download and decompress `Python 3.13.x <http://www.python.org/>`_ or Python 3.8.x -- 64 bit embeddable version
   * Decompress the `portable VapourSynth archive <https://github.com/vapoursynth/vapoursynth/releases>`_ into the Python dir and overwrite all existing files.
   * Install pip using `get-pip.py <https://bootstrap.pypa.io/get-pip.py>`_ or any other method.
   * Install the wheel from the *wheel* directory for the chosen Python version.
   * If using Python 3.8 rename *vsscriptpython38.dll* to *vsscript.dll* and delete the original file.

OS X Installation
*****************

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager
   
Simply run these commands in a terminal and wait for them to complete::

   brew install vapoursynth

Linux installation
******************

Several distributions have VapourSynth packages. Note that those packages are usually OUT OF DATE.
 
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

* Needs `Visual Studio 2022 <https://visualstudio.microsoft.com/de/vs/>`_
* It also needs `64bit <https://www.python.org/>`_ Python 3.8.x and 3.13.x (the msvc project assumes that you installed python for all users.)
* `InnoSetup <http://www.jrsoftware.org/isdl.php>`_ is needed to create the installer (default installation path assumed)
* `7-zip <https://www.7-zip.org/>`_ is needed to compress the portable version (default installation path assumed)

Preparing things
----------------

* Clone VapourSynth
* Clone VSRepo into the VapourSynth dir (``git clone https://github.com/vapoursynth/vsrepo``)
* Clone zimg into the VapourSynth dir (``git clone https://github.com/sekrit-twc/zimg --recurse-submodules``)
* Clone avs+ into the VapourSynth dir (``git clone https://github.com/AviSynth/AviSynthPlus``)
* Clone libp2p into the VapourSynth dir (``git clone https://github.com/sekrit-twc/libp2p``)
* Place 7z.exe and 7z.dll from `7-zip <https://www.7-zip.org/>`_ into the ``installer`` dir
* Place ``pfm-192-vapoursynth-win.exe`` into the ``installer`` dir. You can get this file from the embedded zip or an existing VapourSynth install.
* Run ``install_deps.bat``

Compilation
-----------

* Run ``compile_all.bat`` for 64bit.

.. note:: Note that the Avisynth side of AVFS won't work properly in debug builds (memory allocation and exceptions across module boundaries trolololol)


Linux and OS X Compilation
**************************

These are the requirements:
   * Autoconf, Automake, and Libtool, probably recent versions

   * pkg-config

   * GCC 4.8 or newer, or Clang

   * `zimg <https://github.com/sekrit-twc/zimg>`_

   * Python 3.8 or later (may work on earlier versions but these are never fully tested)

   * Cython 3.x or later installed in your Python 3 environment

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

   brew install python3 ffmpeg libass zimg imagemagick
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

VapourSynth automatically loads all the native plugins located in certain
folders. Autoloading works just like manual loading, with the exception
that any errors encountered while loading a plugin are silently ignored.

.. note::

   Avoid autoloading from folders that other applications might also
   use, such as /usr/lib or /usr/local/lib in a Linux system. Several
   users reported crashes when VapourSynth attempted to load some
   random libraries (\*cough\*wxgtk\*cough\*).

Windows
-------

Windows has in total 3 different autoloading directories: user plugins, core plugins and global plugins. They are searched in that order.
User plugins are always loaded first so that the current user can always decide which exact version of a plugin is used. Core plugins follow.
Global plugins are placed last to prevent them from overriding any of the included plugins by accident.

The searched paths are:

#. *<AppData>*\\VapourSynth\\plugins32 or *<AppData>*\\VapourSynth\\plugins64
#. *<VapourSynth path>*\\core\\plugins
#. *<VapourSynth path>*\\plugins

Note that the per user path is not created by default. 
On modern Windows versions the *AppData* directory is located in *<user>*\\AppData\\Roaming by default.

Shortcuts to the global autoload directory are located in the start menu.

Avisynth plugins are never autoloaded. Support for this may be added in the future.

User plugins should never be put into the *core\\plugins* directory.

Windows Portable
----------------

The searched paths are:

#. *<base path (portable.vs location)>*\\vs-plugins

User plugins should never be put into the *vs-coreplugins* directory.

Linux
-----

Autoloading can be configured using the file
$XDG_CONFIG_HOME/vapoursynth/vapoursynth.conf,
or $HOME/.config/vapoursynth/vapoursynth.conf if XDG_CONFIG_HOME is not
defined.

To provide your own path to the config file, you can use $VAPOURSYNTH_CONF_PATH.

Two configuration options may be used: **UserPluginDir**, empty by default,
and **SystemPluginDir**, whose default value is set at compile time to
``$libdir/vapoursynth``, or to the location passed to the ``--with-plugindir``
argument to ``configure``.

UserPluginDir is tried first, then SystemPluginDir.

Example vapoursynth.conf::

   UserPluginDir=/home/asdf/vapoursynth/plugins
   SystemPluginDir=/special/non/default/location


OS X
----

Autoloading can be configured using the file
$HOME/Library/Application Support/VapourSynth/vapoursynth.conf. Everything else is
the same as in Linux.

Like on linux, you can use $VAPOURSYNTH_CONF_PATH to provide your own configuration.
