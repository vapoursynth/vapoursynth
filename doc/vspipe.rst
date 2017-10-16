VSPipe
######

Synopsis
========

**vspipe** <script> <outfile> [options]

vspipe's main purpose is to evaluate VapourSynth scripts and output the
frames to a file.

If *outfile* is a hyphen (``-``), vspipe will write to the standard output.

If *outfile* is a dot (``.``), vspipe will do everything as usual, except it
will not write the video frames anywhere.


Options
=======

``-a, --arg key=value``
    Argument to pass to the script environment, it a key with this name and value (bytes typed) will be set in the globals dict

``-s, --start N``
    Set output frame range (first frame)
  
``-e, --end N``
    Set output frame range (last frame)

``-o, --outputindex N``
    Select output index

``-r, --requests N``
    Set number of concurrent frame requests

``-y, --y4m``
    Add YUV4MPEG headers to output

``-t, --timecodes FILE``
    Write timecodes v2 file

``-p, --progress``
    Print progress to stderr

``-i, --info``
    Show video info and exit

``-v, --version``
    Show version info and exit


Examples
========

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

