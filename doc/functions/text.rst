Text
====

.. function:: Text(clip clip, string text[, int alignment=7, int scale=1])
   :module: text

   Text is a simple text printing filter. It doesn't use any external libraries
   for drawing the text. It uses a built-in bitmap font: the not-bold, 8×16
   version of Terminus. The font was not modified, only converted from PCF to an
   array of bytes.

   The font covers Windows-1252, which is a superset of ISO-8859-1 (aka latin1).
   Unprintable characters get turned into underscores. Long lines get wrapped in
   a dumb way. Lines that end up too low to fit in the frame are silently
   dropped.

   The *alignment* parameter takes a number from 1 to 9, corresponding to the
   positions of the keys on a numpad.

   The *scale* parameter sets an integer scaling factor for the bitmap font.

   *ClipInfo*, *CoreInfo*, *FrameNum*, and *FrameProps* are convenience functions
   based on *Text*.
