OSX Compilation Instructions
============================

This is a simple guide to on how to compile VapourSynth on OSX for those who are a bit lazy.
It's been tested on a clean install of OSX 10.10 and compiles all parts except the OCR and ImageMagick filters.

Prerequisites
#############

First download and install the prerequisites:
   * Xcode -- Available from the AppStore
   * `Homebrew <http://brew.sh/>`_ -- A package manager

Required packages
#########################

Installation of the required packages is very easy. Simply run these commands in a terminal and wait for them to complete::

   brew install python3 yasm ffmpeg libass
   pip3 install cython sphinx
   
If you've already installed all the required packages and instead want to update them simply run::

   brew update && brew upgrade
   pip3 install --upgrade cython sphinx

Compilation
###########

If you haven't checked out the source code before use git to do so::

   git clone https://github.com/vapoursynth/vapoursynth.git
   
Or if if you already have a copy of the source update it with::

   git pull

Enter the VapourSynth directory and run these commands to compile and install::
   
   export LC_ALL=en_US.UTF-8
   export LANG=en_US.UTF-8
   python3 bootstrap.py
   python3 waf configure
   python3 waf build
   python3 waf install
   python3 setup.py install
   
You should now have a working installation based on the latest git.

More detailed
#############

There are a few more advanced hints and instructions in the INSTALL file.