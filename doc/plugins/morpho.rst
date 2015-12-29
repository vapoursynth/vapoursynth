.. _morpho:

Morpho
======

.. function:: Dilate(clip clip[, int size=5, int shape=0])
   :module: morpho

.. function:: Erode(clip clip[, int size=5, int shape=0])
   :module: morpho

.. function:: Open(clip clip[, int size=5, int shape=0])
   :module: morpho

.. function:: Close(clip clip[, int size=5, int shape=0])
   :module: morpho

.. function:: TopHat(clip clip[, int size=5, int shape=0])
   :module: morpho

.. function:: BottomHat(clip clip[, int size=5, int shape=0])
   :module: morpho

   A set of simple morphological filters. Useful for working with mask clips.

Parameters (common to all functions):

    clip
        Clip to be processed. Must be 8-16 bits per sample.

    size
        Size of the structuring element, in pixels.

    shape
        Shape of the structuring element. Possible values are:

            0: Square
            1: Diamond
            2: Circle
            