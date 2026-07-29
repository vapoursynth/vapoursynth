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

If *outfile* is a double hyphen (``--``), vspipe will do everything as usual, except it
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

``-c, --container <y4m/wav/w64/mkv>``
    Add headers for the specified format to the output.

    Unlike the other types, which wrap the single selected output, ``mkv`` writes a Matroska
    file containing *every* output the script sets, so a script with both video and audio
    outputs produces one file carrying all of them. ``--outputindex`` is ignored in that case.
    See `Matroska output`_ for the formats it accepts.

``-t, --timecodes FILE``
    Write timecodes v2 file

``-j, --json FILE``
    Write properties of output frames in json format to file

``-p, --progress``
    Print progress to stderr

``--filter-time``
    Records the time spent in each filter and prints it out at the end of processing.

``--filter-time-graph FILE``
    Write the output node's filter graph in dot format with time information to file after processing

``-i, --info``
    Show video info and exit

``-g, --graph <simple/full>``
    Print output node filter graph in dot format to outfile and exit

``--frame-ref-debug``
    Print frame allocation debug information

``-v, --version``
    Show version info and exit

``-h, --help``
    Show usage information and exit

Examples
********

Show script info:
    ``vspipe --info script.vpy -``

Write to stdout:
    ``vspipe [options] script.vpy -``

Write to a named pipe (Windows only):
    ``vspipe [options] script.vpy "\\\\.\\pipe\\<pipename>"``

Request all frames but don't output them:
    ``vspipe [options] script.vpy --``

Write frames 5-100 to file:
    ``vspipe --start 5 --end 100 script.vpy output.raw``

Pipe to x264 and write timecodes file:
    ``vspipe script.vpy - -c y4m --timecodes timecodes.txt | x264 --demuxer y4m -o script.mkv -``

Pass values to a script:
    ``vspipe --arg deinterlace=yes --arg "message=fluffy kittens" script.vpy output.raw``

Mux every output, video and audio together, and pipe it to ffmpeg:
    ``vspipe script.vpy - -c mkv | ffmpeg -i - -c:v ffv1 -c:a flac out.mkv``

Matroska output
***************

``-c mkv`` writes uncompressed video and PCM audio into a Matroska file. It exists to hand a
whole script, video and audio at once, to a program like ffmpeg through a single pipe, which
neither the raw output nor y4m can do. Frames are stored exactly as VapourSynth holds them,
so no pixel is converted on the way out.

Timestamps come from the clip's frame rate when it has one, computed per frame from the frame
index so that nothing drifts over a long clip. A clip with no frame rate is treated as variable
and its timeline is accumulated from the ``_DurationNum`` and ``_DurationDen`` frame properties,
which must then be present on every frame.

Supported formats
-----------------

Every integer format is supported: Gray, YUV and RGB at 8 to 16 bits, in all of VapourSynth's
subsamplings. RGB is additionally supported at half and single precision float.

Not supported are float Gray and float YUV, which have no equivalent to decode into on the
reading side, and compat formats. Audio is written as PCM and covers every audio format
VapourSynth produces.

An alpha clip attached with *set_output(index, clip, alpha)* is written as part of the same track,
as a fourth plane alongside the video, for YUV at 4:2:0, 4:2:2 and 4:4:4 and for RGB at every
depth. The remaining subsamplings and Gray video have no layout that can carry an alpha plane, so
there the alpha is dropped and a warning says so; the video itself is unaffected. An alpha clip
that needs to survive in those cases can be set as an output of its own, where it becomes an
ordinary Gray track.

Seeking
-------

When the output is a file, a seek index is written at the end and referenced from the start, so
players can seek without scanning. When it is a pipe there is no way to go back and record where
the index ended up, so it is left out; the file is still perfectly readable and players simply
scan to seek. Piping into a program that writes its own container, which is the usual reason to
pipe at all, is unaffected either way.

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
