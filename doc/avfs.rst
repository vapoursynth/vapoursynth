AVFS (AV FileSystem)
====================

AV FileSystem is based on `AVFS <https://turtlewar.org/avfs/>`_ and shares most of its
source code and functionality. This package has several uses. It can easily make
a script file openable by any application, as it appears like a real,
uncompressed avi file. It can also be used to bridge the 32/64 bit gap, since a
plain file can always be read.

To use it simply run ``avfs`` in the ``core32`` or ``core64`` directories with the script name as argument.
This will create a virtual file in ``C:\\Volumes``.

Avisynth Support
################
Note that this AVFS version is also compatible with Avisynth 2.6 and Avisynth+. When using Avisynth+
higher bitdepth output is also supported. The easiest way to obtain a recent version is to extract
``avfs.exe`` from the portable VapourSynth archives.