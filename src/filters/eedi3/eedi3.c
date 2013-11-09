/*
**   VapourSynth port by Fredrik Mellbin
**
**   eedi3 (enhanced edge directed interpolation 3). Works by finding the
**   best non-decreasing (non-crossing) warping between two lines according to
**   a cost functional. Doesn't really have anything to do with eedi2 aside
**   from doing edge-directed interpolation (they use different techniques).
**
**   Copyright (C) 2010 Kevin Stone
**
**   This program is free software; you can redistribute it and/or modify
**   it under the terms of the GNU General Public License as published by
**   the Free Software Foundation; either version 2 of the License, or
**   (at your option) any later version.
**
**   This program is distributed in the hope that it will be useful,
**   but WITHOUT ANY WARRANTY; without even the implied warranty of
**   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**   GNU General Public License for more details.
**
**   You should have received a copy of the GNU General Public License
**   along with this program; if not, write to the Free Software
**   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#define _POSIX_C_SOURCE 200112L
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "VapourSynth.h"
#include "VSHelper.h"


#define MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define MAX(a, b)  (((a) > (b)) ? (a) : (b))


typedef struct {
    VSNodeRef *node;
    VSVideoInfo vi;

    VSNodeRef *sclip;

    int dh, hp, ucubic, cost3;
    int planes;
    float alpha, beta, gamma,  vthresh0, vthresh1, vthresh2;
    int field, nrad, mdis, vcheck;
} eedi3Data;


static void VS_CC eedi3Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi)
{
    eedi3Data *d = (eedi3Data *) * instanceData;
    vsapi->setVideoInfo(&d->vi, 1, node);
}


void interpLineFP(const uint8_t *srcp, const int width, const int pitch,
                  const float alpha, const float beta, const float gamma, const int nrad,
                  const int mdis, float *temp, uint8_t *dstp, int *dmap, const int ucubic,
                  const int cost3)
{
    const uint8_t *src3p = srcp - 3 * pitch;
    const uint8_t *src1p = srcp - 1 * pitch;
    const uint8_t *src1n = srcp + 1 * pitch;
    const uint8_t *src3n = srcp + 3 * pitch;
    const int tpitch = mdis * 2 + 1;
    float *ccosts = temp;
    float *pcosts = ccosts + width * tpitch;
    int *pbackt = (int *)(pcosts + width * tpitch);
    int *fpath = pbackt + width * tpitch;

    int k, u, v, x;

    // calculate all connection costs
    if(!cost3) {
        for(x = 0; x < width; ++x) {
            const int umax = MIN(MIN(x, width - 1 - x), mdis);

            for(u = -umax; u <= umax; ++u) {
                int s = 0;

                for(k = -nrad; k <= nrad; ++k)
                    s +=
                        abs(src3p[x + u + k] - src1p[x - u + k]) +
                        abs(src1p[x + u + k] - src1n[x - u + k]) +
                        abs(src1n[x + u + k] - src3n[x - u + k]);

                const int ip = (src1p[x + u] + src1n[x - u] + 1) >> 1; // should use cubic if ucubic=true
                const int v = abs(src1p[x] - ip) + abs(src1n[x] - ip);
                ccosts[x * tpitch + mdis + u] = alpha * s + beta * abs(u) + (1.0f - alpha - beta) * v;
            }
        }
    } else {
        for(x = 0; x < width; ++x) {
            const int umax = MIN(MIN(x, width - 1 - x), mdis);

            for(u = -umax; u <= umax; ++u) {
                int s0 = 0, s1 = -1, s2 = -1;

                for(k = -nrad; k <= nrad; ++k)
                    s0 +=
                        abs(src3p[x + u + k] - src1p[x - u + k]) +
                        abs(src1p[x + u + k] - src1n[x - u + k]) +
                        abs(src1n[x + u + k] - src3n[x - u + k]);

                if((u >= 0 && x >= u * 2) || (u <= 0 && x < width + u * 2)) {
                    s1 = 0;

                    for(k = -nrad; k <= nrad; ++k)
                        s1 +=
                            abs(src3p[x + k] - src1p[x - u * 2 + k]) +
                            abs(src1p[x + k] - src1n[x - u * 2 + k]) +
                            abs(src1n[x + k] - src3n[x - u * 2 + k]);
                }

                if((u <= 0 && x >= u * 2) || (u >= 0 && x < width + u * 2)) {
                    s2 = 0;

                    for(k = -nrad; k <= nrad; ++k)
                        s2 +=
                            abs(src3p[x + u * 2 + k] - src1p[x + k]) +
                            abs(src1p[x + u * 2 + k] - src1n[x + k]) +
                            abs(src1n[x + u * 2 + k] - src3n[x + k]);
                }

                s1 = s1 >= 0 ? s1 : (s2 >= 0 ? s2 : s0);
                s2 = s2 >= 0 ? s2 : (s1 >= 0 ? s1 : s0);
                const int ip = (src1p[x + u] + src1n[x - u] + 1) >> 1; // should use cubic if ucubic=true
                const int v = abs(src1p[x] - ip) + abs(src1n[x] - ip);
                ccosts[x * tpitch + mdis + u] = alpha * (s0 + s1 + s2) * 0.333333f + beta * abs(u) + (1.0f - alpha - beta) * v;
            }
        }
    }

    // calculate path costs
    pcosts[mdis] = ccosts[mdis];

    for(x = 1; x < width; ++x) {
        float *tT = ccosts + x * tpitch;
        float *ppT = pcosts + (x - 1) * tpitch;
        float *pT = pcosts + x * tpitch;
        int *piT = pbackt + (x - 1) * tpitch;
        const int umax = MIN(MIN(x, width - 1 - x), mdis);

        for(u = -umax; u <= umax; ++u) {
            int idx;
            float bval = FLT_MAX;
            const int umax2 = MIN(MIN(x - 1, width - x), mdis);

            for(v = MAX(-umax2, u - 1); v <= MIN(umax2, u + 1); ++v) {
                const double y = ppT[mdis + v] + gamma * abs(u - v);
                const float ccost = (float)MIN(y, FLT_MAX * 0.9);

                if(ccost < bval) {
                    bval = ccost;
                    idx = v;
                }
            }

            const double y = bval + tT[mdis + u];

            pT[mdis + u] = (float)MIN(y, FLT_MAX * 0.9);

            piT[mdis + u] = idx;
        }
    }

    // backtrack
    fpath[width - 1] = 0;

    for(x = width - 2; x >= 0; --x)
        fpath[x] = pbackt[x * tpitch + mdis + fpath[x + 1]];

    // interpolate
    for(x = 0; x < width; ++x) {
        const int dir = fpath[x];
        dmap[x] = dir;
        const int ad = abs(dir);

        if(ucubic && x >= ad * 3 && x <= width - 1 - ad * 3)
            dstp[x] = MIN(MAX((36 * (src1p[x + dir] + src1n[x - dir]) -
                               4 * (src3p[x + dir * 3] + src3n[x - dir * 3]) + 32) >> 6, 0), 255);
        else
            dstp[x] = (src1p[x + dir] + src1n[x - dir] + 1) >> 1;
    }
}


void interpLineHP(const uint8_t *srcp, const int width, const int pitch,
                  const float alpha, const float beta, const float gamma, const int nrad,
                  const int mdis, float *temp, uint8_t *dstp, int *dmap, const int ucubic,
                  const int cost3)
{
    const uint8_t *src3p = srcp - 3 * pitch;
    const uint8_t *src1p = srcp - 1 * pitch;
    const uint8_t *src1n = srcp + 1 * pitch;
    const uint8_t *src3n = srcp + 3 * pitch;
    const int tpitch = mdis * 4 + 1;
    float *ccosts = temp;
    float *pcosts = ccosts + width * tpitch;
    int *pbackt = (int *)(pcosts + width * tpitch);
    int *fpath = pbackt + width * tpitch;
    // calculate half pel values
    uint8_t *hp3p = (uint8_t *)fpath;
    uint8_t *hp1p = hp3p + width;
    uint8_t *hp1n = hp1p + width;
    uint8_t *hp3n = hp1n + width;

    int k, u, v, x;

    for(x = 0; x < width - 1; ++x) {
        if(!ucubic || (x == 0 || x == width - 2)) {
            hp3p[x] = (src3p[x] + src3p[x + 1] + 1) >> 1;
            hp1p[x] = (src1p[x] + src1p[x + 1] + 1) >> 1;
            hp1n[x] = (src1n[x] + src1n[x + 1] + 1) >> 1;
            hp3n[x] = (src3n[x] + src3n[x + 1] + 1) >> 1;
        } else {
            hp3p[x] = MIN(MAX((36 * (src3p[x] + src3p[x + 1]) - 4 * (src3p[x - 1] + src3p[x + 2]) + 32) >> 6, 0), 255);
            hp1p[x] = MIN(MAX((36 * (src1p[x] + src1p[x + 1]) - 4 * (src1p[x - 1] + src1p[x + 2]) + 32) >> 6, 0), 255);
            hp1n[x] = MIN(MAX((36 * (src1n[x] + src1n[x + 1]) - 4 * (src1n[x - 1] + src1n[x + 2]) + 32) >> 6, 0), 255);
            hp3n[x] = MIN(MAX((36 * (src3n[x] + src3n[x + 1]) - 4 * (src3n[x - 1] + src3n[x + 2]) + 32) >> 6, 0), 255);
        }
    }

    // calculate all connection costs
    if(!cost3) {
        for(x = 0; x < width; ++x) {
            const int umax = MIN(MIN(x, width - 1 - x), mdis);

            for(u = -umax * 2; u <= umax * 2; ++u) {
                int s = 0, ip;
                const int u2 = u >> 1;

                if(!(u & 1)) {
                    for(k = -nrad; k <= nrad; ++k)
                        s +=
                            abs(src3p[x + u2 + k] - src1p[x - u2 + k]) +
                            abs(src1p[x + u2 + k] - src1n[x - u2 + k]) +
                            abs(src1n[x + u2 + k] - src3n[x - u2 + k]);

                    ip = (src1p[x + u2] + src1n[x - u2] + 1) >> 1; // should use cubic if ucubic=true
                } else {
                    for(k = -nrad; k <= nrad; ++k)
                        s +=
                            abs(hp3p[x + u2 + k] - hp1p[x - u2 - 1 + k]) +
                            abs(hp1p[x + u2 + k] - hp1n[x - u2 - 1 + k]) +
                            abs(hp1n[x + u2 + k] - hp3n[x - u2 - 1 + k]);

                    ip = (hp1p[x + u2] + hp1n[x - u2 - 1] + 1) >> 1; // should use cubic if ucubic=true
                }

                const int v = abs(src1p[x] - ip) + abs(src1n[x] - ip);

                ccosts[x * tpitch + mdis * 2 + u] = alpha * s + beta * abs(u) * 0.5f + (1.0f - alpha - beta) * v;
            }
        }
    } else {
        for(x = 0; x < width; ++x) {
            const int umax = MIN(MIN(x, width - 1 - x), mdis);

            for(u = -umax * 2; u <= umax * 2; ++u) {
                int s0 = 0, s1 = -1, s2 = -1, ip;
                const int u2 = u >> 1;

                if(!(u & 1)) {
                    for(k = -nrad; k <= nrad; ++k)
                        s0 +=
                            abs(src3p[x + u2 + k] - src1p[x - u2 + k]) +
                            abs(src1p[x + u2 + k] - src1n[x - u2 + k]) +
                            abs(src1n[x + u2 + k] - src3n[x - u2 + k]);

                    ip = (src1p[x + u2] + src1n[x - u2] + 1) >> 1; // should use cubic if ucubic=true
                } else {
                    for(k = -nrad; k <= nrad; ++k)
                        s0 +=
                            abs(hp3p[x + u2 + k] - hp1p[x - u2 - 1 + k]) +
                            abs(hp1p[x + u2 + k] - hp1n[x - u2 - 1 + k]) +
                            abs(hp1n[x + u2 + k] - hp3n[x - u2 - 1 + k]);

                    ip = (hp1p[x + u2] + hp1n[x - u2 - 1] + 1) >> 1; // should use cubic if ucubic=true
                }

                if((u >= 0 && x >= u) || (u <= 0 && x < width + u)) {
                    s1 = 0;

                    for(k = -nrad; k <= nrad; ++k)
                        s1 +=
                            abs(src3p[x + k] - src1p[x - u + k]) +
                            abs(src1p[x + k] - src1n[x - u + k]) +
                            abs(src1n[x + k] - src3n[x - u + k]);
                }

                if((u <= 0 && x >= u) || (u >= 0 && x < width + u)) {
                    s2 = 0;

                    for(k = -nrad; k <= nrad; ++k)
                        s2 +=
                            abs(src3p[x + u + k] - src1p[x + k]) +
                            abs(src1p[x + u + k] - src1n[x + k]) +
                            abs(src1n[x + u + k] - src3n[x + k]);
                }

                s1 = s1 >= 0 ? s1 : (s2 >= 0 ? s2 : s0);
                s2 = s2 >= 0 ? s2 : (s1 >= 0 ? s1 : s0);
                const int v = abs(src1p[x] - ip) + abs(src1n[x] - ip);
                ccosts[x * tpitch + mdis * 2 + u] = alpha * (s0 + s1 + s2) * 0.333333f + beta * abs(u) * 0.5f + (1.0f - alpha - beta) * v;
            }
        }
    }

    // calculate path costs
    pcosts[mdis * 2] = ccosts[mdis * 2];

    for(x = 1; x < width; ++x) {
        float *tT = ccosts + x * tpitch;
        float *ppT = pcosts + (x - 1) * tpitch;
        float *pT = pcosts + x * tpitch;
        int *piT = pbackt + (x - 1) * tpitch;
        const int umax = MIN(MIN(x, width - 1 - x), mdis);

        for(u = -umax * 2; u <= umax * 2; ++u) {
            int idx;
            float bval = FLT_MAX;
            const int umax2 = MIN(MIN(x - 1, width - x), mdis);

            for(v = MAX(-umax2 * 2, u - 2); v <= MIN(umax2 * 2, u + 2); ++v) {
                const double y = ppT[mdis * 2 + v] + gamma * abs(u - v) * 0.5f;
                const float ccost = (float)MIN(y, FLT_MAX * 0.9);

                if(ccost < bval) {
                    bval = ccost;
                    idx = v;
                }
            }

            const double y = bval + tT[mdis * 2 + u];

            pT[mdis * 2 + u] = (float)MIN(y, FLT_MAX * 0.9);

            piT[mdis * 2 + u] = idx;
        }
    }

    // backtrack
    fpath[width - 1] = 0;

    for(x = width - 2; x >= 0; --x)
        fpath[x] = pbackt[x * tpitch + mdis * 2 + fpath[x + 1]];

    // interpolate
    for(x = 0; x < width; ++x) {
        const int dir = fpath[x];
        dmap[x] = dir;

        if(!(dir & 1)) {
            const int d2 = dir >> 1;
            const int ad = abs(d2);

            if(ucubic && x >= ad * 3 && x <= width - 1 - ad * 3)
                dstp[x] = MIN(MAX((36 * (src1p[x + d2] + src1n[x - d2]) -
                                   4 * (src3p[x + d2 * 3] + src3n[x - d2 * 3]) + 32) >> 6, 0), 255);
            else
                dstp[x] = (src1p[x + d2] + src1n[x - d2] + 1) >> 1;
        } else {
            const int d20 = dir >> 1;
            const int d21 = (dir + 1) >> 1;
            const int d30 = (dir * 3) >> 1;
            const int d31 = (dir * 3 + 1) >> 1;
            const int ad = MAX(abs(d30), abs(d31));

            if(ucubic && x >= ad && x <= width - 1 - ad) {
                const int c0 = src3p[x + d30] + src3p[x + d31];
                const int c1 = src1p[x + d20] + src1p[x + d21]; // should use cubic if ucubic=true
                const int c2 = src1n[x - d20] + src1n[x - d21]; // should use cubic if ucubic=true
                const int c3 = src3n[x - d30] + src3n[x - d31];
                dstp[x] = MIN(MAX((36 * (c1 + c2) - 4 * (c0 + c3) + 64) >> 7, 0), 255);
            } else
                dstp[x] = (src1p[x + d20] + src1p[x + d21] + src1n[x - d20] + src1n[x - d21] + 2) >> 2;
        }
    }
}


VSFrameRef *copyPad(const VSFrameRef *src, int fn, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi, void **instanceData)
{
    eedi3Data *d = (eedi3Data *) * instanceData;

    const int off = 1 - fn;
    VSFrameRef *srcPF = vsapi->newVideoFrame(d->vi.format, d->vi.width + 24 * (1 << d->vi.format->subSamplingW), d->vi.height + 8 * (1 << d->vi.format->subSamplingH), NULL, core);

    int b, x, y;

    if(!d->dh) {
        for(b = 0; b < d->vi.format->numPlanes; ++b)
            vs_bitblt(vsapi->getWritePtr(srcPF, b) + vsapi->getStride(srcPF, b) * (4 + off) + 12,
                      vsapi->getStride(srcPF, b) * 2,
                      vsapi->getReadPtr(src, b) + vsapi->getStride(src, b)*off,
                      vsapi->getStride(src, b) * 2,
                      vsapi->getFrameWidth(src, b) * d->vi.format->bytesPerSample,
                      vsapi->getFrameHeight(src, b) >> 1);
    } else {
        for(b = 0; b < d->vi.format->numPlanes; ++b)
            vs_bitblt(vsapi->getWritePtr(srcPF, b) + vsapi->getStride(srcPF, b) * (4 + off) + 12,
                      vsapi->getStride(srcPF, b) * 2,
                      vsapi->getReadPtr(src, b),
                      vsapi->getStride(src, b),
                      vsapi->getFrameWidth(src, b) * d->vi.format->bytesPerSample,
                      vsapi->getFrameHeight(src, b));
    }

    for(b = 0; b < d->vi.format->numPlanes; ++b) {
        // fixme, probably pads a bit too much with subsampled formats
        uint8_t *dstp = vsapi->getWritePtr(srcPF, b);
        const int dst_pitch = vsapi->getStride(srcPF, b);
        const int height = vsapi->getFrameHeight(src, b) + 8;
        const int width = vsapi->getFrameWidth(src, b) + 24;
        dstp += (4 + off) * dst_pitch;

        for(y = 4 + off; y < height - 4; y += 2) {
            for(x = 0; x < 12; ++x)
                dstp[x] = dstp[24 - x];

            int c = 2;

            for(x = width - 12; x < width; ++x, c += 2)
                dstp[x] = dstp[x - c];

            dstp += dst_pitch * 2;
        }

        dstp = vsapi->getWritePtr(srcPF, b);

        for(y = off; y < 4; y += 2)
            vs_bitblt(dstp + y * dst_pitch, dst_pitch,
                      dstp + (8 - y) * dst_pitch, dst_pitch, width, 1);

        int c = 2 + 2 * off;

        for(y = height - 4 + off; y < height; y += 2, c += 4)
            vs_bitblt(dstp + y * dst_pitch, dst_pitch,
                      dstp + (y - c) * dst_pitch, dst_pitch, width, 1);
    }

    return srcPF;
}


static const VSFrameRef *VS_CC eedi3GetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
    eedi3Data *d = (eedi3Data *) * instanceData;

    if(activationReason == arInitial) {
        vsapi->requestFrameFilter(d->field > 1 ? (n >> 1) : n, d->node, frameCtx);

        if(d->sclip)
            vsapi->requestFrameFilter(n, d->sclip, frameCtx);
    } else if(activationReason == arAllFramesReady) {

        int field_n;

        if(d->field > 1) {
            if(n & 1)
                field_n = d->field == 3 ? 0 : 1;
            else
                field_n = d->field == 3 ? 1 : 0;
        } else
            field_n = d->field;

        const VSFrameRef *src = vsapi->getFrameFilter(d->field > 1 ? (n >> 1) : n, d->node, frameCtx);
        VSFrameRef *srcPF = copyPad(src, field_n, frameCtx, core, vsapi, instanceData);

        const VSFrameRef *scpPF;

        if(d->vcheck > 0 && d->sclip)
            scpPF = vsapi->getFrameFilter(n, d->sclip, frameCtx);
        else
            scpPF = NULL;

        // fixme,  adjust duration
        VSFrameRef *dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);
        vsapi->freeFrame(src);

        float *workspace;
        if (VS_ALIGNED_MALLOC((void **)&workspace, d->vi.width * MAX(d->mdis * 4 + 1, 16) * 4 * sizeof(float), 16)) {
            vsapi->setFilterError("EEDI3: Memory allocation failed", frameCtx);
            vsapi->freeFrame(scpPF);
            vsapi->freeFrame(srcPF);
            vsapi->freeFrame(dst);
            return 0;
        }

        int *dmapa;
        if (VS_ALIGNED_MALLOC((void **)&dmapa, vsapi->getStride(dst, 0)*vsapi->getFrameHeight(dst, 0)*sizeof(int), 16)) {
            VS_ALIGNED_FREE(workspace);
            vsapi->setFilterError("EEDI3: Memory allocation failed", frameCtx);
            vsapi->freeFrame(scpPF);
            vsapi->freeFrame(srcPF);
            vsapi->freeFrame(dst);
            return 0;
        }

        int b, x, y;

        for(b = 0; b < d->vi.format->numPlanes; ++b) {
            if(!(d->planes & (1 << b)))
                continue;

            const uint8_t *srcp = vsapi->getReadPtr(srcPF, b);
            const int spitch = vsapi->getStride(srcPF, b);
            const int width = vsapi->getFrameWidth(dst, b) + 24;
            const int height = vsapi->getFrameHeight(dst, b) + 8;
            uint8_t *dstp = vsapi->getWritePtr(dst, b);
            const int dpitch = vsapi->getStride(dst, b);
            vs_bitblt(dstp + (1 - field_n)*dpitch, dpitch * 2,
                      srcp + (4 + 1 - field_n)*spitch + 12, spitch * 2,
                      width - 24,
                      (height - 8) >> 1);
            srcp += (4 + field_n) * spitch;
            dstp += field_n * dpitch;

            // ~99% of the processing time is spent in this loop
            for(y = 4 + field_n; y < height - 4; y += 2) {
                const int off = (y - 4 - field_n) >> 1;

                if(d->hp)
                    interpLineHP(srcp + 12 + off * 2 * spitch, width - 24, spitch, d->alpha, d->beta,
                                 d->gamma, d->nrad, d->mdis, workspace, dstp + off * 2 * dpitch,
                                 dmapa + off * dpitch, d->ucubic, d->cost3);
                else
                    interpLineFP(srcp + 12 + off * 2 * spitch, width - 24, spitch, d->alpha, d->beta,
                                 d->gamma, d->nrad, d->mdis, workspace, dstp + off * 2 * dpitch,
                                 dmapa + off * dpitch, d->ucubic, d->cost3);
            }

            if(d->vcheck > 0) {
                int *dstpd = dmapa;
                const uint8_t *scpp = NULL;
                int scpitch;

                if(d->sclip) {
                    scpitch = vsapi->getStride(scpPF, b);
                    scpp = vsapi->getReadPtr(scpPF, b) + field_n * scpitch;
                }

                for(y = 4 + field_n; y < height - 4; y += 2) {
                    if(y >= 6 && y < height - 6) {
                        const uint8_t *dst3p = srcp - 3 * spitch + 12;
                        const uint8_t *dst2p = dstp - 2 * dpitch;
                        const uint8_t *dst1p = dstp - 1 * dpitch;
                        const uint8_t *dst1n = dstp + 1 * dpitch;
                        const uint8_t *dst2n = dstp + 2 * dpitch;
                        const uint8_t *dst3n = srcp + 3 * spitch + 12;
                        uint8_t *tline = (uint8_t *)workspace;

                        for(x = 0; x < width - 24; ++x) {
                            const int dirc = dstpd[x];
                            const int cint = scpp ? scpp[x] :
                                             MIN(MAX((36 * (dst1p[x] + dst1n[x]) - 4 * (dst3p[x] + dst3n[x]) + 32) >> 6, 0), 255);

                            if(dirc == 0) {
                                tline[x] = cint;
                                continue;
                            }

                            const int dirt = dstpd[x - dpitch];

                            const int dirb = dstpd[x + dpitch];

                            if(MAX(dirc * dirt, dirc * dirb) < 0 || (dirt == dirb && dirt == 0)) {
                                tline[x] = cint;
                                continue;
                            }

                            int it, ib, vt, vb, vc;
                            vc = abs(dstp[x] - dst1p[x]) + abs(dstp[x] - dst1n[x]);

                            if(d->hp) {
                                if(!(dirc & 1)) {
                                    const int d2 = dirc >> 1;
                                    it = (dst2p[x + d2] + dstp[x - d2] + 1) >> 1;
                                    vt = abs(dst2p[x + d2] - dst1p[x + d2]) + abs(dstp[x + d2] - dst1p[x + d2]);
                                    ib = (dstp[x + d2] + dst2n[x - d2] + 1) >> 1;
                                    vb = abs(dst2n[x - d2] - dst1n[x - d2]) + abs(dstp[x - d2] - dst1n[x - d2]);
                                } else {
                                    const int d20 = dirc >> 1;
                                    const int d21 = (dirc + 1) >> 1;
                                    const int pa2p = dst2p[x + d20] + dst2p[x + d21] + 1;
                                    const int pa1p = dst1p[x + d20] + dst1p[x + d21] + 1;
                                    const int ps0 = dstp[x - d20] + dstp[x - d21] + 1;
                                    const int pa0 = dstp[x + d20] + dstp[x + d21] + 1;
                                    const int ps1n = dst1n[x - d20] + dst1n[x - d21] + 1;
                                    const int ps2n = dst2n[x - d20] + dst2n[x - d21] + 1;
                                    it = (pa2p + ps0) >> 2;
                                    vt = (abs(pa2p - pa1p) + abs(pa0 - pa1p)) >> 1;
                                    ib = (pa0 + ps2n) >> 2;
                                    vb = (abs(ps2n - ps1n) + abs(ps0 - ps1n)) >> 1;
                                }
                            } else {
                                it = (dst2p[x + dirc] + dstp[x - dirc] + 1) >> 1;
                                vt = abs(dst2p[x + dirc] - dst1p[x + dirc]) + abs(dstp[x + dirc] - dst1p[x + dirc]);
                                ib = (dstp[x + dirc] + dst2n[x - dirc] + 1) >> 1;
                                vb = abs(dst2n[x - dirc] - dst1n[x - dirc]) + abs(dstp[x - dirc] - dst1n[x - dirc]);
                            }

                            const int d0 = abs(it - dst1p[x]);
                            const int d1 = abs(ib - dst1n[x]);
                            const int d2 = abs(vt - vc);
                            const int d3 = abs(vb - vc);

                            const int mdiff0 = d->vcheck == 1 ? MIN(d0, d1) : d->vcheck == 2 ? ((d0 + d1 + 1) >> 1) : MAX(d0, d1);
                            const int mdiff1 = d->vcheck == 1 ? MIN(d2, d3) : d->vcheck == 2 ? ((d2 + d3 + 1) >> 1) : MAX(d2, d3);

                            const float a0 = mdiff0 / d->vthresh0;
                            const float a1 = mdiff1 / d->vthresh1;

                            const int dircv = d->hp ? (abs(dirc) >> 1) : abs(dirc);

                            const float a2 = MAX((d->vthresh2 - dircv) / d->vthresh2, 0.0f);
                            const float a = MIN(MAX(MAX(a0, a1), a2), 1.0f);

                            tline[x] = (int)((1.0 - a) * dstp[x] + a * cint);
                        }

                        memcpy(dstp, tline, width - 24);
                    }

                    srcp += 2 * spitch;
                    dstp += 2 * dpitch;

                    if(scpp)
                        scpp += 2 * scpitch;

                    dstpd += dpitch;
                }
            }
        }

        VS_ALIGNED_FREE(dmapa);
        VS_ALIGNED_FREE(workspace);
        vsapi->freeFrame(srcPF);
        vsapi->freeFrame(scpPF);
        return dst;
    }

    return 0;
}


static void VS_CC eedi3Free(void *instanceData, VSCore *core, const VSAPI *vsapi)
{
    eedi3Data *d = (eedi3Data *)instanceData;
    vsapi->freeNode(d->node);
    vsapi->freeNode(d->sclip);
    free(d);
}


static void VS_CC eedi3Create(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
    eedi3Data d;
    eedi3Data *data;
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, 0);
    d.vi = *vsapi->getVideoInfo(d.node);


    d.field = int64ToIntS(vsapi->propGetInt(in, "field", 0, NULL));

    d.dh = !!vsapi->propGetInt(in, "dh", 0, &err);

    d.alpha = (float)vsapi->propGetFloat(in, "alpha", 0, &err);

    if(err)
        d.alpha = 0.2f;

    d.beta = (float)vsapi->propGetFloat(in, "beta", 0, &err);

    if(err)
        d.beta = 0.25f;

    d.gamma = (float)vsapi->propGetFloat(in, "gamma", 0, &err);

    if(err)
        d.gamma = 20.0f;

    d.nrad = int64ToIntS(vsapi->propGetInt(in, "nrad", 0, &err));

    if(err)
        d.nrad = 2;

    d.mdis = int64ToIntS(vsapi->propGetInt(in, "mdis", 0, &err));

    if(err)
        d.mdis = 20;

    d.hp = !!vsapi->propGetInt(in, "hp", 0, &err);

    d.ucubic = !!vsapi->propGetInt(in, "ucubic", 0, &err);

    if(err)
        d.ucubic = 1;

    d.cost3 = !!vsapi->propGetInt(in, "cost3", 0, &err);

    if(err)
        d.cost3 = 1;

    d.vcheck = int64ToIntS(vsapi->propGetInt(in, "vcheck", 0, &err));

    if(err)
        d.vcheck = 2;

    d.vthresh0 = (float)vsapi->propGetFloat(in, "vthresh0", 0, &err);

    if(err)
        d.vthresh0 = 32;

    d.vthresh1 = (float)vsapi->propGetFloat(in, "vthresh1", 0, &err);

    if(err)
        d.vthresh1 = 64.0f;

    d.vthresh2 = (float)vsapi->propGetFloat(in, "vthresh2", 0, &err);

    if(err)
        d.vthresh2 = 4.0f;

    d.sclip = vsapi->propGetNode(in, "sclip", 0, &err);

    d.planes = 0;
    int nump = vsapi->propNumElements(in, "planes");

    if(nump <= 0) {
        d.planes = -1;
    } else {
        int i;

        for(i = 0; i < nump; i++)
            d.planes |= 1 << vsapi->propGetInt(in, "planes", i, NULL);
    }


    // goto or macro... macro or goto...
    char msg[80];

    if(d.vi.format->bytesPerSample != 1) {
        sprintf(msg, "eedi3:  only 8 bits per sample input supported");
        goto error;
    }

    if((d.vi.height & 1) && !d.dh) {
        sprintf(msg, "eedi3:  height must be mod 2 when dh=false!");
        goto error;
    }



    if(d.field < 0 || d.field > 3) {
        sprintf(msg, "eedi3:  field must be set to 0, 1, 2, or 3!");
        goto error;
    }

    if(d.dh && d.field > 1) {
        sprintf(msg, "eedi3:  field must be set to 0 or 1 when dh=true!");
        goto error;
    }

    if(d.alpha < 0.0f || d.alpha > 1.0f) {
        sprintf(msg, "eedi3:  0 <= alpha <= 1!");
        goto error;
    }

    if(d.beta < 0.0f || d.beta > 1.0f) {
        sprintf(msg, "eedi3:  0 <= beta <= 1!");
        goto error;
    }

    if(d.alpha + d.beta > 1.0f) {
        sprintf(msg, "eedi3:  0 <= alpha+beta <= 1!");
        goto error;
    }

    if(d.gamma < 0.0f) {
        sprintf(msg, "eedi3:  0 <= gamma!");
        goto error;
    }

    if(d.nrad < 0 || d.nrad > 3) {
        sprintf(msg, "eedi3:  0 <= nrad <= 3!");
        goto error;
    }

    if(d.mdis < 1 || d.mdis > 40) {
        sprintf(msg, "eedi3:  1 <= mdis <= 40!");
        goto error;
    }

    if(d.vcheck < 0 || d.vcheck > 3) {
        sprintf(msg, "eedi3:  0 <= vcheck <= 3!");
        goto error;
    }

    if(d.vcheck > 0 && (d.vthresh0 <= 0.0f || d.vthresh1 <= 0.0f || d.vthresh2 <= 0.0f)) {
        sprintf(msg, "eedi3:  0 < vthresh0 , 0 < vthresh1 , 0 < vthresh2!");
        goto error;
    }

    if(d.field > 1) {
        d.vi.numFrames *= 2;
        d.vi.fpsNum *= 2;
    }

    if(d.dh)
        d.vi.height *= 2;

    if(d.vcheck > 0 && d.sclip) {
        const VSVideoInfo *vi2 = vsapi->getVideoInfo(d.sclip);

        if(d.vi.height != vi2->height ||
                d.vi.width != vi2->width ||
                d.vi.numFrames != vi2->numFrames ||
                d.vi.format != vi2->format) {
            sprintf(msg, "eedi3:  sclip doesn't match!\n");
            goto error;
        }
    }


    data = (eedi3Data *)malloc(sizeof(d));
    *data = d;

    vsapi->createFilter(in, out, "eedi3", eedi3Init, eedi3GetFrame, eedi3Free, fmParallel, 0, data, core);
    return;

error:
    vsapi->freeNode(d.node);
    vsapi->freeNode(d.sclip);
    vsapi->setError(out, msg);
    return;
}


VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
    configFunc("com.vapoursynth.eedi3", "eedi3", "EEDI3", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("eedi3", "clip:clip;field:int;dh:int:opt;planes:int[]:opt;alpha:float:opt;beta:float:opt;gamma:float:opt;nrad:int:opt;mdis:int:opt;" \
                 "hp:int:opt;ucubic:int:opt;cost3:int:opt;vcheck:int:opt;vthresh0:float:opt;vthresh1:float:opt;vthresh2:float:opt;sclip:clip:opt;",
                 eedi3Create, NULL, plugin);
}
