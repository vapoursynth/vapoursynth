.. _vivtc:

VIVTC
=====

VIVTC is a set of filters that can be used for inverse telecine.
It is a rewrite of some of tritical's TIVTC filters.

.. function:: VFM(clip clip, int order[, int field=order, int mode=1, bint mchroma=1, int cthresh=9, int mi=80, bint chroma=1, int blockx=16, int blocky=16, int y0=16, int y1=16, float scthresh=12, int micmatch=1, bint micout=0, clip clip2])
   :module: vivtc

   VFM is a field matching filter that recovers the original progressive frames
   from a telecined stream. VFM's output will contain duplicated frames, which
   is why it must be further processed by a decimation filter, like VDecimate.

   Unlike TFM, VFM does not have any postprocessing capabilities.

   VFM adds the following properties to every frame it outputs:
      VFMMics
         Array of five integers.

         It will contain the mic values for the five possible matches
         (p/c/n/b/u). Some of them may be unset (-1), depending on *micout*
         and *micmatch*.

      _Combed
         1 if VFM thinks the frame is combed, 0 if not.

      VFMMatch
         Match used for the frame.

         0 = p

         1 = c

         2 = n

         3 = b

         4 = u

      VFMSceneChange
         1 if VFM thinks the frame is a scene change, 0 if not.


   Parameters:
      clip
         Input clip. Only YUV420P8 with constant dimensions supported.

      order
         Sets the field order of the clip. If this parameter has the wrong
         value, VFM's output will be visibly wrong.

         0 - bottom field first

         1 - top field first

      field
         Sets the field to match from. It is recommended to set this to the same
         value as *order* unless you experience matching failures with that
         setting. In certain circumstances changing the field that is used to
         match from can have a large impact on matching performance.

         0 - bottom field

         1 - top field

         Default: same as *order*.

      mode
         Sets the matching mode or strategy to use. Plain 2-way matching
         (option 0) is the safest of all the options in the sense that it won't
         risk creating jerkiness due to duplicate frames when possible, but if
         there are bad edits or blended fields it will end up outputting combed
         frames when a good match might actually exist. 3-way matching + trying
         the 4th/5th matches if all 3 of the original matches are detected as
         combed (option 5) is the most risky in terms of creating jerkiness,
         but will almost always find a good frame if there is one. The other
         settings (options 1, 2, 3, and 4) are all somewhere in between options
         0 and 5 in terms of risking jerkiness and creating duplicate frames vs.
         finding good matches in sections with bad edits, orphaned fields,
         blended fields, etc.

         Possible settings are::

            0 = 2-way match                                                          (p/c)
            1 = 2-way match + 3rd match on combed                                    (p/c + n)
            2 = 2-way match + 3rd match (same order) on combed                       (p/c + u)
            3 = 2-way match + 3rd match on combed + 4th/5th matches if still combed  (p/c + n + u/b)
            4 = 3-way match                                                          (p/c/n)
            5 = 3-way match + 4th/5th matches on combed                              (p/c/n + u/b)

         The () at the end indicate the matches that would be used for that mode
         assuming order=1 and field=1.

         0 is the fastest and 5 is the slowest.

         Default: 1.

      mchroma
         Sets whether or not chroma is included during the match comparisons.
         In most cases it is recommended to leave this enabled. Only if your
         clip has bad chroma problems such as heavy rainbowing or other
         artifacts should you set this to false. Setting this to false could
         also be used to speed things up at the cost of some accuracy.

         Default: true.

      cthresh
         This is the area combing threshold used for combed frame detection.
         This essentially controls how "strong" or "visible" combing must be
         to be detected. Larger values mean combing must be more visible and
         smaller values mean combing can be less visible or strong and still
         be detected. Valid settings are from -1 (every pixel will be detected
         as combed) to 255 (no pixel will be detected as combed). This is
         basically a pixel difference value. A good range is between 8 to 12.

         Default: 9.

      mi
         The number of combed pixels inside any of the *blockx* by *blocky*
         size blocks on the frame for the frame to be detected as combed.
         While *cthresh* controls how "visible" the combing must be, this
         setting controls "how much" combing there must be in any localized
         area (a window defined by the *blockx* and *blocky* settings) on the
         frame. The minimum is 0, the maximum is *blocky* * *blockx* (at which
         point no frames will ever be detected as combed).

         Default: 80.

      chroma
         Sets whether or not chroma is considered in the combed frame decision.
         Only disable this if your source has chroma problems (rainbowing, etc)
         that are causing problems for the combed frame detection with *chroma*
         enabled. Actually, using chroma=false is usually more reliable, except
         in case there is chroma-only combing in the source.

         Default: true.

      blockx

      blocky
         Sets the size of the window used during combed frame detection. This
         has to do with the size of the area in which *mi* number of pixels are
         required to be detected as combed for a frame to be declared combed.
         See the *mi* parameter description for more info. Possible values are
         any power of 2 between 4 and 512.

         Defaults: 16, 16.

      y0

      y1
         The rows from *y0* to *y1* will be excluded from the field matching
         decision.
         This can be used to ignore subtitles, a logo, or other things that may
         interfere with the matching.
         Set *y0* equal to *y1* to disable.

         Defaults: 16, 16.

      scthresh
         Sets the scenechange threshold as a percentage of maximum change on the
         luma plane.
         Good values are in the 8 to 14 range.

         Default: 12.

      micmatch
         When micmatch is greater than 0, tfm will take into account the mic
         values of matches when deciding what match to use as the final match.
         Only matches that could be used within the current matching mode are
         considered. micmatch has 3 possible settings:

         0 - disabled. Modes 1, 2 and 3 effectively become identical to mode 0.
         Mode 5 becomes identical to mode 4.

         1 - micmatching will be used only around scene changes. See the
         *scthresh* parameter.

         2 - micmatching will be used everywhere.

         Default: 1.

      micout
         If true, VFM will calculate the mic values for all possible matches
         (p/c/n/b/u).
         Otherwise, only the mic values for the matches allowed by *mode* will
         be calculated.

         Default: false.

      clip2
         Clip that VFM will use to create the output frames. If *clip2* is used,
         VFM will perform all calculations based on *clip*, but will copy the
         chosen fields from *clip2*. This can be used to work around VFM's video
         format limitations. For example::

            # Assume original is a YUV444P16 clip.
            yv12 = c.resize.Bicubic(clip=original, format=vs.YUV420P8)
            # fieldmatched will be YUV444P16.
            fieldmatched = c.vivtc.VFM(clip=yv12, order=1, clip2=original)



.. function:: VDecimate(clip clip[, int cycle=5, bint chroma=1, float dupthresh=1.1, float scthresh=15, int blockx=32, int blocky=32, clip clip2, string ovr=""])
   :module: vivtc

   VDecimate is a decimation filter. It drops one in every *cycle* frames -- the
   one that is most likely to be a duplicate (mode 0 in TDecimate).

   Parameters:
      clip
         Input clip. Must have constant format and dimensions, known length,
         integer sample type, and bit depth between 8 and 16 bits per sample.

      cycle
         Size of a cycle, in frames. One in every *cycle* frames will be
         decimated.

         Default: 5.

      chroma
         Controls whether the chroma is considered when calculating frame
         difference metrics.

         Default: true when the input clip has chroma.

      dupthresh
         This sets the threshold for duplicate detection. If the difference
         metric for a frame is less than or equal to this value then it is
         declared a duplicate. This value is a percentage of maximum change
         for a block defined by the *blockx* and *blocky* values, so 1.1 means
         1.1% of maximum possible change.

         Default: 1.1.

      scthresh
         Sets the threshold for detecting scene changes. This value is a
         percentage of maximum change for the luma plane. Good values are
         between 10 and 15.

         Default: 15.

      blockx

      blocky
         Sets the size of the blocks used for metric calculations. Larger blocks
         give better noise suppression, but also give worse detection of small
         movements. Possible values are any power of 2 between 4 and 512.

         Defaults: 32, 32.

      clip2
         This has the same purpose as VFM's *clip2* parameter.

      ovr
         Text file containing overrides. This can be used to manually choose
         what frames get dropped.

         Example::

            # A comment. Lines starting with # will be ignored.
            # Drop a specific frame:
            314 -
            # Drop every fourth frame, starting at frame 1001, up to frame 5403:
            1001,5403 +++-+

         The frame numbers apply to the undecimated input clip, of course.

         The decimation pattern must contain *cycle* characters.

         If the overrides mark more than one frame per cycle, the first frame
         marked for decimation in the cycle will be dropped.


Large parts of this document were copied from "TFM - READ ME.txt" and
"TDecimate - READ ME.txt", written by Kevin Stone (aka tritical).
