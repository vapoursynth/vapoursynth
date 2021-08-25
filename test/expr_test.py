import math
import unittest
import vapoursynth as vs

def get_pixel_value(clip):
    frame = clip.get_frame(0)
    arr = frame.get_read_array(0)
    return arr[0,0]

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

    def helper_sincos(self, op='sin', f=lambda x: math.sin(x)):
        clip = self.core.std.BlankClip(format=vs.GRAYS, color=10, width=1025, height=1024, length=2)
        def init_frame(n, f):
            fout = f.copy()
            arr = fout.get_write_array(0)
            for i in range(len(arr)):
                row = arr[i]
                l = len(row)
                for j in range(l):
                    row[j] = (-1 if n == 0 else 1) * (i * l + j) * 1e-3
            return fout
        clip = self.core.std.ModifyFrame(clip, clip, init_frame)
        clip2 = self.core.std.Expr(clip, "x %s" % op)
        for n in range(clip2.num_frames):
            f1, f2 = map(lambda c: c.get_frame(n), [clip, clip2])
            arr1, arr2 = map(lambda f: f.get_read_array(0), [f1, f2])
            for i in range(clip.height):
                for j in range(clip.width):
                    self.assertTrue(abs(arr2[i,j] - f(arr1[i,j])) < 1e-6)
    def test_expr_sin64(self):
        self.helper_sincos('sin', lambda x: math.sin(x))
    def test_expr_cos65(self):
        self.helper_sincos('cos', lambda x: math.cos(x))

        
if __name__ == '__main__':
    unittest.main()
