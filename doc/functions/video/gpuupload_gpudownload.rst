GPUUpload/GPUDownload
=====================

.. function:: GPUUpload(vnode:all clip)
   :module: std

   Returns a GPU resident version of *clip*, transferring each
   requested frame to the core's Vulkan device. The upload is also inserted
   automatically, with a log message, whenever a CPU clip is passed to a
   filter expecting a GPU clip, so calling it explicitly is only needed to
   control where in the graph the transfer happens.

.. function:: GPUDownload(vnode:all clip)
   :module: std

   Returns a CPU resident version of *clip*, transferring every
   plane back from the device.

   If a clip is already in the requested location it does nothing.
