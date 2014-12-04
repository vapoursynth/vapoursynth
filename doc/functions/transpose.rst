Transpose
=========

.. function:: Transpose(clip clip)
   :module: std

   Flips the contents of the frames in the same way as a matrix transpose would
   do. Combine it with FlipVertical or FlipHorizontal to synthesize a left or
   right rotation. Calling Transpose twice in a row is the same as doing nothing
   (but slower).

   Here is a picture to illustrate what Transpose does::

                                 0   5  55
        0   1   1   2   3        1   8  89
        5   8  13  21  34   =>   1  13 144
       55  89 144 233 377        2  21 233
                                 3  34 377
