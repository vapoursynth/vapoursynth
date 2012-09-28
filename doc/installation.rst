Installation Instructions
=========================

Prerequisites
#############

First download and install the prerequisites:
   * `Python 3.2.x <http://www.python.org/>`_ (32 bit version)

Installation
############

Simply run the VapourSynth installer. It should automatically detect everything.

Test it by opening a Python command line (should be added to your start menu) and type this::

   import vapoursynth as vs
   c = vs.Core()
   print(c.version())

After typing the final line you should see the version printed along with a few other lines describing the options used when instantiating the Core object.
If this for some reason fails there may be a bug in the installer or there are old copied of vapousynth.pyd and vapoursynth.dll lying around.

Compiling
#########
If you're not on windows you have to compile VapourSynth yourself. Check out the source and follow the instructions in the INSTALL file.