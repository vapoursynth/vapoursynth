Output
======

VSPipe
######

Synopsis
********

**vspipe** <script> <outfile> [options]

vspipe's main purpose is to evaluate VapourSynth scripts and output the
frames to a file.

If *outfile* is a hyphen (``-``), vspipe will write to the standard output.

If *outfile* is a dot (``.``), vspipe will do everything as usual, except it
will not write the video frames anywhere.


Options
*******

``-a, --arg key=value``
    Argument to pass to the script environment, it a key with this name and value (str typed) will be set in the globals dict

``-s, --start N``
    Set output frame range (first frame)
  
``-e, --end N``
    Set output frame range (last frame)

``-o, --outputindex N``
    Select output index

``-r, --requests N``
    Set number of concurrent frame requests

``-c, --container <y4m/wav/w64>``
    Add headers for the specified format to the output

``-t, --timecodes FILE``
    Write timecodes v2 file

``-p, --progress``
    Print progress to stderr
    
``--filter-time``
    Records the time spent in each filter and prints it out at the end of processing.

``-i, --info``
    Show video info and exit

``-g, --graph <simple/full>``
    Print output node filter graph in dot format to outfile and exit

``-v, --version``
    Show version info and exit


Examples
********

Show script info:
    ``vspipe --info script.vpy -``

Write to stdout:
    ``vspipe [options] script.vpy -``

Request all frames but don't output them:
    ``vspipe [options] script.vpy .``

Write frames 5-100 to file:
    ``vspipe --start 5 --end 100 script.vpy output.raw``

Pipe to x264 and write timecodes file:
    ``vspipe script.vpy - --y4m --timecodes timecodes.txt | x264 --demuxer y4m -o script.mkv -``

Pass values to a script:
    ``vspipe --arg deinterlace=yes --arg "message=fluffy kittens" script.vpy output.raw``

AVFS
####

AV FileSystem is based on `AVFS <https://turtlewar.org/avfs/>`_ and shares most of its
source code and functionality. This package has several uses. It can easily make
a script file openable by any application, as it appears like a real,
uncompressed avi file. It can also be used to bridge the 32/64 bit gap, since a
plain file can always be read.

To use it simply run ``avfs`` in the ``core32`` or ``core64`` directories with the script name as argument.
This will create a virtual file in ``C:\\Volumes``.

The *alt_output* argument of *set_output* is respected and can be used to get additional compatibility
with professional applications.

Avisynth Support
****************

Note that this AVFS version is also compatible with Avisynth 2.6 and Avisynth+. When using Avisynth+
higher bitdepth output is also supported. The easiest way to obtain a recent version is to extract
``avfs.exe`` from the portable VapourSynth archives.

VFW
###

On windows, you can output video to VFW based programs.

If you install VapourSynth by installer, the VSVFW.dll is registered already

Else, you could register it manually, use register file below or use `theChaosCoder's batch <https://github.com/theChaosCoder/vapoursynth-portable-FATPACK/blob/master/VapourSynth64Portable/extras/enable_vfw_support.bat>`_.

::

    Windows Registry Editor Version 5.00

    [HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}]
    @="VapourSynth"

    [HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}\InProcServer32]
    @="<your VSVFW.dll directory>\\VSVFW.dll"
    "ThreadingModel"="Apartment"

    [HKEY_LOCAL_MACHINE\SOFTWARE\Classes\AVIFile\Extensions\VPY]
    @="{58F74CA0-BD0E-4664-A49B-8D10E6F0C131}"
