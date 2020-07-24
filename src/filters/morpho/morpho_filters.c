/*
 * Simple morphological filters
 *
 * Copyright (c) 2014, Martin Herkt <lachs0r@srsfckn.biz>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <math.h>
#include <stdint.h>

#include "VapourSynth4.h"
#include "VSHelper4.h"

#include "morpho.h"
#include "morpho_filters.h"

const char *FilterNames[] = {
    "Dilate",
    "Erode",
    "Open",
    "Close",
    "TopHat",
    "BottomHat",
    NULL
};

const MorphoFilter FilterFuncs[] = {
    MorphoDilate,
    MorphoErode,
    MorphoOpen,
    MorphoClose,
    MorphoTopHat,
    MorphoBottomHat,
    NULL
};

static inline int Border(int v, int max) {
    if (v < 0)
        v = -v;
    else if (v > max)
        v = max - (v - max);

    return v;
}

#define MORPHO(T,V,OP)                                                         \
    int x, y;                                                                  \
    int hsize = d->size / 2;                                                   \
                                                                               \
    for (y = 0; y < height; y++) {                                             \
        for (x = 0; x < width; x++) {                                          \
            int i, j;                                                          \
            T v = (V);                                                         \
                                                                               \
            for (j = -hsize; j <= hsize; j++) {                                \
                for (i = -hsize; i <= hsize; i++) {                            \
                    uint8_t mv = d->selem[i + hsize + ((j + hsize) * d->size)];\
                                                                               \
                    if (mv) {                                                  \
                        int sx = Border(x + i, width - 1);                     \
                        int sy = Border(y + j, height - 1);                    \
                        T sv = ((const T *)&src[sy * stride])[sx];             \
                        v = OP(v, sv);                                         \
                    }                                                          \
                }                                                              \
            }                                                                  \
                                                                               \
            ((T *)dst)[x] = v;                                                 \
        }                                                                      \
                                                                               \
        dst += stride;                                                         \
    }

void MorphoDilate(const uint8_t *src, uint8_t *dst,
                  int width, int height, ptrdiff_t stride, MorphoData *d)
{
    if (d->vi.format.bytesPerSample == 1) {
        MORPHO(uint8_t, 0, VSMAX);
    } else {
        MORPHO(uint16_t, 0, VSMAX);
    }
}

void MorphoErode(const uint8_t *src, uint8_t *dst,
                 int width, int height, ptrdiff_t stride, MorphoData *d)
{
    int sval = (1 << d->vi.format.bitsPerSample) - 1;

    if (d->vi.format.bytesPerSample == 1) {
        MORPHO(uint8_t, sval, VSMIN);
    } else {
        MORPHO(uint16_t, sval, VSMIN);
    }
}

void MorphoOpen(const uint8_t *src, uint8_t *dst,
                int width, int height, ptrdiff_t stride, MorphoData *d)
{
    uint8_t *tmp = malloc(sizeof(uint8_t) * stride * height);
    MorphoErode(src, tmp, width, height, stride, d);
    MorphoDilate((const uint8_t*)tmp, dst, width, height, stride, d);
    free(tmp);
}

void MorphoClose(const uint8_t *src, uint8_t *dst,
                 int width, int height, ptrdiff_t stride, MorphoData *d)
{
    uint8_t *tmp = malloc(sizeof(uint8_t) * stride * height);
    MorphoDilate(src, tmp, width, height, stride, d);
    MorphoErode((const uint8_t*)tmp, dst, width, height, stride, d);
    free(tmp);
}

void MorphoTopHat(const uint8_t *src, uint8_t *dst,
                  int width, int height, ptrdiff_t stride, MorphoData *d)
{
    int x, y;

    MorphoOpen(src, dst, width, height, stride, d);

    for (y = 0; y < height; y++) {
        if (d->vi.format.bytesPerSample == 1) {
            for (x = 0; x < width; x++) {
                dst[x] = VSMAX(0, (int16_t)src[x] - dst[x]);
            }
        } else {
            const uint16_t *srcp = (const uint16_t *)src;
            uint16_t *dstp = (uint16_t *)dst;

            for (x = 0; x < width; x++) {
                dstp[x] = VSMAX(0, (int32_t)srcp[x] - dstp[x]);
            }
        }

        dst += stride;
        src += stride;
    }
}

void MorphoBottomHat(const uint8_t *src, uint8_t *dst,
                     int width, int height, ptrdiff_t stride, MorphoData *d)
{
    int x, y;

    MorphoClose(src, dst, width, height, stride, d);

    for (y = 0; y < height; y++) {
        if (d->vi.format.bytesPerSample == 1) {
            for (x = 0; x < width; x++) {
                dst[x] = VSMAX(0, (int16_t)dst[x] - src[x]);
            }
        } else {
            const uint16_t *srcp = (const uint16_t *)src;
            uint16_t *dstp = (uint16_t *)dst;

            for (x = 0; x < width; x++) {
                dstp[x] = VSMAX(0, (int32_t)dstp[x] - srcp[x]);
            }
        }

        dst += stride;
        src += stride;
    }
}
