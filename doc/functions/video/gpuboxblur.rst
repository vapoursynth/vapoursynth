GPUBoxBlur
==========

.. function:: GPUBoxBlur(vnode:gpu clip[, int[] planes, int hradius = 1, int hpasses = 1, int vradius = 1, int vpasses = 1])
   :module: std

   The GPU resident equivalent of BoxBlur, taking the same arguments and
   producing bit identical output for integer formats. Float formats are
   computed in a different accumulation order than the CPU implementation and
   match closely but not exactly. Unprocessed planes are passed through
   without copying.

   A CPU clip may be passed directly, in which case the upload is inserted
   automatically; see :doc:`../../gpufilters`.
