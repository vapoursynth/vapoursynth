cdef extern from "include/VSHelper4.h" nogil:
    void bitblt "vsh::bitblt" (void *dstp, ptrdiff_t dst_stride, const void *srcp, ptrdiff_t src_stride, size_t row_size, size_t height)
