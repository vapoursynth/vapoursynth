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
    file containing *every* output the script sets by default, so a script with both video and
    audio outputs produces one file carrying all of them. Naming an output with
    ``--outputindex`` narrows it to just that one. See `Matroska output`_ for the formats it
    accepts.

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

Gray, YUV and RGB are supported at the standard depths of 8, 9, 10, 12, 14 and 16 bits, with YUV
past 8 bits limited to 4:2:0, 4:2:2 and 4:4:4. RGB is additionally supported at half and single
precision float. Custom formats outside these combinations have no identifier a reader would
recognise, so they are refused rather than written into a file nothing can open.

Not supported are float Gray and float YUV, which have no equivalent to decode into on the
reading side, and compat formats. Audio is written as PCM and covers every audio format
VapourSynth produces.

An alpha clip attached with *set_output(index, clip, alpha)* is written as part of the same track,
as a fourth plane alongside the video, for YUV at 4:2:0, 4:2:2 and 4:4:4 and for RGB, at most of
the standard depths. Combinations with no recognised identifier, along with Gray video and the
remaining subsamplings, drop the alpha with a warning instead; the video itself is unaffected. An
alpha clip that needs to survive in those cases can be set as an output of its own, where it
becomes an ordinary Gray track.

Selecting outputs and ranges
----------------------------

Every output the script sets is written unless ``--outputindex`` names one, which narrows the
file to that single track.

``--start`` and ``--end`` only work when a single track is being written, either because one was
selected or because the script only set one. They are refused otherwise, since a frame range
means different amounts of time to a video and an audio track and applying the same numbers to
both would silently misalign them. Trimming in the script itself has no such restriction.

``--timecodes`` and ``--json`` describe a single video track, so they require exactly one video
track among the muxed outputs; audio tracks alongside it are fine. The timecodes are taken from
the same timestamps written into the container, so the file describes the mkv exactly.

``--filter-time`` and ``--filter-time-graph`` report times by walking the graph from one node,
so they follow the same single track rule as ``--start`` and ``--end``.

Seeking
-------

When the output is a file, a seek index is written at the end and referenced from the start, so
players can seek without scanning. When it is a pipe there is no way to go back and record where
the index ended up, so it is left out; the file is still perfectly readable and players simply
scan to seek. Piping into a program that writes its own container, which is the usual reason to
pipe at all, is unaffected either way.

AVFS
####

The `AVFS <https://github.com/vapoursynth/avfs>`_ package has several uses. It can easily make
a script file openable by any application, as it appears like a real,
uncompressed avi file. It can also be used to bridge the 32/64 bit gap, since a
plain file can always be read.

Only windows is supported.

VFW
###

On windows, you can output video to VFW based programs.

If you install VapourSynth by installer, the VFW support is registered already if you selected that option.

If you installed using pip or the portable version you can register that specific installation to handle vfw by running::

    vapoursynth register-vfw