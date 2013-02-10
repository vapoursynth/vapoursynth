import vapoursynth as vs
import gc
#import leakmodule
core = vs.Core()
clip1 = core.std.BlankClip(format=vs.YUV420P8, color=[0, 128, 128], width=5000)
#clip1 = None
clip2 = core.std.BlankClip(format=vs.YUV420P8, color=[255, 128, 128], width=5000)
#clip2 = None
#a = 's'*60000*300
gc.set_debug(gc.DEBUG_LEAK | gc.DEBUG_STATS)
def altf(n, f):
    return n % 2

clip2 = core.std.SelectClip([clip1, clip2], [clip1, clip2], altf)
#clip2 = core.std.SelectClip([clip1, clip2], [clip1, clip2], leakmodule.leaky_func)
#clip2 = core.std.SelectClip([clip1, clip2], clip1, lambda n, f: n % 2)
clip1 = core.std.StackVertical([clip1, clip2])
clip2 = core.std.StackVertical([clip1, clip2])
clip1 = core.std.StackVertical([clip1, clip2])
clip2 = core.std.StackVertical([clip1, clip2])
last = clip2

#def leaky_func():
#    pass

#class leaky_class:
#    def __call__(self):
#        pass

print(str(gc.garbage))
