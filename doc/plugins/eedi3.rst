.. _eedi3:

EEDI3
=====

eedi3 is a very slow edge directed interpolation filter.

eedi3 works by finding the best non-decreasing (non-crossing) warping between
two lines by minimizing a cost functional.
The cost is based on neighborhood similarity (favor connecting regions that look
similar), the vertical difference created by the interpolated values (favor
small differences), the interpolation directions (favor short connections vs
long), and the change in interpolation direction from pixel to pixel (favor
small changes).


.. function:: eedi3(clip clip, int field[, bint dh=0, int[] planes=[0, 1, 2], float alpha=0.2, float beta=0.25, float gamma=20, int nrad=2, int mdis=20, bint hp=0, bint ucubic=1, bint cost3=1, int vcheck=2, float vthresh0=32, float vthresh1=64, float vthresh2=4, clip sclip])
   :module: eedi3

   Parameters:
      clip
         Clip to be processed. The bit depth must be 8 bits per sample.

      field
         Selects the mode of operation and which field will be kept.
         All modes will use the field order specified in the source
         frames and will only fall back to the specified order if not
         present.

         0 - same rate, keep bottom field

         1 - same rate, keep top field

         2 - double rate, start with bottom field

         3 - double rate, start with top field

         In case of double rate output, the frame rate is doubled and
         the frame durations are halved.

      dh
         Doubles the height of the input. If field=0, the input is copied to the
         odd lines of the output. If field=1, the input is copied to the even
         lines of the output. The missing lines are interpolated.

         Default: false.

      planes
         Controls which planes will be processed.

         Default: all.

      alpha

      beta

      gamma
         These trade off line/edge connection vs artifacts created.
         alpha and beta must be in the range [0,1], and the sum alpha+beta must
         be in the range [0,1]. alpha is the weight given to connecting similar
         neighborhoods. The larger it is the more lines/edges should be
         connected. beta is the weight given to vertical difference created by
         the interpolation. The larger beta is the less edges/lines will be
         connected (at 1.0 you get no edge directedness at all). The remaining
         weight (1.0-alpha-beta) is given to interpolation direction (large
         directions (away from vertical) cost more), so the more weight you have
         here the more shorter connections will be favored. Finally, gamma
         penalizes changes in interpolation direction: the larger gamma is the
         smoother the interpolation field between two lines. gamma's range is
         [0,inf].

         If lines aren't getting connected then increase alpha and maybe
         decrease beta/gamma. Go the other way if you are getting unwanted
         artifacts.

         Defaults: 0.2, 0.25, 20.

      nrad
         nrad sets the radius used for computing neighborhood similarity.
         Valid range is [0,3]. Larger nrad will be slower.

         Default: 2.

      mdis
         mdis sets the maximum connection radius. Valid range is [1,40].
         If mdis=20, then when interpolating pixel (50,10) (x,y), the farthest
         connections allowed would be between (30,9)/(70,11) and (70,9)/(30,11).
         Larger mdis will allow connecting lines of smaller slope, but also
         increases the chance of artifacts. Larger mdis will be slower.

         Default: 20.

      hp
         If true, half pel steps will be used (slower).
         Otherwise, full pel steps will be used.

         Default: false.

      ucubic
         If true, cubic 4 point interpolation will be used (slower).
         Otherwise, 2 point linear interpolation will be used.

         Default: true.

      cost3
         If true, 3 neighborhood cost function will be used to define similarity
         (slower). Otherwise, 1 neighborhood cost function will be used.

         Default: true.

      vcheck

      vthresh0

      vthresh1

      vthresh2
         If vcheck is greater than 0, then the resulting interpolation is
         checked for reliability/consistency.

            0 - no reliability check

            1 - weak reliability check

            2 - med reliability check

            3 - strong reliability check

         Assume we interpolated pixel 'fh' below using dir=4 (i.e. averaging
         pixels bl and cd)::

            aa ab ac ad ae af ag ah ai aj ak al am an ao ap
                                 eh          el
            ba bb bc bd be bf bg bh bi bj bk bl bm bn bo bp
                     fd          fh          fl
            ca cb cc cd ce cf cg ch ci cj ck cl cm cn co cp
                     gd          gh
            da db dc dd de df dg dh di dj dk dl dm dn do dp

         When checking pixel 'fh' the following is computed::

            d0 = abs((el+fd)/2 - bh)
            d1 = abs((fl+gd)/2 - ch)

            q2 = abs(bh-fh)+abs(ch-fh)
            q3 = abs(el-bl)+abs(fl-bl)
            q4 = abs(fd-cd)+abs(gd-cd)

            d2 = abs(q2-q3)
            d3 = abs(q2-q4)

            mdiff0 = vcheck == 1 ? min(d0,d1) : vcheck == 2 ? ((d0+d1+1)>>1) : max(d0,d1)
            mdiff1 = vcheck == 1 ? min(d2,d3) : vcheck == 2 ? ((d2+d3+1)>>1) : max(d2,d3)

            a0 = mdiff0/vthresh0;
            a1 = mdiff1/vthresh1;
            a2 = max((vthresh2-abs(dir))/vthresh2,0.0f)

            a = min(max(max(a0,a1),a2),1.0f)

            final_value = (1.0-a)*fh + a*cint


         If sclip is supplied, cint is the corresponding value from sclip. If sclip isn't supplied,
         then vertical cubic interpolation is used to create it.

      sclip
         Another clip from which to take cint. (What does this actually do?)


Most of this document was copied from "EEDI3 - Readme.txt", written by
Kevin Stone (aka tritical).
