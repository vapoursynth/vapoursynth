VSHelper.h
==========

Table of contents
#################

Introduction_


Macros_
   inline_

   VS_RESTRICT_

   `VS_ALIGNED_MALLOC <VS_ALIGNED_MALLOC_c_>`_

   `VS_ALIGNED_FREE <VS_ALIGNED_FREE_c_>`_

   VSMIN_

   VSMAX_


Functions_
   `vs_aligned_malloc <vs_aligned_malloc_cpp_>`_

   `vs_aligned_free <vs_aligned_free_cpp_>`_

   isConstantFormat_

   isSameFormat_

   muldivRational_

   vs_addRational_

   vs_normalizeRational_

   int64ToIntS_

   vs_bitblt_

   areValidDimensions_


Introduction
############

This is a collection of helpful macros and functions.


Macros
######

inline
------

It's defined to ``_inline`` for Visual Studio's C mode.


VS_RESTRICT
-----------

Attempts to provide a portable definition of the C99 ``restrict`` keyword,
or its C++ counterpart.


.. _vs_aligned_malloc_c:

VS_ALIGNED_MALLOC
-----------------

VS_ALIGNED_MALLOC(pptr, size, alignment)

Expands to _aligned_malloc() in Windows, and posix_memalign() elsewhere. Note that
the arguments are in the style of posix_memalign().

*pptr* is a pointer to a pointer.


.. _vs_aligned_free_c:

VS_ALIGNED_FREE
---------------

VS_ALIGNED_FREE(ptr)

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

.. _vs_aligned_malloc_cpp:

vs_aligned_malloc
-----------------

.. cpp:function:: T* vs_aligned_malloc(size_t size, size_t alignment)

   A templated aligned malloc for C++. It uses the same functions as the
   `VS_ALIGNED_MALLOC <VS_ALIGNED_MALLOC_c_>`_ macro but is easier to use.


.. _vs_aligned_free_cpp:

vs_aligned_free
---------------

.. cpp:function:: void vs_aligned_free(void *ptr)

   This simply uses the `VS_ALIGNED_FREE <VS_ALIGNED_FREE_c_>`_ macro.


isConstantFormat
----------------

.. c:function:: static inline int isConstantFormat(const VSVideoInfo *vi)

   Checks if a clip's format and dimensions are known (and therefore constant).


isSameFormat
------------

.. c:function:: static inline int isSameFormat(const VSVideoInfo *v1, const VSVideoInfo *v2)

   Checks if two clips have the same format and dimensions. If the format is
   unknown in both, it will be considered the same. This is also true for the
   dimensions.


muldivRational
--------------

.. c:function:: static inline void muldivRational(int64_t *num, int64_t *den, int64_t mul, int64_t div)

   Multiplies two rational numbers and reduces the result, i.e.
   *num*\ /\ *den* \* *mul*\ /\ *div*. The result is stored in *num* and *den*.

   The caller must ensure that *div* is not 0.


vs_addRational
--------------

.. c:function:: static inline void vs_addRational(int64_t *num, int64_t *den, int64_t addnum, int64_t addden)

   Adds two rational numbers and reduces the result, i.e.
   *num*\ /\ *den* + *addnum*\ /\ *addden*. The result is stored in *num* and *den*.


vs_normalizeRational
--------------------

.. c:function:: static inline void vs_normalizeRational(int64_t *num, int64_t *den)

   Reduces a rational number.


int64ToIntS
-----------

.. c:function:: static inline int int64ToIntS(int64_t i)

   Converts an int64_t to int with signed saturation. It's useful to silence
   warnings when reading integer properties from a VSMap and to avoid unexpected behavior on int overflow.


vs_bitblt
---------

.. c:function:: static inline void vs_bitblt(void *dstp, int dst_stride, const void *srcp, int src_stride, size_t row_size, size_t height)

   Copies bytes from one plane to another. Basically, it is memcpy in a loop.

   *row_size* is in bytes.


areValidDimensions
------------------

.. c:function:: static inline int areValidDimensions(const VSFormat *fi, int width, int height)

   Checks if the given dimensions are valid for a particular format, with regards
   to chroma subsampling.
