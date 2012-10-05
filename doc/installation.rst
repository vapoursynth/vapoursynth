Installation Instructions
=========================

Prerequisites
#############

First download and install the prerequisites:
   * `Python 3.3 <http://www.python.org/>`_ (32 bit version)
   * `Pismo File Mount Audit Package <http://www.pismotechnic.com/download/>`_ (only if you want to use the advanced virtual filesystem features)

Installation
############

Simply run the VapourSynth installer. It should automatically detect everything.

Test it by opening a Python command line (should be added to your start menu) and type this::

   import vapoursynth as vs
   c = vs.Core()
   print(c.version())

After typing the final line you should see the version printed along with a few other lines describing the options used when instantiating the Core object.
If this for some reason fails there may be a bug in the installer or there are old copied of vapousynth.pyd and vapoursynth.dll lying around.

Installation of VSFS
####################
By default vsfs isn't enabled and you will have to do it yourself. Assuming you've already installed the Pismo File Mount Audit Package simply open a command prompt and
type "pfm register <installation directory here>\\vsfs.dll". After this you can right click any .vpy script and select *Quick mount* to have it appear as a virtual directory with
and avi file and an error log.

Compiling
#########
If you're not on windows you have to compile VapourSynth yourself. Check out the source and follow the instructions in the INSTALL file.