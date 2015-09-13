Windows Installation Instructions
=================================

Prerequisites
#############

First download and install the prerequisites:
   * `Python 3.5 <http://www.python.org/>`_ (32 or 64 bit version, make sure to install for all users and to different directories)
   * `Pismo File Mount Audit Package <http://www.pismotechnic.com/download/>`_
     (only if you want to use the advanced virtual filesystem features)

Installation
############

Simply run the VapourSynth installer. It should automatically detect everything.

Test it by opening a Python command line (should be added to your start menu)
and type this::

   import vapoursynth as vs
   c = vs.get_core()
   print(c.version())

After typing the final line you should see the version printed along with a
few other lines describing the options used when instantiating the Core object.
If this for some reason fails, there may be a bug in the installer or there are
old copies of vapoursynth.pyd and vapoursynth.dll lying around.

Installation of VSFS
####################

By default VSFS will be registered if the Pismo File Mount Audit Package was
installed before VapourSynth. If you install the Pismo File Mount Audit Package
after VapourSynth and still want to use this feature, either reinstall
VapourSynth or register it from the command line with
"pfm register <path>\\core32\\vsfs.dll" or "pfm register <path>\\core64\\vsfs.dll".

Linux and OS X Installation Instructions
========================================

This is a simple guide for compiling VapourSynth on OS X or Linux Mint (or any other Ubuntu derivative) for those who are a bit lazy.
It's been tested on a clean install of OS X 10.10 and Linux Mint 17.1 and compiles all parts except the OCR and ImageMagick filters.

Required packages (OS X)
#########################

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager

Installation of the required packages is very easy. Simply run these commands in a terminal and wait for them to complete::

   brew install python3 yasm ffmpeg libass
   pip3 install cython
   
If you've already installed all the required packages and instead want to update them, simply run::

   brew update && brew upgrade
   pip3 install --upgrade cython
   
Required packages (Linux Mint)
##############################

First download and install the required packages. Cython is installed through pip in order to get a more recent version than the system packages provide::

   apt-get install build-essential yasm git libavcodec-dev libswscale-dev libass-dev python3-pip python3-dev
   pip3 install cython
   
If you've already installed all the required packages and instead want to update them, simply update your system like normal and use pip to update Cython::

   pip3 install --upgrade cython

Compilation (Both)
##################

If you haven't checked out the source code before, use git to do so::

   git clone https://github.com/vapoursynth/vapoursynth.git
   
Or if you already have a copy of the source, update it with::

   git pull

Note that you may have to specify the prefix to use when calling configure to install VapourSynth into a path where your system will search for it by default.
Enter the VapourSynth directory and run these commands to compile and install::
   
   ./autogen.sh
   ./configure
   make
   make install
   
You should now have a working installation based on the latest git.


