AVFS (AV FileSystem)
====================

AV FileSystem is based on `AVFS <http://www.turtlewar.org/avfs/>`_ and shares most of its
source code and functionality. This package has several uses. It can easily make
a script file openable by any application, as it appears like a real,
uncompressed avi file. It can also be used to bridge the 32/64 bit gap, since a
plain file can always be read.

To use it simply run avfs in the ``core32`` or ``core64`` directory with the script name as argument.
This will create a virtual file in ``C:\\Volumes``.

Note that this avfs version is also compatible with Avisynth scripts.