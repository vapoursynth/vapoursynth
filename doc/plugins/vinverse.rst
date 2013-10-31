.. _vinverse:

Vinverse
========

Vinverse is a simple filter to remove residual combing, based on
`an AviSynth script by Didée <http://forum.doom9.org/showthread.php?p=841641#post841641>`_.

.. function::   Vinverse(clip clip[, float sstr=2.7, int amnt=255, float scl=0.25])
   :module: vinverse

   Parameters:
      clip
         Clip to be processed. The bit depth must be 8 bits per sample.

      sstr
         Strength of contra sharpening.

      amnt
         Change no pixel by more than this. Valid range is [0, 255].

      scl
         Scale factor for VshrpD * VblurD < 0.
