Avisynth functions and their VapourSynth equivalents
====================================================

+------------------------+---------------------+----------------------------------------------------------------------+
| Avisynth               | VapourSynth         | Notes                                                                |
+========================+=====================+======================================================================+
| AviSource              | avisource.AVISource |                                                                      |
|                        |                     |                                                                      |
+------------------------+---------------------+----------------------------------------------------------------------+
| DirectShowSource       | none                | DirectShowSource will at least have a special compatible avisynth    |
|                        |                     | version created. Or VapourSynth will get varargs support.            |
+------------------------+---------------------+----------------------------------------------------------------------+
| ImageReader/ImageWriter| none                | Planned, contributions welcome                                       |
+------------------------+---------------------+----------------------------------------------------------------------+
| Import                 | none                | See the documentation for Python's import command                    |
+------------------------+---------------------+----------------------------------------------------------------------+
| ConvertTo*             | resize.Bicubic(     | This also determines the resizer used for chroma resampling,         |
|                        | format=vs.YUV444P8) | if needed                                                            |
+------------------------+---------------------+----------------------------------------------------------------------+
| ColorYUV               | std.Lut             | Do the adjustment yourself with a lut                                |
+------------------------+---------------------+----------------------------------------------------------------------+
| GreyScale              | std.ShufflePlanes   | ShufflePlanes(clips=inclip, planes=0, format=vs.GRAY)                |
|                        |                     | Extracts the first plane. Y for YUV, R for RGB, planes=1/2 = U/V G/B |
+------------------------+---------------------+----------------------------------------------------------------------+
| Invert                 | std.Lut             | Do the adjustment yourself with a lut                                |
+------------------------+---------------------+----------------------------------------------------------------------+
| Limiter                | std.Lut             | Do the adjustment yourself with a lut                                |
+------------------------+---------------------+----------------------------------------------------------------------+
| MergeRGB               | std.ShufflePlanes   | ShufflePlanes(clips=[R,G,B], planes=[0, 0, 0], format=vs.RGB)        |
+------------------------+---------------------+----------------------------------------------------------------------+
| MergeChroma/MergeLuma  | std.ShufflePlanes   | ShufflePlanes(clips=[Yclip,UVclip], planes=[0, 1, 2], format=vs.YUV) |
+------------------------+---------------------+----------------------------------------------------------------------+
| RGBAdjust              | std.Lut             | Do the adjustment yourself with a lut                                |
+------------------------+---------------------+----------------------------------------------------------------------+
| ShowAlpha/ShowRed/     | std.ShufflePlanes   | ShufflePlanes(clips=inclip, planes=0, format=vs.GRAY)                |
| ShowGreen/ShowBlue     |                     | Extracts the first plane. Y for YUV, R for RGB, planes=1/2 = U/V G/B |
+------------------------+---------------------+----------------------------------------------------------------------+
| SwapUV                 | std.ShufflePlanes   | ShufflePlanes(clips=inclip, planes=[0, 2, 1], format=vs.YUV)         |
+------------------------+---------------------+----------------------------------------------------------------------+
| Tweak                  | std.Lut             | Do the adjustment yourself with a lut                                |
+------------------------+---------------------+----------------------------------------------------------------------+
| UToY/VToY/*            | std.ShufflePlanes   | See GreyScale and the other examples                                 |
+------------------------+---------------------+----------------------------------------------------------------------+
| ColorKeyMask           | std.Lut             | Not perfect but probably good enough for a clever person             |
+------------------------+---------------------+----------------------------------------------------------------------+
| Layer                  | std.Lut2            | Do the adjustment yourself with a lut                                |
+------------------------+---------------------+----------------------------------------------------------------------+
| Overlay                | std.Lut2            | Do the adjustment yourself with a lut. Missing offset and other stuff|
+------------------------+---------------------+----------------------------------------------------------------------+
| Subtract               | std.Lut2            | Do the adjustment yourself with a lut                                |
+------------------------+---------------------+----------------------------------------------------------------------+
| AddBorders             | std.AddBorders      |                                                                      |
+------------------------+---------------------+----------------------------------------------------------------------+
| Crop                   | std.CropAbs/CropRel |                                                                      |
+------------------------+---------------------+----------------------------------------------------------------------+
| FlipHorizontal/        | std.FlipHorizontal/ |                                                                      |
| FlipVertical           | std.FlipVertical    |                                                                      |
+------------------------+---------------------+----------------------------------------------------------------------+
| Letterbox              | std.CropAbs+        |                                                                      |
|                        | std.AddBorders      |                                                                      |
+------------------------+---------------------+----------------------------------------------------------------------+
| ReduceBy2              | resize.*            | Too specialized to ever be included in the core                      |
+------------------------+---------------------+----------------------------------------------------------------------+
| Resize (all kinds)     | resize.*            | Chroma placement/shift/range conversion not supported in swscale     |
+------------------------+---------------------+----------------------------------------------------------------------+
| Turn180                | std.Turn180         |                                                                      |
+------------------------+---------------------+----------------------------------------------------------------------+
| TurnRight/TurnLeft     | std.Transpose       | Add std.FlipHorizontal/std.FlipVertical to create a true turn        |
+------------------------+---------------------+----------------------------------------------------------------------+
| ConditionalFilter      | std.SelectClip      | Can also substitute many of the other conditionals                   |
+------------------------+---------------------+----------------------------------------------------------------------+
| Animate/ApplyRange     | none                | Will never have an equivalent                                        |
+------------------------+---------------------+----------------------------------------------------------------------+
| BlankClip              | std.BlankClip       |                                                                      |
+------------------------+---------------------+----------------------------------------------------------------------+
| StackHorizontal/       | std.StackHorizontal/|                                                                      |
| StackVertical          | std.StackVertical   |                                                                      |
+------------------------+---------------------+----------------------------------------------------------------------+
