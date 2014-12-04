About
=====

VapourSynth is an application for video manipulation. Or a plugin. Or a library.
It's hard to tell because it has a core library written in C++ and a Python
module to allow video scripts to be created. It came to be when I started
thinking about alternative designs for Avisynth and most of it was written
over a 3 month period.

The software has been heavily inspired by `Avisynth <http://www.avisynth.org>`_
and aims to be a 21st century rewrite, taking advantage of the advancements
computers have made since the late 90s.
The main features compared to Avisynth are:

   * Multithreaded - Frame level multithreading that scales well
   * Generalized Colorspaces - New colorspaces can be specified at runtime
   * Per Frame Properties - Additional metadata can be attached to frames
   * Python Based - The scripting part is implemented as a Python module so you
     don't have to learn a special language
   * Support for video with format changes - Some video just can't stick to one
     format or frame size. VapourSynth can handle any kind of change
   * Compatible with a large number of already existing Avisynth plugins

About the author
################

Fredrik Mellbin majored in electrical engineering with a focus on image analysis
and processing with medical applications. He has previously worked with digital
electronics and likes to plan his own software projects in his spare time.
When he one day found himself out of work he needed something to do between
sending out job applications and waiting for a reply. The natural choice for
the author was to try to improve Avisynth, the software that once made him
interested in video editing. VapourSynth is the result of all that time waiting.

Feel free to contact me at fredrik.mellbin that round thingy with an a gmail.com
if you need help to port a filter or want to sponsor the development.
