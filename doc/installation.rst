Installation Instructions
=========================

Prerequisites
#############

First download and install the prerequisites:
   * `Python 3.x <http://www.python.org/>`_ (32 bit version)
   * `Visual C++ 2010 SP1 redistributable <http://www.microsoft.com/en-us/download/details.aspx?id=8328>`_ (you may or may not have this one)

Installation
############

Unpack the VapourSynth binaries to Python's library path. This would be 'C:\\Python32\\Lib\\site-packages' if you installed Python 3.2 to the default location.

Test it by opening a Python command line (should be added to your start menu) and type this::

   import vapoursynth as vs
   c = vs.Core()
   print(c.version())

After typing the final line you should see the version printed along with a few other lines describing the options used when cinstantiating the Core object.
If the first import line fails you have most likely not placed the all the files in the right location or you don't have the required runtime installed.