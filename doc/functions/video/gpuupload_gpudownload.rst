GPUUpload/GPUDownload
=====================

.. function:: GPUUpload(vnode clip)
   :module: std

   Returns a GPU resident version (*vnode:gpu*) of *clip*, transferring each
   requested frame to the core's Vulkan device. The upload is also inserted
   automatically, with a log message, whenever a CPU clip is passed to a
   filter expecting a *vnode:gpu* clip, so calling it explicitly is only needed to
   control where in the graph the transfer happens.

.. function:: GPUDownload(vnode:gpu clip)
   :module: std

   Returns a CPU resident version (*vnode*) of *clip*. Planes that passed
   through the GPU untouched since their upload are returned as the original
   host memory without a transfer.

   Frame properties are unaffected by either direction. Requires a Vulkan 1.4
   capable device; see :doc:`../../gpufilters`.
