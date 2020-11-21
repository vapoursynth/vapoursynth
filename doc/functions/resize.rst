Resize
======

.. function::   Bilinear(clip clip[, int width, int height, int format, enum matrix, enum transfer, enum primaries, enum range, enum chromaloc, enum matrix_in, enum transfer_in, enum primaries_in, enum range_in, enum chromaloc_in, float filter_param_a, float filter_param_b, string resample_filter_uv, float filter_param_a_uv, float filter_param_b_uv, string dither_type="none", string cpu_type, bint prefer_props=False, float src_left, float src_top, float src_width, float src_height, float nominal_luminance])
                Bicubic(clip clip[, ...])
                Point(clip clip[, ...])
                Lanczos(clip clip[, ...])
                Spline16(clip clip[, ...])
                Spline36(clip clip[, ...])
                Spline64(clip clip[, ...])
   :module: resize
   
   In VapourSynth the resizers have several functions. In addition to scaling,
   they also do colorspace conversions and conversions to and from the compat
   formats. Resize converts a clip of known or unknown format to another clip
   of known or unknown format, changing only the parameters specified by the
   user. The resize filters can handle varying size and format input clips
   and turn them into constant format clips.

   If you do not know which resizer to choose, then try Bicubic. It usually
   makes a good neutral default.

   Arguments denoted as type *enum* may be specified by numerical index (see
   ITU-T H.265 Annex E.3) or by name. Enums specified by name have their
   argument name suffixed with "_s". For example, a destination matrix of
   BT 709 can be specified either with ``matrix=1`` or with ``matrix_s="709"``.

   Note that *matrix* is not an optional argument when converting to YUV.
   Also note that if no matrix is specified in an input YUV frame's properties
   then *matrix_in* also needs to be set.
   
   The function will return an error if the subsampling restrictions aren't
   followed.  

   If you get an error like::

      Resize error 3074: no path between colorspaces (2/2/2 => 1/1/1).
      May need to specify additional colorspace parameters.

   It usually means the matrix/transfer/primaries are unknown and you have to
   specify the input colorspace paramaters yourself. Note: 2 means "unspecified"
   according to the ITU-T recommendation.

   *clip*:
   
      Accepts all kinds of input.

   *width*, *height*:
   
      Output image dimensions.

   *format*:
   
      Output format id.

   *matrix*, *transfer*, *primaries*:

      Output colorspace specification. If not provided, the corresponding attributes from
      the input clip will be selected, except for YCoCg and RGB color families, where the
      corresponding matrix is set by default.

   *range*:

      Output pixel range. For integer formats, this allows selection of the legal code
      values. Even when set, out of range values (BTB/WTW) may be generated. If the input
      format is of a different color family, the default range is studio/limited for YUV
      and full-range for RGB.

   *chromaloc*:
   
      Output chroma location. For subsampled formats, specifies the chroma location. If
      the input format is 4:4:4 or RGB and the output is subsampled, the default location
      is left-aligned, as per MPEG. Possible chroma locations (ITU-T H.265 Figure E.1):
      *left*, *center*, *top_left*, *top*, *bottom_left*, *bottom*
      
   *matrix_in*, *transfer_in*, *primaries_in*, *range_in*, *chromaloc_in*:
   
      Input colorspace/format specification. If the corresponding frame property is set
      to a value other than unspecified, the frame property is used instead of this parameter.
      Default values are set for certain color families. See the equivalent output arguments
      for more information. By default these override the corresponding frame properties if
      present, to instead give the frame properties precedence when both are present set
      *prefer_props*.

   *filter_param_a*, *filter_param_b*:
   
      Parameters for the scaler used for RGB and Y-channel. For the bicubic filter,
      filter_param_a/b represent the "b" and "c" parameters. For the lanczos filter,
      filter_param_a represents the number of taps.

   *resample_filter_uv*:
   
      Scaling method for UV channels. It defaults to the same as for the Y-channel. The
      following values can be used with *resample_filter_uv*: *point*, *bilinear*, *bicubic*,
      *spline16*, *spline36*, *lanczos*.

   *filter_param_a_uv*, *filter_param_b_uv*:

      Parameters for the scaler used for UV channels.

   *dither_type*:
   
      Dithering method. Dithering is used only for conversions resulting in an integer
      format. The following dithering methods are available: *none*, *ordered*, *random*,
      *error_diffusion*.
      
   *cpu_type*:
   
      Only used for testing.
      
   *prefer_props*:
   
      Determines whether frame properties or arguments take precedence when both are present.
      This option affects the *matrix_in*, *transfer_in*, *primaries_in*, *range_in*
      and *chromaloc_in* arguments and their frame property equivalents.
      
   *src_left*, *src_top*, *src_width*, *src_height*:
   
      Used to select the source region of the input to use. Can also be used to shift the image.
      Defaults to the whole image.
      
   *nominal_luminance*:
   
      Determines the physical brightness of the value 1.0. The unit is in cd/m^2.
      
   To convert to YV12::

      Bicubic(clip=clip, format=vs.YUV420P8, matrix_s="709")

   To resize and convert YUV with color information frame properties to planar RGB::

      Bicubic(clip=clip, width=1920, height=1080, format=vs.RGB24)

   To resize and convert YUV without color information frame properties to planar RGB::

      Bicubic(clip=clip, width=1920, height=1080, format=vs.RGB24, matrix_in_s="709")

   The following tables list values of selected colorspace enumerations and
   their abbreviated names. (Numerical value in parentheses.) For all possible values,
   see ITU-T H.265.
   
      Matrix coefficients (ITU-T H.265 Table E.5)::
        
        rgb (0)        Identity
                       The identity matrix.
                       Typically used for GBR (often referred to as RGB);
                       however, may also be used for YZX (often referred to as
                       XYZ);
        709 (1)        KR = 0.2126; KB = 0.0722
                       ITU-R Rec. BT.709-5
        unspec (2)     Unspecified
                       Image characteristics are unknown or are determined by the
                       application.
        fcc (4)
        470bg (5)      KR = 0.299; KB = 0.114
                       ITU-R Rec. BT.470-6 System B, G (historical)
                       (functionally the same as the value 6 (170m))
        170m (6)       KR = 0.299; KB = 0.114
                       SMPTE 170M (2004)
                       (functionally the same as the value 5 (470bg))
        240m (7)       SMPTE 240M
        ycgco (8)      YCgCo
        2020ncl (9)    KR = 0.2627; KB = 0.0593
                       Rec. ITU-R BT.2020 non-constant luminance system
        2020cl (10)    KR = 0.2627; KB = 0.0593
                       Rec. ITU-R BT.2020 constant luminance system
        chromancl (12) Chromaticity derived non-constant luminance system
        chromacl (13)  Chromaticity derived constant luminance system
        ictcp (14)     ICtCp

      Transfer characteristics (ITU-T H.265 Table E.4)::
        
        709 (1)        V = a * Lc0.45 - ( a - 1 ) for 1 >= Lc >= b
                       V = 4.500 * Lc for b > Lc >= 0
                       Rec. ITU-R BT.709-5
                       (functionally the same as the values 6 (601),
                       14 (2020_10) and 15 (2020_12))
        unspec (2)     Unspecified
                       Image characteristics are unknown or are determined by the
                       application.
        470m (4)       ITU-R Rec. BT.470-6 System M       
        470bg (5)      ITU-R Rec. BT.470-6 System B, G (historical)
        601 (6)        V = a * Lc0.45 - ( a - 1 ) for 1 >= Lc >= b
                       V = 4.500 * Lc for b > Lc >= 0
                       Rec. ITU-R BT.601-6 525 or 625
                       (functionally the same as the values 1 (709),
                       14 (2020_10) and 15 (2020_12))
        240m (7)       SMPTE 240M       
        linear (8)     V = Lc for all values of Lc
                       Linear transfer characteristics
        log100 (9)     Log 1:100 contrast
        log316 (10)    Log 1:316 contrast
        xvycc (11)     IEC 61966-2-4
        srgb (13)      IEC 61966-2-1
        2020_10 (14)   V = a * Lc0.45 - ( a - 1 ) for 1 >= Lc >= b
                       V = 4.500 * Lc for b > Lc >= 0
                       Rec. ITU-R BT.2020
                       (functionally the same as the values 1 (709),
                       6 (601) and 15 (2020_12))
        2020_12 (15)   V = a * Lc0.45 - ( a - 1 ) for 1 >= Lc >= b
                       V = 4.500 * Lc for b > Lc >= 0
                       Rec. ITU-R BT.2020
                       (functionally the same as the values 1 (709),
                       6 (601) and 14 (2020_10))
        st2084 (16)    SMPTE ST 2084
        std-b67 (18)   ARIB std-b67

      Color primaries (ITU-T H.265 Table E.3)::
      
        709 (1)        primary x y
                       green 0.300 0.600
                       blue 0.150 0.060
                       red 0.640 0.330
                       white D65 0.3127 0.3290
                       Rec. ITU-R BT.709-5
        unspec (2)     Unspecified
                       Image characteristics are unknown or are determined by the
                       application.
        470m (4)       ITU-R Rec. BT.470-6 System M
        470bg (5)      ITU-R Rec. BT.470-6 System B, G (historical)
        170m (6)       primary x y
                       green 0.310 0.595
                       blue 0.155 0.070
                       red 0.630 0.340
                       white D65 0.3127 0.3290
                       SMPTE 170M (2004)
                       (functionally the same as the value 7 (240m))
        240m (7)       primary x y
                       green 0.310 0.595
                       blue 0.155 0.070
                       red 0.630 0.340
                       white D65 0.3127 0.3290
                       SMPTE 240M (1999)
                       (functionally the same as the value 6 (170m))
        film (8)
        2020 (9)       primary x y
                       green 0.170 0.797
                       blue 0.131 0.046
                       red 0.708 0.292
                       white D65 0.3127 0.3290
                       Rec. ITU-R BT.2020
        st428 (10)     Commonly known as xyz
        xyz (10)       Alias for st428
        st431-2 (11)   DCI-P3 with traditional white point
        st432-1 (12)   DCI-P3
        jedec-p22 (22) E.B.U. STANDARD FOR CHROMATICITY TOLERANCES FOR STUDIO MONITORS (3213-E)
                       Also known as JEDEC P22

      Pixel range (ITU-T H.265 Eq E-4 to E-15)::
      
        limited (0) Studio (TV) legal range, 16-235 in 8 bits.
                    Y = Clip1Y( Round( ( 1 << ( BitDepthY - 8 ) ) *
                                              ( 219 * E'Y + 16 ) ) )
                    Cb = Clip1C( Round( ( 1 << ( BitDepthC - 8 ) ) *
                                               ( 224 * E'PB + 128 ) ) )
                    Cr = Clip1C( Round( ( 1 << ( BitDepthC - 8 ) ) *
                                               ( 224 * E'PR + 128 ) ) )

                    R = Clip1Y( ( 1 << ( BitDepthY - 8 ) ) *
                                       ( 219 * E'R + 16 ) )
                    G = Clip1Y( ( 1 << ( BitDepthY - 8 ) ) *
                                       ( 219 * E'G + 16 ) )
                    B = Clip1Y( ( 1 << ( BitDepthY - 8 ) ) *
                                       ( 219 * E'B + 16 ) )
        full (1)    Full (PC) dynamic range, 0-255 in 8 bits.
                    Y = Clip1Y( Round( ( ( 1 << BitDepthY ) - 1 ) * E'Y ) )
                    Cb = Clip1C( Round( ( ( 1 << BitDepthC ) - 1 ) * E'PB +
                                          ( 1 << ( BitDepthC - 1 ) ) ) )
                    Cr = Clip1C( Round( ( ( 1 << BitDepthC ) - 1 ) * E'PR +
                                          ( 1 << ( BitDepthC - 1 ) ) ) )

                    R = Clip1Y( ( ( 1 << BitDepthY ) - 1 ) * E'R )
                    G = Clip1Y( ( ( 1 << BitDepthY ) - 1 ) * E'G )
                    B = Clip1Y( ( ( 1 << BitDepthY ) - 1 ) * E'B )




