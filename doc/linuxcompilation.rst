Linux Mint Compilation Instructions
===================================

This is a simple guide for compiling VapourSynth on Linux Mint for those who are a bit lazy.
It's been tested on a clean install of Linux Mint 17 with all updates and compiles all parts except the OCR and ImageMagick filters.
As usual you'll need to prefix quite a few of these commands with *sudo*.

Required packages
#########################

First download and install the required packages. Cython is installed through pip in order to get a more recent version::

   apt-get install build-essential yasm git libavcodec-dev libswscale-dev libass-dev python3-pip python3-dev python3-sphinx
   pip3 install cython
   
If you've already installed all the required packages and instead want to update them, simply update your system like normal and use pip to update Cython::

   pip3 install --upgrade cython

Compilation
###########

If you haven't checked out the source code before, use git to do so::

   git clone https://github.com/vapoursynth/vapoursynth.git
   
Or if you already have a copy of the source, update it with::

   git pull

Enter the VapourSynth directory and run these commands to compile and install::
   
   python3 bootstrap.py
   python3 waf configure
   python3 waf build
   python3 waf install
   python3 setup.py install
   
You should now have a working installation based on the latest git.

More detailed
#############

There are a few more advanced hints and instructions in the INSTALL file.
