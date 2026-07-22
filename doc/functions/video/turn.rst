Turn
====

.. function:: Turn90(vnode clip)
              Turn180(vnode clip)
              Turn270(vnode clip)
   :module: std

   Turns the frames in a clip by the given number of degrees clockwise, which
   means that Turn90 turns them to the right, Turn270 to the left and Turn180
   upside down.

   Turn90 and Turn270 swap the output width and height, and with them the chroma
   subsampling, so a 4:2:2 clip becomes 4:4:0 and the other way around. Turn180
   leaves the dimensions and the format alone.

   A quarter turn is a transpose with one of the two axes reversed, so Turn90 and
   Turn270 do the same amount of work as Transpose. Combining Transpose with
   FlipVertical or FlipHorizontal produces the same result but needs an extra
   pass over the frame.

   Turning a clip twice in the same direction is the same as Turn180, and turning
   it once in each direction returns the original clip.

   Here is a picture to illustrate what the filters do::

                                        55   5   0
        0   1   1   2   3               89   8   1
        5   8  13  21  34   Turn90  => 144  13   1
       55  89 144 233 377              233  21   2
                                       377  34   3

        0   1   1   2   3              377 233 144  89  55
        5   8  13  21  34   Turn180 =>  34  21  13   8   5
       55  89 144 233 377                3   2   1   1   0

                                         3  34 377
        0   1   1   2   3                2  21 233
        5   8  13  21  34   Turn270 =>   1  13 144
       55  89 144 233 377                1   8  89
                                         0   5  55
