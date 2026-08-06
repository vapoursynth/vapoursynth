import math
import unittest

import vapoursynth as vs


def get_pixel_value(clip):
    frame = clip.get_frame(0)
    arr = frame[0]
    return arr[0, 0]


def get_row(clip, row=0):
    arr = clip.get_frame(0)[0]
    return [arr[row, x] for x in range(clip.width)]


class CoreTestSequence(unittest.TestCase):
    def setUp(self):
        self.core = vs.core
        self.core.num_threads = 1

    def test_expr_op1(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 2 *")
        self.assertEqual(get_pixel_value(clip), 116)

    def test_expr_op2(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=57)
        clip = self.core.std.Expr(clip, "x 2 /")
        self.assertEqual(get_pixel_value(clip), 28)

    def test_expr_op3(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 2 / 0.1 +")
        self.assertEqual(get_pixel_value(clip), 29)

    def test_expr_op4(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 +")
        self.assertEqual(get_pixel_value(clip), 68)

    def test_expr_op5(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 28 -")
        self.assertEqual(get_pixel_value(clip), 30)

    def test_expr_op6(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x -1 * abs")
        self.assertEqual(get_pixel_value(clip), 58)

    def test_expr_op7(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x sqrt")
        self.assertEqual(get_pixel_value(clip), 8)

    def test_expr_op8(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x dup -")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op9(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x dup +")
        self.assertEqual(get_pixel_value(clip), 116)

    def test_expr_op10(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "2 x swap /")
        self.assertEqual(get_pixel_value(clip), 29)

    def test_expr_op11(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 60 max")
        self.assertEqual(get_pixel_value(clip), 60)

    def test_expr_op12(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "40 x min")
        self.assertEqual(get_pixel_value(clip), 40)

    def test_expr_op13(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip = self.core.std.Expr(clip, "x exp")
        self.assertEqual(get_pixel_value(clip), 7)

    def test_expr_op14(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr(clip, "x exp")
        self.assertEqual(get_pixel_value(clip), 20)

    def test_expr_op15(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr(clip, "x exp")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op16(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x log")
        self.assertEqual(get_pixel_value(clip), 4)

    def test_expr_op17(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x log exp")
        self.assertEqual(get_pixel_value(clip), 58)

    def test_expr_op18(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 <")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op19(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "10 x <")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op20(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "58 x <")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op21(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 <")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op22(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "10 x >")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op23(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 >")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op24(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "58 x >")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op25(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 >")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op26(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 <=")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op27(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "10 x <=")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op28(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "58 x <=")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op29(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 <=")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op30(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "10 x >=")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op31(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 >=")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op32(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "58 x >=")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op33(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 >=")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op34(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr((clip1, clip2), "x y =")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op35(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=4)
        clip = self.core.std.Expr(clip, "x x 1 - =")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op36(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr((clip1, clip2), "x y and")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op37(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr((clip1, clip2), "x y or")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op38(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr((clip1, clip2), "x y xor")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op39(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=1)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y and")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op40(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=1)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y or")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op41(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=1)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y xor")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op42(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y and")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op43(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y or")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op44(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y xor")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op45(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=8)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=7)
        clip = self.core.std.Expr((clip1, clip2), "x y and")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op46(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=8)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=7)
        clip = self.core.std.Expr((clip1, clip2), "x y or")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op47(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=8)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=7)
        clip = self.core.std.Expr((clip1, clip2), "x y xor")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op48(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=200)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2, clip3), "z x y ?")
        self.assertEqual(get_pixel_value(clip), 200)

    def test_expr_op49(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=200)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=1)
        clip = self.core.std.Expr((clip1, clip2, clip3), "z x y ?")
        self.assertEqual(get_pixel_value(clip), 100)

    def test_expr_op50(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=200)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2, clip3), "z not x y ?")
        self.assertEqual(get_pixel_value(clip), 100)

    def test_expr_op51(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=200)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip = self.core.std.Expr((clip1, clip2, clip3), "z not x y ?")
        self.assertEqual(get_pixel_value(clip), 200)

    def test_expr_op52(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x not")
        self.assertEqual(get_pixel_value(clip), 0)

    def test_expr_op53(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x not not")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op54(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 =")
        self.assertEqual(get_pixel_value(clip), 1)

    def test_expr_op55(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr(clip, "x 2 pow")
        self.assertEqual(get_pixel_value(clip), 9)

    def test_expr_op56(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=6)
        clip = self.core.std.Expr(clip, "2 x pow")
        self.assertEqual(get_pixel_value(clip), 64)

    def test_expr_op57(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "x y z swap2 * +")
        self.assertEqual(get_pixel_value(clip), 23)

    def test_expr_op58(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "10 2 3 swap2 * +")
        self.assertEqual(get_pixel_value(clip), 23)

    def test_expr_op59(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "x y z swap1 * +")
        self.assertEqual(get_pixel_value(clip), 16)

    def test_expr_op60(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "10 2 3 swap1 * +")
        self.assertEqual(get_pixel_value(clip), 16)

    def test_expr_op61(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "x dup0 dup1 dup2 y swap3 z * + + swap / +")
        self.assertEqual(get_pixel_value(clip), 35)

    def test_expr_op62(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "10 dup0 dup1 dup2 2 swap3 3 * + + swap / +")
        self.assertEqual(get_pixel_value(clip), 35)

    def test_expr_op63(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "x dup0 10 dup2 y swap3 3 * + + swap / +")
        self.assertEqual(get_pixel_value(clip), 35)

    def helper_sincos(self, op="sin", f=lambda x: math.sin(x)):
        clip = self.core.std.BlankClip(format=vs.GRAYS, color=10, width=1025, height=1024, length=2)

        def init_frame(n, f):
            fout = f.copy()
            arr = fout[0]
            M, N = arr.shape
            for i in range(M):
                for j in range(N):
                    arr[i, j] = (n != 0 or -1) * (i * N + j) * 1e-3
            return fout

        clip = self.core.std.ModifyFrame(clip, clip, init_frame)
        clip2 = self.core.std.Expr(clip, "x %s" % op)
        for n in range(clip2.num_frames):
            f1, f2 = map(lambda c: c.get_frame(n), [clip, clip2])
            arr1, arr2 = f1[0], f2[0]
            for i in range(clip.height):
                for j in range(clip.width):
                    self.assertTrue(abs(arr2[i, j] - f(arr1[i, j])) < 1e-6)

    def test_expr_sin64(self):
        self.helper_sincos("sin", lambda x: math.sin(x))

    def test_expr_cos65(self):
        self.helper_sincos("cos", lambda x: math.cos(x))

    # The tests above build the clip with BlankClip's default 640x480 and assert
    # a single pixel. 640 is a multiple of every vector width a backend is likely
    # to use, and [0, 0] is always in the first vector, so a backend that
    # mishandles the partial vector at the end of a row - or whose vector path
    # disagrees with its own scalar remainder path - passes all of them.

    EDGE_EXPR = (
        "x 3.5 * 1.75 + sqrt x 2.25 / 0.5 max + x x * 1000 / + "
        "x 17.5 > x 1.5 / x 2.5 * ? min"
    )

    def test_expr_row_tail66(self):
        # Evaluating the same pixels at different offsets moves each of them
        # through every position within a vector and gives each run a different
        # remainder length, so a vector/remainder disagreement shows up here.
        WIDTH = 64
        src = self.core.std.BlankClip(
            format=vs.GRAYS, width=WIDTH, height=1, length=1
        )

        def ramp(n, f):
            fout = f.copy()
            arr = fout[0]
            for x in range(WIDTH):
                arr[0, x] = (x * 7 % 251) / 3.0 + 0.25
            return fout

        src = self.core.std.ModifyFrame(src, src, ramp)

        full = get_row(self.core.std.Expr(src, self.EDGE_EXPR))
        for off in range(1, 8):
            sub = self.core.std.CropAbs(
                src, width=WIDTH - off, height=1, left=off, top=0
            )
            got = get_row(self.core.std.Expr(sub, self.EDGE_EXPR))
            self.assertEqual(
                got, full[off:], "offset %d disagrees with the unshifted result" % off
            )

    def test_expr_widths67(self):
        # Every sample format, at widths either side of the usual vector widths,
        # checking every pixel rather than only the first.
        for fmt, peak in (
            (vs.GRAY8, 255),
            (vs.GRAY10, 1023),
            (vs.GRAY16, 65535),
            (vs.GRAYH, 1),
            (vs.GRAYS, 1),
        ):
            for w in (1, 2, 3, 4, 5, 7, 8, 9, 13, 15, 16, 17, 31, 33, 719):
                a = self.core.std.BlankClip(
                    format=fmt, width=w, height=2, length=1, color=[peak]
                )
                b = self.core.std.BlankClip(
                    format=fmt, width=w, height=2, length=1, color=[0]
                )
                got = get_row(self.core.std.Expr((a, b), "x y max"))
                self.assertEqual(got, [peak] * w, "format %s width %d" % (fmt, w))


if __name__ == "__main__":
    unittest.main()
