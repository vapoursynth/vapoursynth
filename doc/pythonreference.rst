Python Reference
================
Here all the classes and functions in the Python module will be documented.

Color Family Constants
######################
The color family constants describe a group formats and the basic way their color information is stored. You should be familiar with all of them apart from maybe *YCOCR* and *COMPAT* which
is a special junk category for non-planar formats. These are the declared constants in the module::

   RGB
   YUV
   GRAY
   YCOCG
   COMPAT

Format Constants
################
Format constants exactly describe a format, all common and even more uncommon formats have handy constants predefined so in practice no one should really need to register one of their own.
These values are mostly used by the resizers to specify which format to convert to. The naming system is quite simple. First the color family, the the subsampling (only YUV has it) and after that how many
bits per sample in one plane, the exception to this rule is RGB which has the bits for all 3 planes added together. The long list of values::

   GRAY8
   GRAY16

   YUV420P8
   YUV422P8
   YUV444P8
   YUV410P8
   YUV411P8
   YUV440P8

   YUV420P9
   YUV422P9
   YUV444P9

   YUV420P10
   YUV422P10
   YUV444P10

   YUV420P16
   YUV422P16
   YUV444P16

   RGB24
   RGB27
   RGB30
   RGB48

   COMPATBGR32
   COMPATYUY2
