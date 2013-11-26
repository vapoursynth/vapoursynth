/*
* Copyright (c) 2012 Fredrik Mellbin
*
* This file is part of VapourSynth.
*
* VapourSynth is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* VapourSynth is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with VapourSynth; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

// Some metrics calculation used is directly based on TIVTC

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "VapourSynth.h"
#include "VSHelper.h"

// Shared

#ifndef max
#define max(a,b)   (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)   (((a) < (b)) ? (a) : (b))
#endif

static int isPowerOf2(int i) {
    return i && !(i & (i - 1));
}

// VFM

typedef struct {
    VSNodeRef *node;
    VSNodeRef *clip2;
    const VSVideoInfo *vi;
    VSFrameRef *map;
    VSFrameRef *cmask;
    int64_t scthresh;
    int *cArray;
    uint8_t *tbuffer;
    int tpitchy;
    int tpitchuv;
    int order;
    int field;
    int mode;
    int chroma;
    int mchroma;
    int cthresh;
    int mi;
    int blockx;
    int blocky;
    int y0;
    int y1;
    int micmatch;
    int micout;
    int lastn;
    int64_t lastscdiff;
} VFMData;


static void copyField(VSFrameRef *dst, const VSFrameRef *src, int field, const VSAPI *vsapi) {
    const VSFormat *fi = vsapi->getFrameFormat(src);
    int plane;
    for (plane=0; plane<fi->numPlanes; plane++) {
        vs_bitblt(vsapi->getWritePtr(dst, plane)+field*vsapi->getStride(dst, plane),vsapi->getStride(dst, plane)*2,
            vsapi->getReadPtr(src, plane)+field*vsapi->getStride(src, plane),vsapi->getStride(src, plane)*2,
            vsapi->getFrameWidth(src, plane),vsapi->getFrameHeight(src,plane)/2);
    }
}

static int64_t calcAbsDiff(const VSFrameRef *f1, const VSFrameRef *f2, const VSAPI *vsapi) {
    const uint8_t *srcp1 = vsapi->getReadPtr(f1, 0);
    const uint8_t *srcp2 = vsapi->getReadPtr(f2, 0);
    int stride = vsapi->getStride(f1, 0);
    int width = vsapi->getFrameWidth(f1, 0);
    int height = vsapi->getFrameHeight(f1, 0);
    int x, y;
    int64_t acc = 0;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            acc += abs(srcp1[x] - srcp2[x]);
        srcp1 += stride;
        srcp2 += stride;
    }
    return acc;
}

// the secret is that tbuffer is an interlaced, offset subset of all the lines
static void buildABSDiffMask(const unsigned char *prvp, const unsigned char *nxtp,
    int src_pitch, int tpitch, unsigned char *tbuffer, int width, int height,
    const VSAPI *vsapi) {

    int y, x;
    for (y=0; y<height; ++y) {
        for (x=0; x<width; x++)
            tbuffer[x] = abs(prvp[x]-nxtp[x]);

        prvp += src_pitch;
        nxtp += src_pitch;
        tbuffer += tpitch;
    }
}


static int calcMI(const VSFrameRef *src, const VSAPI *vsapi,
    int *blockN, int chroma, int cthresh, VSFrameRef *cmask, int *cArray, int blockx, int blocky)
{
    int ret = 0;
    const int cthresh6 = cthresh*6;
    int plane;
    int x, y, u, v;
    for (plane=0; plane < (chroma ? 3 : 1); plane++) {
        const unsigned char *srcp = vsapi->getReadPtr(src, plane);
        const int src_pitch = vsapi->getStride(src, plane);
        const int Width = vsapi->getFrameWidth(src, plane);
        const int Height = vsapi->getFrameHeight(src, plane);
        unsigned char *cmkp = vsapi->getWritePtr(cmask, plane);
        const int cmk_pitch = vsapi->getStride(cmask, plane);
        if (cthresh < 0) {
            memset(cmkp,255,Height*cmk_pitch);
            continue;
        }
        memset(cmkp,0,Height*cmk_pitch);
        for (x=0; x<Width; ++x) {
            const int sFirst = srcp[x] - srcp[x + src_pitch];
            if (sFirst > cthresh || sFirst < -cthresh) {
                if (abs(srcp[x + 2*src_pitch]+(srcp[x]*4)+srcp[x + 2*src_pitch]-6*srcp[x + src_pitch]) > cthresh6)
                    cmkp[x] = 0xFF;
            }
        }
        srcp += src_pitch;
        cmkp += cmk_pitch;
        for (x=0; x<Width; ++x) {
            const int sFirst = srcp[x] - srcp[x - src_pitch];
            const int sSecond = srcp[x] - srcp[x + src_pitch];
            if ((sFirst > cthresh && sSecond > cthresh) || (sFirst < -cthresh && sSecond < -cthresh)) {
                if (abs(srcp[x + 2*src_pitch]+(srcp[x]*4)+srcp[x + 2*src_pitch]-(3*(srcp[x - src_pitch]+srcp[x + src_pitch]))) > cthresh6)
                    cmkp[x] = 0xFF;
            }
        }
        srcp += src_pitch;
        cmkp += cmk_pitch;

        for (y=2; y<Height-2; ++y) {
            for (x=0; x<Width; ++x) {
                const int sFirst = srcp[x] - srcp[x - src_pitch];
                const int sSecond = srcp[x] - srcp[x + src_pitch];
                if ((sFirst > cthresh && sSecond > cthresh) || (sFirst < -cthresh && sSecond < -cthresh)) {
                    if (abs(srcp[x - 2*src_pitch]+(srcp[x]*4)+srcp[x + 2*src_pitch]-(3*(srcp[x - src_pitch]+srcp[x + src_pitch]))) > cthresh6)
                        cmkp[x] = 0xFF;
                }
            }
            srcp += src_pitch;
            cmkp += cmk_pitch;
        }

        for (x=0; x<Width; ++x) {
            const int sFirst = srcp[x] - srcp[x - src_pitch];
            const int sSecond = srcp[x] - srcp[x + src_pitch];
            if ((sFirst > cthresh && sSecond > cthresh) || (sFirst < -cthresh && sSecond < -cthresh)) {
                if (abs(srcp[x - 2*src_pitch]+(srcp[x]*4)+srcp[x - 2*src_pitch]-(3*(srcp[x - src_pitch]+srcp[x + src_pitch]))) > cthresh6)
                    cmkp[x] = 0xFF;
            }
        }
        srcp += src_pitch;
        cmkp += cmk_pitch;
        for (x=0; x<Width; ++x) {
            const int sFirst = srcp[x] - srcp[x - src_pitch];
            if (sFirst > cthresh || sFirst < -cthresh) {
                if (abs(2*srcp[x - 2*src_pitch]+(srcp[x]*4)-6*srcp[x - src_pitch]) > cthresh6)
                    cmkp[x] = 0xFF;
            }
        }
    }
    if (chroma) {
        unsigned char *cmkp = vsapi->getWritePtr(cmask, 0);
        unsigned char *cmkpU = vsapi->getWritePtr(cmask, 1);
        unsigned char *cmkpV = vsapi->getWritePtr(cmask, 2);
        const int Width = vsapi->getFrameWidth(cmask, 2);
        const int Height = vsapi->getFrameHeight(cmask, 2);
        const int cmk_pitch = vsapi->getStride(cmask, 0) * 2;
        const int cmk_pitchUV = vsapi->getStride(cmask, 2);
        unsigned char *cmkpp = cmkp - (cmk_pitch>>1);
        unsigned char *cmkpn = cmkp + (cmk_pitch>>1);
        unsigned char *cmkpnn = cmkpn + (cmk_pitch>>1);
        for (y=1; y<Height-1; ++y) {
            cmkpp += cmk_pitch;
            cmkp += cmk_pitch;
            cmkpn += cmk_pitch;
            cmkpnn += cmk_pitch;
            cmkpV += cmk_pitchUV;
            cmkpU += cmk_pitchUV;
            for (x=1; x<Width-1; ++x) {
                if ((cmkpV[x] == 0xFF && (cmkpV[x-1] == 0xFF || cmkpV[x+1] == 0xFF ||
                    cmkpV[x-1 - cmk_pitchUV] == 0xFF || cmkpV[x - cmk_pitchUV] == 0xFF || cmkpV[x+1 - cmk_pitchUV] == 0xFF ||
                    cmkpV[x-1 + cmk_pitchUV] == 0xFF || cmkpV[x + cmk_pitchUV] == 0xFF || cmkpV[x+1 + cmk_pitchUV] == 0xFF)) ||
                    (cmkpU[x] == 0xFF && (cmkpU[x-1] == 0xFF || cmkpU[x+1] == 0xFF ||
                    cmkpU[x-1 - cmk_pitchUV] == 0xFF || cmkpU[x - cmk_pitchUV] == 0xFF || cmkpU[x+1 - cmk_pitchUV] == 0xFF ||
                    cmkpU[x-1 + cmk_pitchUV] == 0xFF || cmkpU[x + cmk_pitchUV] == 0xFF || cmkpU[x+1 + cmk_pitchUV] == 0xFF)))
                {
                    ((unsigned short*)cmkp)[x] = (unsigned short) 0xFFFF;
                    ((unsigned short*)cmkpn)[x] = (unsigned short) 0xFFFF;
                    if (y&1) ((unsigned short*)cmkpp)[x] = (unsigned short) 0xFFFF;
                    else ((unsigned short*)cmkpnn)[x] = (unsigned short) 0xFFFF;
                }
            }
        }
    }
    {
    int xhalf = blockx/2;
    int yhalf = blocky/2;
    const int cmk_pitch = vsapi->getStride(cmask, 0);
    const unsigned char *cmkp = vsapi->getReadPtr(cmask, 0) + cmk_pitch;
    const unsigned char *cmkpp = cmkp - cmk_pitch;
    const unsigned char *cmkpn = cmkp + cmk_pitch;
    const int Width = vsapi->getFrameWidth(cmask, 0);
    const int Height = vsapi->getFrameHeight(cmask, 0);
    const int xblocks = ((Width+xhalf)/blockx) + 1;
    const int xblocks4 = xblocks<<2;
    const int yblocks = ((Height+yhalf)/blocky) + 1;
    const int arraysize = (xblocks*yblocks)<<2;
    int Heighta = (Height/(blocky/2))*(blocky/2);
    const int Widtha = (Width/(blockx/2))*(blockx/2);
    if (Heighta == Height)
        Heighta = Height-yhalf;
    memset(&cArray[0],0,arraysize*sizeof(int));
    for (y=1; y<yhalf; ++y) {
        const int temp1 = (y/blocky)*xblocks4;
        const int temp2 = ((y+yhalf)/blocky)*xblocks4;
        for (x=0; x<Width; ++x) {
            if (cmkpp[x] == 0xFF && cmkp[x] == 0xFF && cmkpn[x] == 0xFF) {
                const int box1 = (x/blockx)*4;
                const int box2 = ((x+xhalf)/blockx)*4;
                ++cArray[temp1+box1+0];
                ++cArray[temp1+box2+1];
                ++cArray[temp2+box1+2];
                ++cArray[temp2+box2+3];
            }
        }
        cmkpp += cmk_pitch;
        cmkp += cmk_pitch;
        cmkpn += cmk_pitch;
    }
    for (y=yhalf; y<Heighta; y+=yhalf) {
        const int temp1 = (y/blocky)*xblocks4;
        const int temp2 = ((y+yhalf)/blocky)*xblocks4;

        for (x=0; x<Widtha; x+=xhalf) {
            const unsigned char *cmkppT = cmkpp;
            const unsigned char *cmkpT = cmkp;
            const unsigned char *cmkpnT = cmkpn;
            int sum = 0;
            for (u=0; u<yhalf; ++u) {
                for (v=0; v<xhalf; ++v) {
                    if (cmkppT[x+v] == 0xFF && cmkpT[x+v] == 0xFF &&
                        cmkpnT[x+v] == 0xFF) ++sum;
                }
                cmkppT += cmk_pitch;
                cmkpT += cmk_pitch;
                cmkpnT += cmk_pitch;
            }
            if (sum) {
                const int box1 = (x/blockx)*4;
                const int box2 = ((x+xhalf)/blockx)*4;
                cArray[temp1+box1+0] += sum;
                cArray[temp1+box2+1] += sum;
                cArray[temp2+box1+2] += sum;
                cArray[temp2+box2+3] += sum;
            }
        }

        for (x=Widtha; x<Width; ++x) {
            const unsigned char *cmkppT = cmkpp;
            const unsigned char *cmkpT = cmkp;
            const unsigned char *cmkpnT = cmkpn;
            int sum = 0;
            for (u=0; u<yhalf; ++u) {
                if (cmkppT[x] == 0xFF && cmkpT[x] == 0xFF &&
                    cmkpnT[x] == 0xFF) ++sum;
                cmkppT += cmk_pitch;
                cmkpT += cmk_pitch;
                cmkpnT += cmk_pitch;
            }
            if (sum) {
                const int box1 = (x/blockx)*4;
                const int box2 = ((x+xhalf)/blockx)*4;
                cArray[temp1+box1+0] += sum;
                cArray[temp1+box2+1] += sum;
                cArray[temp2+box1+2] += sum;
                cArray[temp2+box2+3] += sum;
            }
        }
        cmkpp += cmk_pitch*yhalf;
        cmkp += cmk_pitch*yhalf;
        cmkpn += cmk_pitch*yhalf;
    }
    for (y=Heighta; y<Height-1; ++y) {
        const int temp1 = (y/blocky)*xblocks4;
        const int temp2 = ((y+yhalf)/blocky)*xblocks4;
        for (x=0; x<Width; ++x) {
            if (cmkpp[x] == 0xFF && cmkp[x] == 0xFF && cmkpn[x] == 0xFF) {
                const int box1 = (x/blockx)*4;
                const int box2 = ((x+xhalf)/blockx)*4;
                ++cArray[temp1+box1+0];
                ++cArray[temp1+box2+1];
                ++cArray[temp2+box1+2];
                ++cArray[temp2+box2+3];
            }
        }
        cmkpp += cmk_pitch;
        cmkp += cmk_pitch;
        cmkpn += cmk_pitch;
    }
    for (x=0; x<arraysize; ++x) {
        if (cArray[x] > ret) {
            ret = cArray[x];
            if (blockN)
                *blockN = x;
        }
    }
    }
    return ret;
}


// build a map over which pixels differ a lot/a little
static void buildDiffMap(const unsigned char *prvp, const unsigned char *nxtp,
    unsigned char *dstp,int src_pitch, int dst_pitch, int Height,
    int Width, int tpitch, unsigned char *tbuffer, const VSAPI *vsapi)
{
    const unsigned char *dp = tbuffer+tpitch;
    int x, y, u, diff, count;

    buildABSDiffMask(prvp-src_pitch, nxtp-src_pitch, src_pitch,
        tpitch, tbuffer, Width, Height>>1, vsapi);

    for (y=2; y<Height-2; y+=2) {
        for (x=1; x<Width-1; ++x) {
            diff = dp[x];
            if (diff > 3) {
                for (count=0,u=x-1; u<x+2 && count<2; ++u) {
                    if (dp[u-tpitch] > 3) ++count;
                    if (dp[u] > 3) ++count;
                    if (dp[u+tpitch] > 3) ++count;
                }
                if (count > 1) {
                    ++dstp[x];
                    if (diff > 19) {
                        int upper = 0, lower = 0;
                        for (count=0, u=x-1; u<x+2 && count<6; ++u) {
                            if (dp[u-tpitch] > 19) { ++count; upper = 1; }
                            if (dp[u] > 19) ++count;
                            if (dp[u+tpitch] > 19) { ++count; lower = 1; }
                        }
                        if (count > 3) {
                            if (!upper || !lower) {
                                int upper2 = 0, lower2 = 0;
                                for (u=max(x-4,0); u<min(x+5,Width); ++u)
                                {
                                    if (y != 2 && dp[u-2*tpitch] > 19)
                                        upper2 = 1;
                                    if (dp[u-tpitch] > 19)
                                        upper = 1;
                                    if (dp[u+tpitch] > 19)
                                        lower = 1;
                                    if (y != Height-4 && dp[u+2*tpitch] > 19)
                                        lower2 = 1;
                                }
                                if ((upper && (lower || upper2)) ||
                                    (lower && (upper || lower2)))
                                    dstp[x] += 2;
                                else if (count > 5)
                                    dstp[x] += 4;
                            }
                            else dstp[x] += 2;
                        }
                    }
                }
            }
        }
        dp += tpitch;
        dstp += dst_pitch;
    }
}

static int compareFieldsSlow(const VSFrameRef *prv, const VSFrameRef *src, const VSFrameRef *nxt, VSFrameRef *map, int match1,
    int match2, int mchroma, int field, int y0, int y1, uint8_t *tbuffer, int tpitchy, int tpitchuv, const VSAPI *vsapi)
{
    int plane, ret;
    const unsigned char *prvp = 0, *srcp = 0, *nxtp = 0;
    const unsigned char *curpf = 0, *curf = 0, *curnf = 0;
    const unsigned char *prvpf = 0, *prvnf = 0, *nxtpf = 0, *nxtnf = 0;
    unsigned char *mapp;
    int src_stride, Width, Height;
    int curf_pitch, stopx, map_pitch;
    int x, y, temp1, temp2, startx, y0a, y1a, tp;
    int stop = mchroma ? 3 : 1;
    unsigned long accumPc = 0, accumNc = 0, accumPm = 0;
    unsigned long accumNm = 0, accumPml = 0, accumNml = 0;
    int norm1, norm2, mtn1, mtn2;
    float c1, c2, mr;

    for (plane=0; plane<stop; ++plane) {
        mapp = vsapi->getWritePtr(map, plane);
        map_pitch = vsapi->getStride(map, plane);
        prvp = vsapi->getReadPtr(prv, plane);
        srcp = vsapi->getReadPtr(src, plane);
        src_stride = vsapi->getStride(src, plane);
        Width = vsapi->getFrameWidth(src, plane);
        Height = vsapi->getFrameHeight(src, plane);
        nxtp = vsapi->getReadPtr(nxt, plane);
        memset(mapp,0,Height*map_pitch);
        startx = (plane == 0 ? 8 : 4);
        stopx = Width - startx;
        curf_pitch = src_stride<<1;
        if (plane == 0) {
            y0a = y0;
            y1a = y1;
            tp = tpitchy;
        } else {
            y0a = y0>>1;
            y1a = y1>>1;
            tp = tpitchuv;
        }
        if (match1 < 3) {
            curf = srcp + ((3-field)*src_stride);
            mapp = mapp + ((field == 1 ? 1 : 2)*map_pitch);
        }
        if (match1 == 0) {
            prvpf = prvp + ((field == 1 ? 1 : 2)*src_stride);
        } else if (match1 == 1) {
            prvpf = srcp + ((field == 1 ? 1 : 2)*src_stride);
        } else if (match1 == 2) {
            prvpf = nxtp + ((field == 1 ? 1 : 2)*src_stride);
        } else if (match1 == 3) {
            curf = srcp + ((2+field)*src_stride);
            prvpf = prvp + ((field == 1 ? 2 : 1)*src_stride);
            mapp = mapp + ((field == 1 ? 2 : 1)*map_pitch);
        } else if (match1 == 4) {
            curf = srcp + ((2+field)*src_stride);
            prvpf = nxtp + ((field == 1 ? 2 : 1)*src_stride);
            mapp = mapp + ((field == 1 ? 2 : 1)*map_pitch);
        }
        if (match2 == 0) {
            nxtpf = prvp + ((field == 1 ? 1 : 2)*src_stride);
        } else if (match2 == 1) {
            nxtpf = srcp + ((field == 1 ? 1 : 2)*src_stride);
        } else if (match2 == 2) {
            nxtpf = nxtp + ((field == 1 ? 1 : 2)*src_stride);
        } else if (match2 == 3) {
            nxtpf = prvp + ((field == 1 ? 2 : 1)*src_stride);
        } else if (match2 == 4)
        {
            nxtpf = nxtp + ((field == 1 ? 2 : 1)*src_stride);
        }
        prvnf = prvpf + curf_pitch;
        curpf = curf - curf_pitch;
        curnf = curf + curf_pitch;
        nxtnf = nxtpf + curf_pitch;
        map_pitch <<= 1;
        if ((match1 >= 3 && field == 1) || (match1 < 3 && field != 1))
            buildDiffMap(prvpf,nxtpf,mapp,curf_pitch,map_pitch,Height,Width,tp,tbuffer,vsapi);
        else
            buildDiffMap(prvnf,nxtnf,mapp + map_pitch,curf_pitch,map_pitch,Height,Width,tp,tbuffer,vsapi);

        for (y=2; y<Height-2; y+=2) {
            if (y0a == y1a || y < y0a || y > y1a) {
                for (x=startx; x<stopx; x++) {
                    if (mapp[x] > 0 || mapp[x + map_pitch] > 0) {
                        temp1 = curpf[x]+(curf[x]<<2)+curnf[x];
                        temp2 = abs(3*(prvpf[x]+prvnf[x])-temp1);
                        if (temp2 > 23 && ((mapp[x]&1) || (mapp[x + map_pitch]&1)))
                            accumPc += temp2;
                        if (temp2 > 42) {
                            if ((mapp[x]&2) || (mapp[x + map_pitch]&2))
                                accumPm += temp2;
                            if ((mapp[x]&4) || (mapp[x + map_pitch]&4))
                                accumPml += temp2;
                        }
                        temp2 = abs(3*(nxtpf[x]+nxtnf[x])-temp1);
                        if (temp2 > 23 && ((mapp[x]&1) || (mapp[x + map_pitch]&1)))
                            accumNc += temp2;
                        if (temp2 > 42) {
                            if ((mapp[x]&2) || (mapp[x + map_pitch]&2))
                                accumNm += temp2;
                            if ((mapp[x]&4) || (mapp[x + map_pitch]&4))
                                accumNml += temp2;
                        }
                    }
                }
            }
            prvpf += curf_pitch;
            prvnf += curf_pitch;
            curpf += curf_pitch;
            curf += curf_pitch;
            curnf += curf_pitch;
            nxtpf += curf_pitch;
            nxtnf += curf_pitch;
            mapp += map_pitch;
        }
    }
    if (accumPm < 500 && accumNm < 500 && (accumPml >= 500 || accumNml >= 500) &&
        max(accumPml,accumNml) > 3*min(accumPml,accumNml))
    {
        accumPm = accumPml;
        accumNm = accumNml;
    }
    norm1 = (int)((accumPc / 6.0f) + 0.5f);
    norm2 = (int)((accumNc / 6.0f) + 0.5f);
    mtn1 = (int)((accumPm / 6.0f) + 0.5f);
    mtn2 = (int)((accumNm / 6.0f) + 0.5f);
    c1 = ((float)max(norm1,norm2))/((float)max(min(norm1,norm2),1));
    c2 = ((float)max(mtn1,mtn2))/((float)max(min(mtn1,mtn2),1));
    mr = ((float)max(mtn1,mtn2))/((float)max(max(norm1,norm2),1));
    if (((mtn1 >= 500  || mtn2 >= 500)  && (mtn1*2 < mtn2*1 || mtn2*2 < mtn1*1)) ||
        ((mtn1 >= 1000 || mtn2 >= 1000) && (mtn1*3 < mtn2*2 || mtn2*3 < mtn1*2)) ||
        ((mtn1 >= 2000 || mtn2 >= 2000) && (mtn1*5 < mtn2*4 || mtn2*5 < mtn1*4)) ||
        ((mtn1 >= 4000 || mtn2 >= 4000) && c2 > c1))
    {
        if (mtn1 > mtn2)
            ret = match2;
        else
            ret = match1;
    } else if (mr > 0.005 && max(mtn1,mtn2) > 150 && (mtn1*2 < mtn2*1 || mtn2*2 < mtn1*1)) {
        if (mtn1 > mtn2)
            ret = match2;
        else
            ret = match1;
    } else {
        if (norm1 > norm2)
            ret = match2;
        else
            ret = match1;
    }
    return ret;
}


static const VSFrameRef *createWeaveFrame(const VSFrameRef *prv, const VSFrameRef *src,
    const VSFrameRef *nxt, const VSAPI *vsapi, VSCore *core, int match, int field) {
    if (match == 1) {
        return vsapi->cloneFrameRef(src);
    } else {
        VSFrameRef *dst = vsapi->newVideoFrame(vsapi->getFrameFormat(src), vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), src, core);

        if (match == 0) {
            copyField(dst, src, 1-field, vsapi);
            copyField(dst, prv, field, vsapi);
        } else if (match == 2) {
            copyField(dst, src, 1-field, vsapi);
            copyField(dst, nxt, field, vsapi);
        } else if (match == 3) {
            copyField(dst, src, field, vsapi);
            copyField(dst, prv, 1-field, vsapi);
        } else if (match == 4) {
            copyField(dst, src, field, vsapi);
            copyField(dst, nxt, 1-field, vsapi);
        }

        return dst;
    }
}


static int checkmm(int m1, int m2, int *m1mic, int *m2mic, int *blockN, int MI, int field, int chroma, int cthresh, const VSFrameRef **genFrames,
    const VSFrameRef *prv, const VSFrameRef *src, const VSFrameRef *nxt, VSFrameRef *cmask, int *cArray, int blockx, int blocky, const VSAPI *vsapi, VSCore *core) {
    if (*m1mic < 0) {
        if (!genFrames[m1])
            genFrames[m1] = createWeaveFrame(prv, src, nxt, vsapi, core, m1, field);
        *m1mic = calcMI(genFrames[m1], vsapi, blockN, chroma, cthresh, cmask, cArray, blockx, blocky);
    }

    if (*m2mic < 0) {
        if (!genFrames[m2])
            genFrames[m2] = createWeaveFrame(prv, src, nxt, vsapi, core, m2, field);
        *m2mic = calcMI(genFrames[m2], vsapi, blockN, chroma, cthresh, cmask, cArray, blockx, blocky);
    }

    if (((*m2mic)*3 < *m1mic || ((*m2mic)*2 < *m1mic && *m1mic > MI)) &&
        abs(*m2mic-*m1mic) >= 30 && *m2mic < MI)
        return m2;
    else
        return m1;
}

static void VS_CC vfmInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    VFMData *vfm = (VFMData *)*instanceData;
    vsapi->setVideoInfo(vfm->vi, 1, node);
}

typedef enum {
    mP = 0,
    mC = 1,
    mN = 2,
    mB = 3,
    mU = 4
} FMP;

static const VSFrameRef *VS_CC vfmGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    VFMData *vfm = (VFMData *)*instanceData;
    n = min(vfm->vi->numFrames - 1, n);
    if (activationReason == arInitial) {
        if (n > 0) {
            vsapi->requestFrameFilter(n-1, vfm->node, frameCtx);
            if (vfm->clip2)
                vsapi->requestFrameFilter(n-1, vfm->clip2, frameCtx);
        }
        vsapi->requestFrameFilter(n, vfm->node, frameCtx);
        if (vfm->clip2)
            vsapi->requestFrameFilter(n, vfm->clip2, frameCtx);
        if (n < vfm->vi->numFrames - 1) {
            vsapi->requestFrameFilter(n+1, vfm->node, frameCtx);
            if (vfm->clip2)
                vsapi->requestFrameFilter(n+1, vfm->clip2, frameCtx);
        }
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef *prv = vsapi->getFrameFilter(n > 0 ? n-1 : 0, vfm->node, frameCtx);
        const VSFrameRef *src = vsapi->getFrameFilter(n, vfm->node, frameCtx);
        const VSFrameRef *nxt = vsapi->getFrameFilter(n < vfm->vi->numFrames - 1 ? n+1 : vfm->vi->numFrames - 1, vfm->node, frameCtx);
        int mics[] = { -1,-1,-1,-1,-1 };

        // selected based on field^order
        const int fxo0m[] = { 0, 1, 2, 3, 4 };
        const int fxo1m[] = { 2, 1, 0, 4, 3};
        const int *fxo = vfm->field^vfm->order ? fxo1m : fxo0m;
        int match;
        int i;
        const VSFrameRef *genFrames[] =  { NULL, NULL, NULL, NULL, NULL };
        int blockN;
        const VSFrameRef *dst1;
        VSFrameRef *dst2;
        VSMap *m;
        int sc = 0;

        // check if it's a scenechange so micmatch can be used
        // only relevant for mm mode 1
        if (vfm->micmatch == 1) {
            if (vfm->lastn == n - 1) {
                if (vfm->lastscdiff > vfm->scthresh)
                    sc = 1;
            } else if (calcAbsDiff(prv, src, vsapi) > vfm->scthresh) {
                sc = 1;
            }

            if (!sc) {
                vfm->lastn = n;
                vfm->lastscdiff = calcAbsDiff(src, nxt, vsapi);
                sc = vfm->lastscdiff > vfm->scthresh;
            }
        }

        // p/c selection
        match = compareFieldsSlow(prv, src, nxt, vfm->map, fxo[mC], fxo[mP], vfm->mchroma, vfm->field, vfm->y0, vfm->y1, vfm->tbuffer, vfm->tpitchy, vfm->tpitchuv, vsapi);
        // the mode has 3-way p/c/n matches
        if (vfm->mode >= 4)
            match = compareFieldsSlow(prv, src, nxt, vfm->map, match, fxo[mN], vfm->mchroma, vfm->field, vfm->y0, vfm->y1, vfm->tbuffer, vfm->tpitchy, vfm->tpitchuv, vsapi);

        genFrames[mC] = vsapi->cloneFrameRef(src);

        // calculate all values for mic output, checkmm calculates and prepares it for the two matches if not already done
        if (vfm->micout) {
            checkmm(0, 1, &mics[0], &mics[1], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
            checkmm(2, 3, &mics[2], &mics[3], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
            checkmm(4, 0, &mics[4], &mics[0], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
        }

        // check the micmatches to see if one of the options are better
        // here come the huge ass mode tables

        if (vfm->micmatch == 2 || (sc && vfm->micmatch == 1)) {
            // here comes the conditional hell to try to approximate mode 0-5 in tfm
            if (vfm->mode == 0) {
                // maybe not completely appropriate but go back and see if the discarded match is less sucky
                match = checkmm(match, match == fxo[mP] ? fxo[mC] : fxo[mP], &mics[match], &mics[match == fxo[mP] ? fxo[mC] : fxo[mP]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
            } else if (vfm->mode == 1) {
                match = checkmm(match, fxo[mN], &mics[match], &mics[fxo[mN]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
            } else if (vfm->mode == 2) {
                match = checkmm(match, fxo[mU], &mics[match], &mics[fxo[mU]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
            } else if (vfm->mode == 3) {
                match = checkmm(match, fxo[mN], &mics[match], &mics[fxo[mN]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
                match = checkmm(match, fxo[mU], &mics[match], &mics[fxo[mU]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
                match = checkmm(match, fxo[mB], &mics[match], &mics[fxo[mB]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
            } else if (vfm->mode == 4) {
                // degenerate check because I'm lazy
                match = checkmm(match, match == fxo[mP] ? fxo[mC] : fxo[mP], &mics[match], &mics[match == fxo[mP] ? fxo[mC] : fxo[mP]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky,vsapi, core);
            } else if (vfm->mode == 5) {
                match = checkmm(match, fxo[mU], &mics[match], &mics[fxo[mU]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
                match = checkmm(match, fxo[mB], &mics[match], &mics[fxo[mB]], &blockN, vfm->mi, vfm->field, vfm->chroma, vfm->cthresh, genFrames, prv, src, nxt, vfm->cmask, &vfm->cArray[0], vfm->blockx, vfm->blocky, vsapi, core);
            }
        }

        if (vfm->clip2) {
            const VSFrameRef *prv2 = vsapi->getFrameFilter(n > 0 ? n-1 : 0, vfm->clip2, frameCtx);
            const VSFrameRef *src2 = vsapi->getFrameFilter(n, vfm->clip2, frameCtx);
            const VSFrameRef *nxt2 = vsapi->getFrameFilter(n < vfm->vi->numFrames - 1 ? n+1 : vfm->vi->numFrames - 1, vfm->clip2, frameCtx);
            dst1 = createWeaveFrame(prv2, src2, nxt2, vsapi, core, match, vfm->field);
            vsapi->freeFrame(prv2);
            vsapi->freeFrame(src2);
            vsapi->freeFrame(nxt2);
        } else {
            if (!genFrames[match])
                genFrames[match] = createWeaveFrame(prv, src, nxt, vsapi, core, match, vfm->field);
            dst1 = vsapi->cloneFrameRef(genFrames[match]);
        }

        vsapi->freeFrame(prv);
        vsapi->freeFrame(src);
        vsapi->freeFrame(nxt);

        for (i = 0; i < 5; i++)
            vsapi->freeFrame(genFrames[i]);

        dst2 = vsapi->copyFrame(dst1, core);
        vsapi->freeFrame(dst1);
        m = vsapi->getFramePropsRW(dst2);
        for (i = 0; i < 5; i++)
            vsapi->propSetInt(m, "VFMMics", mics[fxo[i]], paAppend);
        vsapi->propSetInt(m, "_Combed", mics[match] >= vfm->mi, paReplace);
        vsapi->propSetInt(m, "VFMMatch", fxo[match], paReplace);
        vsapi->propSetInt(m, "VFMSceneChange", sc, paReplace);
        return dst2;
    }
    return NULL;
}

static void VS_CC vfmFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    VFMData *vfm = (VFMData *)instanceData;
    vsapi->freeFrame(vfm->cmask);
    vsapi->freeFrame(vfm->map);
    free(vfm);
}

static void VS_CC createVFM(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    int err;
    VFMData vfm;
    VFMData *vfmd ;
    const VSVideoInfo *vi;
    double scthresh;

    vfm.order = !!vsapi->propGetInt(in, "order", 0, 0);
    vfm.field = int64ToIntS(vsapi->propGetInt(in, "field", 0, &err));
    if (err || vfm.field != !!vfm.field)
        vfm.field = vfm.order;

    vfm.mode = int64ToIntS(vsapi->propGetInt(in, "mode", 0, &err));
    if (err)
        vfm.mode = 1;
    vfm.mchroma = !!vsapi->propGetInt(in, "mchroma", 0, &err);
    if (err)
        vfm.mchroma = 1;
    vfm.cthresh = int64ToIntS(vsapi->propGetInt(in, "cthresh", 0, &err));
    if (err)
        vfm.cthresh = 9;
    vfm.mi = int64ToIntS(vsapi->propGetInt(in, "mi", 0, &err));
    if (err)
        vfm.mi = 80;
    vfm.chroma = !!vsapi->propGetInt(in, "chroma", 0, &err);
    if (err)
        vfm.chroma = 1;
    vfm.blockx = int64ToIntS(vsapi->propGetInt(in, "blockx", 0, &err));
    if (err)
        vfm.blockx = 16;
    vfm.blocky = int64ToIntS(vsapi->propGetInt(in, "blocky", 0, &err));
    if (err)
        vfm.blocky = 16;
    vfm.y0 = int64ToIntS(vsapi->propGetInt(in, "y0", 0, &err));
    if (err)
        vfm.y0 = 16;
    vfm.y1 = int64ToIntS(vsapi->propGetInt(in, "y1", 0, &err));
    if (err)
        vfm.y1 = 16;
    scthresh = vsapi->propGetFloat(in, "scthresh", 0, &err);
    if (err)
        scthresh = 12.0;
    vfm.micmatch = int64ToIntS(vsapi->propGetInt(in, "micmatch", 0, &err));
    if (err)
        vfm.micmatch = 1;
    vfm.micout = !!vsapi->propGetInt(in, "micout", 0, &err);

    if (vfm.mode < 0 || vfm.mode > 5) {
        vsapi->setError(out, "VFM: Invalid mode specified, only 0-5 allowed");
        return;
    }

    if (vfm.blockx < 4 || vfm.blockx > 512 || !isPowerOf2(vfm.blockx) || vfm.blocky < 4 || vfm.blocky > 512 || !isPowerOf2(vfm.blocky)) {
        vsapi->setError(out, "VFM: invalid blocksize, must be between 4 and 512 and be a power of 2");
        return;
    }

    if (vfm.mi < 0 || vfm.mi > vfm.blockx * vfm.blocky) {
        vsapi->setError(out, "VFM: Invalid mi threshold specified");
        return;
    }

    if (scthresh < 0 || scthresh > 100) {
        vsapi->setError(out, "VFM: Invalid scthresh specified");
        return;
    }

    if (vfm.cthresh < -1 || vfm.cthresh > 255) {
        vsapi->setError(out, "VFM: invalid cthresh specified");
        return;
    }

    if (vfm.micmatch < 0 || vfm.micmatch > 2) {
        vsapi->setError(out, "VFM: invalid micmatch mode specified");
        return;
    }

    vfm.node = vsapi->propGetNode(in, "clip", 0, 0);
    vfm.clip2 = vsapi->propGetNode(in, "clip2", 0, &err);
    vfm.vi = vsapi->getVideoInfo(vfm.clip2 ? vfm.clip2 : vfm.node);
    vi = vsapi->getVideoInfo(vfm.node);

    if (!isConstantFormat(vi) || !vi->numFrames || vi->format->id != pfYUV420P8) {
        vsapi->setError(out, "VFM: input clip must be constant format YUV420P8");
        vsapi->freeNode(vfm.node);
        vsapi->freeNode(vfm.clip2);
        return;
    }

    if (vi->numFrames != vfm.vi->numFrames || !isConstantFormat(vi)) {
        vsapi->setError(out, "VFM: the number of frames must be the same in both input clips and clip2 must be constant format");
        vsapi->freeNode(vfm.node);
        vsapi->freeNode(vfm.clip2);
        return;
    }

    vfm.scthresh =  (int64_t)((vi->width * vi->height * 255.0 * scthresh) / 100.0);

    vfm.map = vsapi->newVideoFrame(vi->format, vi->width, vi->height, NULL, core);
    vfm.cmask = vsapi->newVideoFrame(vi->format, vi->width, vi->height, NULL, core);

    vfm.tpitchy = (vi->width&15) ? vi->width+16-(vi->width&15) : vi->width;
    vfm.tpitchuv = ((vi->width>>1)&15) ? (vi->width>>1)+16-((vi->width>>1)&15) : (vi->width>>1);

    vfm.tbuffer = (uint8_t *)malloc((vi->height>>1)*vfm.tpitchy*sizeof(uint8_t));
    vfm.cArray = (int *)malloc((((vi->width+vfm.blockx/2)/vfm.blockx)+1)*(((vi->height+vfm.blocky/2)/vfm.blocky)+1)*4*sizeof(int));

    vfmd = (VFMData *)malloc(sizeof(vfm));
    *vfmd = vfm;
    vsapi->createFilter(in, out, "VFM", vfmInit, vfmGetFrame, vfmFree, fmParallelRequests, 0, vfmd, core);
}

// VDecimate

typedef struct {
    int64_t maxbdiff;
    int64_t totdiff;
} VDInfo;

typedef struct {
    VSNodeRef *node;
    VSNodeRef *clip2;
    VSVideoInfo vi;
    int cycle;
    int chroma;
    int tail;
    int inputNumFrames;
    int64_t dupthresh;
    int64_t scthresh;
    int blockx;
    int blocky;
    int nxblocks;
    int nyblocks;
    int bdiffsize;
    int64_t *bdiffs;
    VDInfo *vmi;
    const char *ovrfile;
    char *ovr;
} VDecimateData;

static int64_t calcMetric(const VSFrameRef *f1, const VSFrameRef *f2, int64_t *totdiff, VDecimateData *vdm, const VSAPI *vsapi) {
    int64_t *bdiffs = vdm->bdiffs;
    int plane;
    int x, y, xl;
    int i, j;
    int numplanes = vdm->chroma ? 3 : 1;
    int64_t maxdiff = -1;
    memset(bdiffs, 0, vdm->bdiffsize * sizeof(int64_t));
    for (plane = 0; plane < numplanes; plane++) {
        int stride = vsapi->getStride(f1, plane);
        const uint8_t *f1p = vsapi->getReadPtr(f1, plane);
        const uint8_t *f2p = vsapi->getReadPtr(f2, plane);
        const VSFormat *fi = vsapi->getFrameFormat(f1);

        int width = vsapi->getFrameWidth(f1, plane);
        int height = vsapi->getFrameHeight(f1, plane);
        int hblockx = vdm->blockx/2;
        int hblocky = vdm->blocky/2;
        int nxblocks = vdm->nxblocks;
        // adjust for subsampling
        if (plane > 0) {
            hblockx /= 1 << fi->subSamplingW;
            hblocky /= 1 << fi->subSamplingH;
        }

        for (y = 0; y < height; y++) {
            int ydest = y / hblocky;
            int xdest = 0;
            // some slight code duplication to not put an if statement for 8/16 bit processing in the inner loop
            if (fi->bitsPerSample == 8) {
                for (x = 0; x < width; x+= hblockx) {
                    int acc = 0;
                    int m = min(width, x + hblockx);
                    for (xl = x; xl < m; xl++)
                        acc += abs(f1p[xl] - f2p[xl]);
                    bdiffs[ydest * nxblocks + xdest] += acc;
                    xdest++;
                }
            } else {
                for (x = 0; x < width; x+= hblockx) {
                    int acc = 0;
                    int m = min(width, x + hblockx);
                    for (xl = x; xl < m; xl++)
                        acc += abs(((const uint16_t *)f1p)[xl] - ((const uint16_t *)f2p)[xl]);
                    bdiffs[ydest * nxblocks + xdest] += acc;
                    xdest++;
                }
            }
            f1p += stride;
            f2p += stride;
        }
    }

    for (i = 0; i  < vdm->nyblocks - 1; i++) {
        for (j = 0; j  < vdm->nxblocks - 1; j++) {
            int64_t tmp = bdiffs[i * vdm->nxblocks + j] + bdiffs[i * vdm->nxblocks + j + 1] + bdiffs[(i + 1) * vdm->nxblocks + j] + bdiffs[(i + 1) * vdm->nxblocks + j + 1];
            if (tmp > maxdiff)
                maxdiff = tmp;
        }
    }

    *totdiff = 0;
    for (i = 0; i  < vdm->bdiffsize; i++)
        *totdiff += bdiffs[i];
    return maxdiff;
}

static int vdecimateLoadOVR(const char *ovrfile, char **overrides, int cycle, int numFrames, char err[80]) {
    int line = 0;
    char buf[80];
    char* pos;
    char *ovr;
#ifdef _WIN32
    FILE* moo = NULL;
    int len, ret;
    wchar_t *ovrfile_wc;
    len = MultiByteToWideChar(CP_UTF8, 0, ovrfile, -1, NULL, 0);
    ovrfile_wc = malloc(len * sizeof(wchar_t));
    if (ovrfile_wc) {
        ret = MultiByteToWideChar(CP_UTF8, 0, ovrfile, -1, ovrfile_wc, len);
        if (ret == len)
            moo = _wfopen(ovrfile_wc, L"rb");
        free(ovrfile_wc);
    }
#else
    FILE* moo = fopen(ovrfile, "r");
#endif
    if (!moo) {
        sprintf(err, "VDecimate: can't open ovr file");
        return 1;
    }

    ovr = malloc(numFrames);
    memset(ovr, 0, numFrames);

    memset(buf, 0, sizeof(buf));
    while (fgets(buf, 80, moo)) {
        int frame = -1;
        int frame_start = -1;
        int frame_end = -1;

        char drop_char = 0;
        char drop_pattern[26] = { 0 }; // 25 (maximum cycle size allowed in vdecimate) + \0
        int drop_pos = -1;

        line++;
        pos = buf + strspn(buf, " \t\r\n");

        if (pos[0] == '#' || pos[0] == 0) {
            continue;
        } else if (sscanf(pos, " %u, %u %25s", &frame_start, &frame_end, drop_pattern) == 3) {
            char *tmp = strchr(drop_pattern, '-');
            if (tmp) {
                drop_pos = tmp - drop_pattern;
            }
        } else if (sscanf(pos, " %u %c", &frame, &drop_char) == 2) {
            ;
        } else {
            sprintf(err, "VDecimate: sscanf failed to parse override at line %d", line);
            fclose(moo);
            free(ovr);
            return 1;
        }

        if (frame >= 0 && frame < numFrames && drop_char == '-') {
            ovr[frame] = 1;
        } else if (frame_start >= 0 && frame_start < numFrames &&
                   frame_end >= 0 && frame_end < numFrames &&
                   frame_start < frame_end &&
                   strlen(drop_pattern) == (size_t)cycle &&
                   drop_pos > -1) {
            int i;
            for (i = frame_start + drop_pos; i <= frame_end; i += cycle) {
                ovr[i] = 1;
            }
        } else {
            sprintf(err, "VDecimate: Bad override at line %d in ovr", line);
            fclose(moo);
            free(ovr);
            return 1;
        }

        while (buf[78] != 0 && buf[78] != '\n' && fgets(buf, 80, moo)) {
            ; // slurp the rest of a long line
        }
    }

    fclose(moo);
    *overrides = ovr;
    return 0;
}

static void VS_CC vdecimateInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    VDecimateData *vdm = (VDecimateData *)*instanceData;
    vsapi->setVideoInfo(&vdm->vi, 1, node);

    if (vdm->ovrfile) {
        char err[80];

        vdm->ovr = NULL;
        if (vdecimateLoadOVR(vdm->ovrfile, &vdm->ovr, vdm->cycle, vdm->inputNumFrames, err)) {
            free(vdm->bdiffs);
            free(vdm->vmi);
            vsapi->freeNode(vdm->node);
            free(vdm);
            vsapi->setError(out, err);
            return;
        }
    }
}

static const VSFrameRef *VS_CC vdecimateGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    VDecimateData *vdm = (VDecimateData *)*instanceData;
    int hasall = 1;
    int i;

    if (activationReason == arInitial) {
        int prevreqd = 0;
        int cyclestart = (n / (vdm->cycle - 1)) * vdm->cycle;
        int cycleend = cyclestart + vdm->cycle;
        if (cycleend > vdm->inputNumFrames)
            cycleend = vdm->inputNumFrames;


        for (i = cyclestart; i < cycleend; i++) {
            if (vdm->vmi[i].maxbdiff < 0) {
                hasall = 0;
                if (!prevreqd && i-1 >= 0)
                    vsapi->requestFrameFilter(i - 1, vdm->node, frameCtx);
                vsapi->requestFrameFilter(i, vdm->node, frameCtx);
                prevreqd = 1;
            } else {
                prevreqd = 0;
            }
        }
        *frameData = (void *)-1;
    }

    if (activationReason == arAllFramesReady || (hasall && activationReason == arInitial)) {
        int fin, fcut;
        intptr_t fout;
        int cyclestart, cycleend, lowest;
        int scpos = -1;
        int duppos = -1;
        int override = -1;

        intptr_t rframe = (intptr_t)*frameData;
        if(rframe >= 0) {
            if (vdm->clip2)
                return vsapi->getFrameFilter(rframe, vdm->clip2, frameCtx);
            else
                return vsapi->getFrameFilter(rframe, vdm->node, frameCtx);
        }

        // calculate all the needed metrics
        cyclestart = (n / (vdm->cycle - 1)) * vdm->cycle;
        cycleend = cyclestart + vdm->cycle;
        if (cycleend > vdm->inputNumFrames)
            cycleend = vdm->inputNumFrames;

        if (vdm->ovrfile) {
            for (i = cyclestart; i < cycleend; i++) {
                if (vdm->ovr[i]) {
                    override = i;
                    break;
                }
            }
        }

        if (override == -1) {
            for (i = cyclestart; i < cycleend; i++) {
                if (vdm->vmi[i].maxbdiff < 0) {
                    const VSFrameRef *prv = vsapi->getFrameFilter(max(i - 1, 0), vdm->node, frameCtx);
                    const VSFrameRef *cur = vsapi->getFrameFilter(i, vdm->node, frameCtx);
                    vdm->vmi[i].maxbdiff = calcMetric(prv, cur, &vdm->vmi[i].totdiff, vdm, vsapi);
                    vsapi->freeFrame(prv);
                    vsapi->freeFrame(cur);
                }
            }

            // make a decision
            // precalculate the position of the lowest dup metric frame
            // the last sc and the lowest dup, if any

            lowest = cyclestart;
            for (i = cyclestart; i < cycleend; i++) {
                if (vdm->vmi[i].totdiff > vdm->scthresh)
                    scpos = i;
                if (vdm->vmi[i].maxbdiff < vdm->vmi[lowest].maxbdiff)
                    lowest = i;
            }

            if (vdm->vmi[lowest].maxbdiff < vdm->dupthresh)
                duppos = lowest;

            // if there is no scenechange simply drop the frame with lowest difference
            // if there is a scenechange see if any frame qualifies as a duplicate and drop it
            // otherwise drop the first frame right after the sc to keep the motion smooth

            fin = n % (vdm->cycle - 1);
            // no dups so drop the frame right after the sc to keep things smooth
            if (scpos >= 0 && duppos < 0) {
                fcut = scpos % vdm->cycle;
            } else {
                fcut = lowest % vdm->cycle;
            }

        } else {
            fin = n % (vdm->cycle - 1);
            fcut = override % vdm->cycle;
        }

        fout = cyclestart + (fcut > fin ? fin : (fin + 1));

        if (vdm->clip2) {
            vsapi->requestFrameFilter(fout, vdm->clip2, frameCtx);
        } else {
            const VSFrameRef *ftry = vsapi->getFrameFilter(fout, vdm->node, frameCtx);
            if (ftry)
                return ftry;
            vsapi->requestFrameFilter(fout, vdm->node, frameCtx);
        }
        *frameData = (void *)fout;

    }
    return NULL;
}

static void VS_CC vdecimateFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    VDecimateData *vdm = (VDecimateData *)instanceData;
    if (vdm->ovrfile) {
        free(vdm->ovr);
    }
    free(vdm->bdiffs);
    free(vdm->vmi);
    free(vdm);
}

static void VS_CC createVDecimate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    VDecimateData vdm;
    VDecimateData *d;
    const VSVideoInfo *vi;
    int i, err, max_value;
    double dupthresh, scthresh;

    vdm.cycle = int64ToIntS(vsapi->propGetInt(in, "cycle", 0, &err));
    if (err)
        vdm.cycle = 5;
    vdm.blockx = int64ToIntS(vsapi->propGetInt(in, "blockx", 0, &err));
    if (err)
        vdm.blockx = 32;
    vdm.blocky = int64ToIntS(vsapi->propGetInt(in, "blocky", 0, &err));
    if (err)
        vdm.blocky = 32;
    dupthresh = vsapi->propGetFloat(in, "dupthresh", 0, &err);
    if (err)
        dupthresh = 1.1;
    scthresh = vsapi->propGetFloat(in, "scthresh", 0, &err);
    if (err)
        scthresh = 15.0;

    if (vdm.cycle < 2 || vdm.cycle > 25) {
        vsapi->setError(out, "VDecimate: Invalid cycle size specified");
        return;
    }

    if (vdm.blockx < 4 || vdm.blockx > 512 || !isPowerOf2(vdm.blockx) || vdm.blocky < 4 || vdm.blocky > 512 || !isPowerOf2(vdm.blocky)) {
        vsapi->setError(out, "VDecimate: invalid blocksize, must be between 4 and 512 and be a power of 2");
        return;
    }

    if (dupthresh < 0 || dupthresh > 100) {
        vsapi->setError(out, "VDecimate: invalid dupthresh specified");
        return;
    }

    if (scthresh < 0 || scthresh > 100) {
        vsapi->setError(out, "VDecimate: invalid scthresh specified");
        return;
    }

    vdm.node = vsapi->propGetNode(in, "clip", 0, 0);
    vdm.clip2 = vsapi->propGetNode(in, "clip2", 0, &err);
    vdm.vi = *vsapi->getVideoInfo(vdm.clip2 ? vdm.clip2 : vdm.node);

    vi = vsapi->getVideoInfo(vdm.node);
    if (!isConstantFormat(vi) || !vi->numFrames || vi->format->bitsPerSample > 16 || vi->format->sampleType != stInteger) {
        vsapi->setError(out, "VDecimate: input clip must be constant format, with 8..16 bits per sample");
        vsapi->freeNode(vdm.node);
        vsapi->freeNode(vdm.clip2);
        return;
    }

    if (vi->numFrames != vdm.vi.numFrames) {
        vsapi->setError(out, "VDecimate: the number of frames must be the same in both input clips");
        vsapi->freeNode(vdm.node);
        vsapi->freeNode(vdm.clip2);
        return;
    }

    vdm.chroma = !!vsapi->propGetInt(in, "chroma", 0, &err);
    if (err)
        vdm.chroma = vi->format->colorFamily != cmGray;
    else {
        if (vdm.chroma && vi->format->colorFamily == cmGray) {
            vsapi->setError(out, "VDecimate: it makes no sense to enable chroma when the input clip is grayscale");
            vsapi->freeNode(vdm.node);
            vsapi->freeNode(vdm.clip2);
            return;
        } else if (!vdm.chroma && vi->format->colorFamily == cmRGB) {
            vsapi->setError(out, "VDecimate: it makes no sense to disable chroma when the input clip is RGB");
            vsapi->freeNode(vdm.node);
            vsapi->freeNode(vdm.clip2);
            return;
        }
    }

    vdm.ovrfile = vsapi->propGetData(in, "ovr", 0, &err);


    max_value = (1 << vi->format->bitsPerSample) - 1;
    // Casting max_value to int64_t to avoid losing the high 32 bits of the result
    vdm.scthresh = (int64_t)(((int64_t)max_value * vi->width * vi->height * scthresh)/100);
    vdm.dupthresh = (int64_t)((max_value * vdm.blockx * vdm.blocky * dupthresh)/100);

    vdm.nxblocks = (vdm.vi.width + vdm.blockx/2 - 1)/(vdm.blockx/2);
    vdm.nyblocks = (vdm.vi.height + vdm.blocky/2 - 1)/(vdm.blocky/2);
    vdm.bdiffsize = vdm.nxblocks * vdm.nyblocks;
    vdm.bdiffs = (int64_t *)malloc(vdm.bdiffsize * sizeof(int64_t));
    vdm.vmi = (VDInfo *)malloc(vdm.vi.numFrames * sizeof(VDInfo));
    for (i = 0; i < vdm.vi.numFrames; i++) {
        vdm.vmi[i].maxbdiff = -1;
        vdm.vmi[i].totdiff = -1;
    }

    vdm.inputNumFrames = vdm.vi.numFrames;
    vdm.tail = vdm.vi.numFrames % vdm.cycle;
    vdm.vi.numFrames /= vdm.cycle;
    vdm.vi.numFrames *= vdm.cycle - 1;
    vdm.vi.numFrames += vdm.tail;
    muldivRational(&vdm.vi.fpsNum, &vdm.vi.fpsDen, vdm.cycle-1, vdm.cycle);

    d = (VDecimateData *)malloc(sizeof(vdm));
    *d = vdm;
    vsapi->createFilter(in, out, "VDecimate", vdecimateInit, vdecimateGetFrame, vdecimateFree, fmUnordered, 0, d, core);
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("org.ivtc.v", "vivtc", "VFM", VAPOURSYNTH_API_VERSION, 1, plugin);
    // add ovr support
    registerFunc("VFM", "clip:clip;order:int;field:int:opt;mode:int:opt;" \
        "mchroma:int:opt;cthresh:int:opt;mi:int:opt;" \
        "chroma:int:opt;blockx:int:opt;blocky:int:opt;y0:int:opt;y1:int:opt;" \
        "scthresh:float:opt;micmatch:int:opt;micout:int:opt;clip2:clip:opt;", createVFM, NULL, plugin);
    // add metrics output
    // adjust frame durations too/cfr it all?
    registerFunc("VDecimate", "clip:clip;cycle:int:opt;" \
        "chroma:int:opt;dupthresh:float:opt;scthresh:float:opt;" \
        "blockx:int:opt;blocky:int:opt;clip2:clip:opt;ovr:data:opt;", createVDecimate, NULL, plugin);
}
