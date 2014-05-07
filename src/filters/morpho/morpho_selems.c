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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "morpho_selems.h"

const SElemFunc SElemFuncs[] = {
    SquareSElem,
    DiamondSElem,
    CircleSElem,
    NULL
};

void SquareSElem(uint8_t *selem, int size) {
    memset(selem, 1, sizeof(uint8_t) * size * size);
}

void DiamondSElem(uint8_t *selem, int size) {
    int x, y;
    int hs = size / 2;

    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            *selem++ = (abs(x - hs) - (hs - abs(y - hs)) <= 0);
        }
    }
}

void CircleSElem(uint8_t *selem, int size) {
    int r = size / 2;
    int f = 1 - r;
    int ddFx = 0, ddFy = -(r << 1);
    int x, y;

    for (x = 0, y = r; x < y;) {
        if (f >= 0) {
            int x1 = r - x, x2 = r + x;
            int y1 = r - y, y2 = r + y;
            int i;

            for (i = x1; i < x2; i++) {
                selem[i + y1 * size] = 1;
                selem[i + y2 * size] = 1;
            }

            f += (ddFy += 2);
            y--;
        }

        f += (ddFx += 2);

        if (y != x++) {
            int x1 = r - y, x2 = r + y;
            int y1 = r - x, y2 = r + x;
            int i;

            for (i = x1; i < x2; i++) {
                selem[i + y1 * size] = 1;
                selem[i + y2 * size] = 1;
            }
        }

    }

    for (y = 0; y < r * 2; y++) {
        selem[y + (r * size)] = 9;
    }
}
