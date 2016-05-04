import vapoursynth as vs
core = vs.get_core()

colorfamilies = (vs.GRAY, vs.YUV, vs.RGB, vs.YCOCG)
intbitdepths = (8, 9, 10, 11, 12, 13, 14, 15, 16)
floatbitdepths = (16, 32)
yuvss = (0, 1, 2)

formatids = []

for cfs in colorfamilies:
    for bps in intbitdepths:
        if cfs in (vs.YUV, vs.YCOCG):
            for wss in yuvss:
                for hss in yuvss:
                    formatids.append(core.register_format(cfs, vs.INTEGER, bps, wss, hss).id)
        else:
            formatids.append(core.register_format(cfs, vs.INTEGER, bps, 0, 0).id)

for cfs in colorfamilies:
    for bps in floatbitdepths:
        if cfs in (vs.YUV, vs.YCOCG):
            for wss in yuvss:
                for hss in yuvss:
                    formatids.append(core.register_format(cfs, vs.FLOAT, bps, wss, hss).id)
        else:
            formatids.append(core.register_format(cfs, vs.FLOAT, bps, 0, 0).id)

print(len(formatids))

cid = 0

for informat in formatids:
    cid = cid + 1
    print(cid)
    for outformat in formatids:
        clip = core.std.BlankClip(format=informat)     
        try:
            if (clip.format.color_family in (vs.YUV, vs.GRAY)):
                clip = core.resize.Bicubic(clip, format=outformat, matrix_in_s="709")
            elif (core.get_format(outformat).color_family in (vs.YUV, vs.GRAY)):
                clip = core.resize.Bicubic(clip, format=outformat, matrix_s="709")       
            else:
                clip = core.resize.Bicubic(clip, format=outformat) 
            clip.get_frame(0)
        except vs.Error as e:
            print(core.get_format(informat).name + ' ' + core.get_format(outformat).name)
            print(e)
          
