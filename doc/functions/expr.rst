Expr
====

.. function:: Expr(clip[] clips, string[] expr[, int format])
   :module: std

   Expr evaluates an expression per pixel for up to 3 input *clips*.
   The expression, *expr*, is written using reverse polish notation and can be
   specified for each plane individually.
   The expression given for the previous plane is used if the *expr* array
   contains fewer expressions than the input clip has planes.
   In practice this means that a single expression will be applied to all planes
   by default.

   Specifying an empty string as the expression enables a fast plane copy from
   the first specified clip, when possible. If it is not possible due to the
   output *format* being incompatible the plane contents will be undefined.

   Since the expression is evaluated at runtime there are a few pitfalls. In
   order to keep speed up the input ranges are not normalized to the usual
   floating point ranges. Instead they are left as is, meaning that an 8 bit
   clip will have values in the 0-255 range and a 10 bit clip will have values
   in the 0-1023 range.
   Note that floating point clips are even more difficult, as most channels are
   stored in the 0-1 range with the exception of U, V, Co and Cg planes, which
   are in the -0.5-0.5 range.
   If you mix clips with different input formats this must be taken into
   consideration.

   By default the output *format* is the same as the first input clip's format.
   You can override it by setting *format*. The only restriction is that the
   output *format* must have the same subsampling as the input *clips* and be
   8 or 16 bit integer or 32 bit float.

   Logical operators are also a bit special, since everything is done in
   floating point arithmetic.
   All values greater than 0 are considered true for the purpose of comparisons.
   Logical operators return 0.0 for false and 1.0 for true in their operations.
   Since the expression is being evaluated at runtime, there are also the stack
   manipulation operators, *swap* and *dup*. The former swaps the topmost and
   second topmost values, and the latter duplicates the topmost stack value.

   Load operators::

      x y z

   The operators taking one argument are::

      exp log sqrt abs not dup

   The operators taking two arguments are::

      + - * / max min > < = >= <= and or xor swap

   The operators taking three arguments are::

      ? (ternary operator, equivalent to a ? b : c in C)

   How to average the Y planes of 3 YUV clips and pass through the UV planes
   unchanged (assuming same format)::

      std.Expr(clips=[clipa, clipb, clipc], expr=["x y + z + 3 /", "", ""])

   How to average the Y planes of 3 YUV clips and pass through the UV planes
   unchanged (different formats)::

      std.Expr(clips=[clipa16bit, clipb10bit, clipa8bit],
         expr=["x y 64 * + z 256 * + 3 /", ""])

   Setting the output format because the input format isn't allowed as output
   (note that the U and V planes will contain junk since direct copy isn't
   possible)::

      std.Expr(clips=[clipa10bit, clipb16bit, clipa8bit],
         expr=["x 64 * y + z 256 * + 3 /", ""], format=vs.YUV420P16)
