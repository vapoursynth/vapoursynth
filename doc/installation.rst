Installation Instructions
=========================

Prerequisites
#############

First download and install the prerequisites:
   * `Python 3.3 <http://www.python.org/>`_ (32 or 64 bit version, make sure to install for all users)
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
If this for some reason fails there may be a bug in the installer or there are
old copies of vapoursynth.pyd and vapoursynth.dll lying around.

Installation of VSFS
####################

By default VSFS will be registered if the Pismo File Mount Audit Package was
installed before VapourSynth. If you install the Pismo File Mount Audit Package
after VapourSynth and still want to use this feature either reinstall
VapourSynth or register it from the commandline with
"pfm register <path>\\core32\\vsfs.dll" or "pfm register <path>\\core64\\vsfs.dll".

Compiling
#########

If you're not on Windows you have to compile VapourSynth yourself.
Check out the source and follow the instructions in the INSTALL file.
