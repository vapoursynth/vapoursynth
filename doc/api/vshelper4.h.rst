VSHelper4.h
===========

Table of contents
#################

Introduction_


Macros_
   VSH_STD_PLUGIN_ID_
   
   VSH_RESIZE_PLUGIN_ID_
   
   VSH_TEXT_PLUGIN_ID_

   VS_RESTRICT_

   `VSH_ALIGNED_MALLOC <VSH_ALIGNED_MALLOC_c_>`_

   `VSH_ALIGNED_FREE <VSH_ALIGNED_FREE_c_>`_

   VSMIN_

   VSMAX_


Functions_
   `vsh_aligned_malloc <vsh_aligned_malloc_cpp_>`_

   `vsh_aligned_free <vsh_aligned_free_cpp_>`_

   isConstantFormat_

   isSameVideoFormat_
   
   isSameVideoPresetFormat_
   
   isSameVideoInfo_
   
   isSameAudioFormat_

   isSameAudioInfo_

   muldivRational_

   addRational_

   reduceRational_

   int64ToIntS_
   
   doubleToFloatS_

   bitblt_

   areValidDimensions_


Introduction
############

This is a collection of helpful macros and functions. Note that all functions (not macros)
are either prefixed with `vsh_` in C mode or placed in the `vsh` namespace for C++. This documentation
will use the C++ names for these function.


Macros
######

VSH_STD_PLUGIN_ID
-----------------
Macro defined to the internal std plugin id provided for convenience.


VSH_RESIZE_PLUGIN_ID
--------------------
Macro defined to the internal resizer plugin id provided for convenience.


VSH_TEXT_PLUGIN_ID
------------------
Macro defined to the internal std plugin id provided for convenience.


VS_RESTRICT
-----------

Attempts to provide a portable definition of the C99 ``restrict`` keyword,
or its C++ counterpart.


.. _vsh_aligned_malloc_c:

VSH_ALIGNED_MALLOC
------------------

VSH_ALIGNED_MALLOC(pptr, size, alignment)

Expands to _aligned_malloc() in Windows, and posix_memalign() elsewhere. Note that
the arguments are in the style of posix_memalign().

*pptr* is a pointer to a pointer.


.. _vsh_aligned_free_c:

VSH_ALIGNED_FREE
----------------

VSH_ALIGNED_FREE(ptr)

Expands to _aligned_free() in Windows, and free() elsewhere.

*ptr* is a pointer.


VSMIN
-----

VSMIN(a, b)

Returns the minimum of the two numbers.


VSMAX
-----

VSMAX(a, b)

Returns the maximum of the two numbers.


Functions
#########

.. _vsh_aligned_malloc_cpp:

vsh_aligned_malloc
------------------

.. cpp:function:: T* vsh::vsh_aligned_malloc(size_t size, size_t alignment)

   A templated aligned malloc for C++. It uses the same functions as the
   `VSH_ALIGNED_MALLOC <VSH_ALIGNED_MALLOC_c_>`_ macro but is easier to use.


.. _vsh_aligned_free_cpp:

vsh_aligned_free
----------------

.. cpp:function:: void vsh::vsh_aligned_free(void *ptr)

   This simply uses the `VSH_ALIGNED_FREE <VSH_ALIGNED_FREE_c_>`_ macro.


isConstantFormat
----------------

.. cpp:function:: static inline bool vsh::isConstantFormat(const VSVideoInfo *vi)

   Checks if a clip's format and dimensions are known (and therefore constant).


isSameVideoFormat
-----------------

.. cpp:function:: static inline bool vsh::isSameVideoFormat(const VSVideoInfo *v1, const VSVideoInfo *v2)

   Checks if two clips have the same video format. If the format is
   unknown in both, it will be considered the same.
   
   
isSameVideoPresetFormat
-----------------------

.. cpp:function:: static inline bool vsh::isSameVideoPresetFormat(unsigned presetFormat, const VSVideoFormat *v, VSCore *core, const VSAPI *vsapi)

   Checks if a clip has the same video format as the preset.
   
   
isSameVideoInfo
---------------

.. cpp:function:: static inline bool vsh::isSameVideoInfo(const VSVideoInfo *v1, const VSVideoInfo *v2)

   Checks if two clips have the same video format and dimensions. If the format is
   unknown in both, it will be considered the same. This is also true for the
   dimensions. Framerate is not taken into consideration when comparing.



isSameAudioFormat
-----------------

.. cpp:function:: static inline bool vsh::isSameAudioFormat(const VSAudioInfo *v1, const VSAudioInfo *v2)

   Checks if two clips have the same audio format.


isSameAudioInfo
---------------

.. cpp:function:: static inline bool vsh::isSameAudioInfo(const VSAudioInfo *v1, const VSAudioInfo *v2)

   Checks if two clips have the same audio format and samplerate.


muldivRational
--------------

.. cpp:function:: static inline void vsh::muldivRational(int64_t *num, int64_t *den, int64_t mul, int64_t div)

   Multiplies two rational numbers and reduces the result, i.e.
   *num*\ /\ *den* \* *mul*\ /\ *div*. The result is stored in *num* and *den*.

   The caller must ensure that *div* is not 0.


reduceRational
--------------

.. cpp:function:: static inline void vsh::reduceRational(int64_t *num, int64_t *den)

   Reduces a rational number.
   

addRational
-----------

.. cpp:function:: static inline void vsh::addRational(int64_t *num, int64_t *den, int64_t addnum, int64_t addden)

   Adds two rational numbers and reduces the result, i.e.
   *num*\ /\ *den* + *addnum*\ /\ *addden*. The result is stored in *num* and *den*.


int64ToIntS
-----------

.. cpp:function:: static inline int vsh::int64ToIntS(int64_t i)

   Converts an int64_t to int with signed saturation. It's useful to silence
   warnings when reading integer properties from a VSMap and to avoid unexpected behavior on int overflow.


doubleToFloatS
--------------

.. cpp:function:: static inline int vsh::doubleToFloatS(double d)

   Converts a double to float. It's useful to silence
   warnings when reading double properties from a VSMap and mostly exists to mirror `int64ToIntS`_.


bitblt
------

.. cpp:function:: static inline void vsh::bitblt(void *dstp, int dst_stride, const void *srcp, int src_stride, size_t row_size, size_t height)

   Copies bytes from one plane to another. Basically, it is memcpy in a loop.

   *row_size* is in bytes.


areValidDimensions
------------------

.. cpp:function:: static inline bool vsh::areValidDimensions(const VSFormat *fi, int width, int height)

   Checks if the given dimensions are valid for a particular format, with regards
   to chroma subsampling.
