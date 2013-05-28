Python Reference
================
Here all the classes and functions in the Python module will be documented.

Classes and Functions
#####################
.. py:function:: get_core([threads = 0, add_cache = True, accept_lowercase = False])

   Get the singleton Core object. If it is the first time the function is called the Core will be instantiated with the given options.
   If the Core already has been instantiated all options are ignored. Setting *threads* to a value greater than zero overrides the autodetection.

.. py:class:: Core([threads = 0, add_cache = True, accept_lowercase = False])

   Create a new instance of the Core class.
   All loaded plugins are exposed as attributes of the core object. These attributes in turn hold the contained functions in the plugin.
   Use *list_functions()* to obtain a full list of all the currently named plugins you may call this way.
   
   .. py:method:: set_max_cache_size(mb)
   
      Set the upper framebuffer cache size after which memory is aggressively freed. The value is in megabytes.
   
   .. py:method:: list_functions()
   
      Returns a string containing all the loaded plugins and internal functions.
   
   .. py:method:: register_format(color_family, sample_type, bits_per_sample, subsampling_w, subsampling_h)
   
      Register a new Format object or obtain a reference to an existing one if it has already been registered.
   
   .. py:method:: get_format(id)
   
      Retrieve a Format object corresponding to the id.
   
   .. py:method:: version()
   
      Returns version information as a string.
   
.. py:class:: VideoNode

   Represents a video clip. The class itself supports indexing and slicing to perform trim, reverse and selectevery operations.
   Several operators are also defined for the VideoNode class: addition appends clips and multiplication repeats them.
   Note that slicing and indexing always return a new VideoNode object and not a VideoFrame.
   
   .. py:attribute:: format
   
      A Format object describing the frame data. If the format can change between frames this value is None.
      
   .. py:attribute:: width
   
      The width of the video. This value will be 0 if the width and height can change between frames.
      
   .. py:attribute:: height
   
      The height of the video. This value will be 0 if the width and height can change between frames.
      
   .. py:attribute:: num_frames
   
      The number of frames in the clip. This value will be 0 if the total number of frames is unknown or infinite.
    
   .. py:attribute:: fps_num
   
      The numerator of the framerate. If the clip has variable framerate the value will be 0.
      
   .. py:attribute:: fps_den
   
      The denominator of the framerate. If the clip has variable framerate the value will be 0.
      
   .. py:attribute:: flags
   
      Special flags set for this clip. This attribute should normally be ignored.
      
   .. py:method:: get_frame(n)
   
      Returns a VideoFrame from position n.
   
   .. py:method:: output(fileobj[, y4m = False, prefetch = 0, progress_update = None])
   
      Write the whole clip to the specified file handle. It is possible to pipe to stdout by specifying *sys.stdout* as the file.
      YUV4MPEG2 headers will be appended when *y4m* is true.
      The current progress can be reported by passing a callback function of the form *func(current_frame, total_frame)* to *progress_update*.
      The *prefetch* argument is only for debugging purposes.
      
.. py:class:: VideoFrame

      This class represents a video frame and all metadata attached to it. 

   .. py:attribute:: format
    
      A Format object describing the frame data.
    
   .. py:attribute:: width
    
      The width of the frame.
    
   .. py:attribute:: height
       
      The height of the frame.
       
   .. py:attribute:: readonly
       
      If *readonly* is True the frame data and properties cannot be modified.
       
   .. py:attribute:: props
    
      This attribute holds all the frame's properties mapped as sub-attributes.
      
   .. py:method:: copy()

      Returns a writable copy of the frame.

   .. py:method:: get_read_ptr(plane)
   
      Returns a pointer to the raw frame data. The data may not be modified.

   .. py:method:: get_write_ptr(plane)
   
      Returns a pointer to the raw frame data. It may be written to using ctypes or some other similar python package.
   
   .. py:method:: get_stride(plane)
   
      Returns the stride between lines in a *plane*.
      
.. py:class:: Format

   This class represents all information needed to describe a frame format. It holds the general color type, subsampling, number of planes and so on.
   The names map directly to the C API so consult it for more detailed information.
      
   .. py:attribute:: id
   
      A unique *id* identifying the format.
      
   .. py:attribute:: name
      
      A human readable name of the format.
      
   .. py:attribute:: color_family
   
      Which group of colorspaces the format describes.
      
   .. py:attribute:: sample_type
   
      If the format is integer or floating point based.
      
   .. py:attribute:: bits_per_sample
   
      How many bits are used to store one sample in one plane.
      
   .. py:attribute:: bytes_per_sample
   
      The actual storage is padded up to 2^n bytes for efficiency.
      
   .. py:attribute:: subsampling_w
   
      The subsampling for the second and third plane in the horizontal direction.
      
   .. py:attribute:: subsampling_h
   
      The subsampling for the second and third plane in the vertical direction.
      
   .. py:attribute:: num_planes
   
      The number of planes the format has.
      
.. py:exception:: Error

   The standard exception class. This exception is thrown on most errors encountered in VapourSynth.
   
Color Family Constants
######################
The color family constants describe a group formats and the basic way their color information is stored. You should be familiar with all of them apart from maybe *YCOCG* and *COMPAT* which
is a special junk category for non-planar formats. These are the declared constants in the module::

   RGB
   YUV
   GRAY
   YCOCG
   COMPAT

Format Constants
################
Format constants exactly describe a format. All common and even more uncommon formats have handy constants predefined so in practice no one should really need to register one of their own.
These values are mostly used by the resizers to specify which format to convert to. The naming system is quite simple. First the color family, then the subsampling (only YUV has it) and after that how many
bits per sample in one plane. The exception to this rule is RGB, which has the bits for all 3 planes added together. The long list of values::

   GRAY8
   GRAY16
   GRAYH
   GRAYS

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
   
   YUV444PH
   YUV444PS

   RGB24
   RGB27
   RGB30
   RGB48
   
   RGBH
   RGBS

   COMPATBGR32
   COMPATYUY2
