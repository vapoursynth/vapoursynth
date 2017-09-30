import unittest
import vapoursynth as vs

class CoreTestSequence(unittest.TestCase):

    def setUp(self):
        self.core = vs.get_core(threads=1)
            
    def test_expr_op1(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 2 *")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 116)

    def test_expr_op2(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=57)
        clip = self.core.std.Expr(clip, "x 2 /")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 28)

    def test_expr_op3(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 2 / 0.1 +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 29)

    def test_expr_op4(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 68)

    def test_expr_op5(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 28 -")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 30)

    def test_expr_op6(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x -1 * abs")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 58)

    def test_expr_op7(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x sqrt")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 8)

    def test_expr_op8(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x dup -")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op9(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x dup +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 116)

    def test_expr_op10(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "2 x swap /")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 29)

    def test_expr_op11(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 60 max")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 60)

    def test_expr_op12(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "40 x min")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 40)

    def test_expr_op13(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip = self.core.std.Expr(clip, "x exp")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 7)

    def test_expr_op14(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr(clip, "x exp")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 20)

    def test_expr_op15(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr(clip, "x exp")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op16(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x log")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 4)

    def test_expr_op17(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x log exp")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 58)

    def test_expr_op18(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 <")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op19(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "10 x <")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op20(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "58 x <")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op21(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 <")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op22(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "10 x >")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op23(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 >")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op24(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "58 x >")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op25(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 >")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op26(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 <=")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op27(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "10 x <=")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op28(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "58 x <=")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op29(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 <=")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op30(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "10 x >=")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op31(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 10 >=")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op32(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "58 x >=")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op33(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 >=")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op34(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr((clip1, clip2), "x y =")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op35(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=4)
        clip = self.core.std.Expr(clip, "x x 1 - =")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op36(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr((clip1, clip2), "x y and")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op37(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr((clip1, clip2), "x y or")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op38(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr((clip1, clip2), "x y xor")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)
        
    def test_expr_op39(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=1)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y and")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op40(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=1)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y or")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op41(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=1)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y xor")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op42(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y and")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op43(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y or")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op44(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2), "x y xor")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op45(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=8)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=7)
        clip = self.core.std.Expr((clip1, clip2), "x y and")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op46(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=8)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=7)
        clip = self.core.std.Expr((clip1, clip2), "x y or")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op47(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=8)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=7)
        clip = self.core.std.Expr((clip1, clip2), "x y xor")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op48(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=200)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2, clip3), "z x y ?")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 200)

    def test_expr_op49(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=200)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=1)
        clip = self.core.std.Expr((clip1, clip2, clip3), "z x y ?")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 100)

    def test_expr_op50(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=200)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=0)
        clip = self.core.std.Expr((clip1, clip2, clip3), "z not x y ?")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 100)

    def test_expr_op51(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=200)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=100)
        clip = self.core.std.Expr((clip1, clip2, clip3), "z not x y ?")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 200)

    def test_expr_op52(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x not")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 0)

    def test_expr_op53(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x not not")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)

    def test_expr_op54(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=58)
        clip = self.core.std.Expr(clip, "x 58 =")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 1)
        
    def test_expr_op55(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr(clip, "x 2 pow")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 9)
        
    def test_expr_op56(self):
        clip = self.core.std.BlankClip(format=vs.GRAY8, color=6)
        clip = self.core.std.Expr(clip, "2 x pow")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 64)

    def test_expr_op57(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "x y z swap2 * +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 23)

    def test_expr_op58(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "10 2 3 swap2 * +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 23)

    def test_expr_op59(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "x y z swap1 * +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 16)

    def test_expr_op60(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "10 2 3 swap1 * +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 16)

    def test_expr_op61(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "x dup0 dup1 dup2 y swap3 z * + + swap / +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 35)

    def test_expr_op62(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "10 dup0 dup1 dup2 2 swap3 3 * + + swap / +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 35)

    def test_expr_op63(self):
        clip1 = self.core.std.BlankClip(format=vs.GRAY8, color=10)
        clip2 = self.core.std.BlankClip(format=vs.GRAY8, color=2)
        clip3 = self.core.std.BlankClip(format=vs.GRAY8, color=3)
        clip = self.core.std.Expr((clip1, clip2, clip3), "x dup0 10 dup2 y swap3 3 * + + swap / +")
        val = clip.get_frame(0).get_read_array(0)[0,0]
        self.assertEqual(val, 35)


        
if __name__ == '__main__':
    unittest.main()
