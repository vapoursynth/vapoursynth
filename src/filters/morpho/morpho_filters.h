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

typedef void (*MorphoFilter)(const uint8_t*, uint8_t*, int, int, ptrdiff_t, MorphoData*);

void MorphoDilate(const uint8_t *src, uint8_t *dst,
                  int width, int height, ptrdiff_t stride, MorphoData *d);
void MorphoErode(const uint8_t *src, uint8_t *dst,
                 int width, int height, ptrdiff_t stride, MorphoData *d);
void MorphoOpen(const uint8_t *src, uint8_t *dst,
                int width, int height, ptrdiff_t stride, MorphoData *d);
void MorphoClose(const uint8_t *src, uint8_t *dst,
                 int width, int height, ptrdiff_t stride, MorphoData *d);
void MorphoTopHat(const uint8_t *src, uint8_t *dst,
                  int width, int height, ptrdiff_t stride, MorphoData *d);
void MorphoBottomHat(const uint8_t *src, uint8_t *dst,
                     int width, int height, ptrdiff_t stride, MorphoData *d);

extern const char *FilterNames[];
extern const MorphoFilter FilterFuncs[];
