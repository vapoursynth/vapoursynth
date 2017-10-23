.. _avisource:

AVISource
=========

.. function:: AVISource(string path[, string pixel_type, string fourcc, bint alpha=False])
   :module: avisource

.. function:: AVIFileSource(string path[, string pixel_type, string fourcc, bint alpha=False])
   :module: avisource

.. function:: OpenDMLSource(string path[, string pixel_type, string fourcc, bint alpha=False])
   :module: avisource

   Opens an AVI file using VFW in windows. *AVISource* should usually be
   the only function you have to use.
   
   The only two options are *fourcc*, which overrides the fourcc stored
   in the AVI, and *pixel_type*, which tells the decoder to prefer
   a certain output format.
   
   Accepted *pixel_type* values::
   
      YV24, YV16, YV12, YV411, YUY2, Y8, RGB32, RGB24, RGB48, P010, P016, P210, P216, v210
