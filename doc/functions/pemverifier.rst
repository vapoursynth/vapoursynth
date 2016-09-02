PEMVerifier
===========

.. function:: PEMVerifier(clip clip[, float[] upper, float[] lower])
   :module: std

   The *PEMVerifier* is used to check for out-of-bounds pixel values during filter
   development. It is a public function so badly coded filters won't go
   unnoticed.

   If no values are set, then *upper* and *lower* default to the max and min values
   allowed in the current format. If an out of bounds value is
   encountered a frame error is set and the coordinates of the first bad pixel
   are included in the error message.
