/*
* Copyright (c) 2017 Fredrik Mellbin
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

#include "internalfilters.h"
#include "VSHelper.h"
#include "filtershared.h"
#include "filtersharedcpp.h"

#include <memory>
#include <algorithm>
#include <string>
#include <vector>

//////////////////////////////////////////
// BoxBlur

struct BoxBlurData {
	VSNodeRef *node;
	int radius, passes;
	bool process[3];
};

template<typename T>
static void blurH(const T * VS_RESTRICT src, T * VS_RESTRICT dst, const int width, const int radius, const unsigned div, const unsigned round) {
	unsigned acc = radius * src[0];
	for (int x = 0; x < radius; x++)
		acc += src[std::min(x, width - 1)];

	for (int x = 0; x < std::min(radius, width); x++) {
		acc += src[std::min(x + radius, width - 1)];
		dst[x] = (acc + round) / div;
		acc -= src[std::max(x - radius, 0)];
	}

	if (width > radius) {
		for (int x = radius; x < width - radius; x++) {
			acc += src[x + radius];
			dst[x] = (acc + round) / div;
			acc -= src[x - radius];
		}

		for (int x = std::max(width - radius, radius); x < width; x++) {
			acc += src[std::min(x + radius, width - 1)];
			dst[x] = (acc + round) / div;
			acc -= src[std::max(x - radius, 0)];
		}
	}
}

template<typename T>
static void processPlane(const uint8_t *src, uint8_t *dst, int stride, int width, int height, int passes, int radius, uint8_t *tmp) {
	const unsigned div = radius * 2 + 1;
	const unsigned round = div - 1;
	for (int h = 0; h < height; h++) {
		uint8_t *dst1 = (passes & 1) ? dst : tmp;
		uint8_t *dst2 = (passes & 1) ? tmp : dst;
		blurH(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst1), width, radius, div, round);
		for (int p = 1; p < passes; p++) {
			blurH(reinterpret_cast<const T *>(dst1), reinterpret_cast<T *>(dst2), width, radius, div, (p & 1) ? 0 : round);
			std::swap(dst1, dst2);
		}
		src += stride;
		dst += stride;
	}
}

template<typename T>
static void blurHR1(const T *src, T *dst, int width, const unsigned round) {
	unsigned tmp[2] = { src[0], src[1] };
	unsigned acc = tmp[0] * 2 + tmp[1];
	dst[0] = (acc + round) / 3;
	acc -= tmp[0];

	unsigned v = src[2];
	acc += v;
	dst[1] = (acc + round) / 3;
	acc -= tmp[0];
	tmp[0] = v;

	for (int x = 2; x < width - 2; x+= 2) {
		v = src[x + 1];
		acc += v;
		dst[x] = (acc + round) / 3;
		acc -= tmp[1];
		tmp[1] = v;

		v = src[x + 2];
		acc += v;
		dst[x + 1] = (acc + round) / 3;
		acc -= tmp[0];
		tmp[0] = v;
	}

	if (width & 1) {
		acc += tmp[0];
		dst[width - 1] = (acc + round) / 3;
	} else {
		v = src[width - 1];
		acc += v;
		dst[width - 2] = (acc + round) / 3;
		acc -= tmp[1];

		acc += v;
		dst[width - 1] = (acc + round) / 3;
	}
}

template<typename T>
static void processPlaneR1(const uint8_t *src, uint8_t *dst, int stride, int width, int height, int passes) {
	for (int h = 0; h < height; h++) {
		blurHR1(reinterpret_cast<const T *>(src), reinterpret_cast<T *>(dst), width, 2);
		for (int p = 1; p < passes; p++)
			blurHR1(reinterpret_cast<const T *>(dst), reinterpret_cast<T *>(dst), width, (p & 1) ? 0 : 2);
		src += stride;
		dst += stride;
	}
}

static const VSFrameRef *VS_CC boxBlurGetframe(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	BoxBlurData *d = reinterpret_cast<BoxBlurData *>(*instanceData);

	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(n, d->node, frameCtx);
	} else if (activationReason == arAllFramesReady) {
		const VSFrameRef *src = vsapi->getFrameFilter(n, d->node, frameCtx);
		const VSFormat *fi = vsapi->getFrameFormat(src);
		const int pl[] = { 0, 1, 2 };
		const VSFrameRef *fr[] = { d->process[0] ? nullptr : src, d->process[1] ? nullptr : src, d->process[2] ? nullptr : src };
		VSFrameRef *dst = vsapi->newVideoFrame2(fi, vsapi->getFrameWidth(src, 0), vsapi->getFrameHeight(src, 0), fr, pl, src, core);
		int bytesPerSample = fi->bytesPerSample;
		int radius = d->radius;
		uint8_t *tmp = (radius > 1) ? new uint8_t[bytesPerSample * vsapi->getFrameWidth(src, d->process[0] ? 0 : 1)] : nullptr;

		for (int plane = 0; plane < fi->numPlanes; plane++) {
			if (d->process[plane]) {			
				const uint8_t *srcp = vsapi->getReadPtr(src, plane);
				int stride = vsapi->getStride(src, plane);
				uint8_t *dstp = vsapi->getWritePtr(dst, plane);
				int h = vsapi->getFrameHeight(src, plane);
				int w = vsapi->getFrameWidth(src, plane);

				if (radius == 1) {
					if (bytesPerSample == 1)
						processPlaneR1<uint8_t>(srcp, dstp, stride, w, h, d->passes);
					else
						processPlaneR1<uint16_t>(srcp, dstp, stride, w, h, d->passes);
				} else {
					if (bytesPerSample == 1)
						processPlane<uint8_t>(srcp, dstp, stride, w, h, d->passes, radius, tmp);
					else
						processPlane<uint16_t>(srcp, dstp, stride, w, h, d->passes, radius, tmp);
				}
			}
		}

		delete[] tmp;

		vsapi->freeFrame(src);
		return dst;
	}

	return nullptr;
}

static void VS_CC boxBlurCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
	VSNodeRef *node = vsapi->propGetNode(in, "clip", 0, 0);

	try {
		int err;
		const VSVideoInfo *vi = vsapi->getVideoInfo(node);

		if (isCompatFormat(vi))
			throw std::string("compat formats are not supported");

		if (!isConstantFormat(vi))
			throw std::string("only constant format input supported");

		if (vi->format->sampleType != stInteger || vi->format->bitsPerSample > 16)
			throw std::string("only clips with integer samples and up to 16 bits per channel precision supported");

		bool process[3];
		getPlanesArg(in, process, vsapi);

		int hradius = vsapi->propGetInt(in, "hradius", 0, &err);
		int hpasses = vsapi->propGetInt(in, "hpasses", 0, &err);
		if (err)
			hpasses = 1;
		bool hblur = (hradius > 0) && (hpasses > 0);

		int vradius = vsapi->propGetInt(in, "vradius", 0, &err);
		int vpasses = vsapi->propGetInt(in, "vpasses", 0, &err);
		if (err)
			vpasses = 1;
		bool vblur = (vradius > 0) && (vpasses > 0);

		if (!hblur && !vblur)
			throw std::string("nothing to be performed");


		VSPlugin *stdplugin = vsapi->getPluginById("com.vapoursynth.std", core);

		if (hblur) {
			VSMap *htmp = vsapi->createMap();
			vsapi->createFilter(in, htmp, "BoxBlur", templateNodeInit<BoxBlurData>, boxBlurGetframe, templateNodeFree<BoxBlurData>, fmParallel, 0, new BoxBlurData{ node, hradius, hpasses, { process[0], process[1], process[2] } }, core);
			node = vsapi->propGetNode(htmp, "clip", 0, nullptr);
			vsapi->freeMap(htmp);
		}

		if (vblur) {
			VSMap *vtmp1 = vsapi->createMap();
			vsapi->propSetNode(vtmp1, "clip", node, paAppend);
			vsapi->freeNode(node);
			VSMap *vtmp2 = vsapi->invoke(stdplugin, "Transpose", vtmp1);
			vsapi->clearMap(vtmp1);
			node = vsapi->propGetNode(vtmp2, "clip", 0, nullptr);
			vsapi->clearMap(vtmp2);
			vsapi->createFilter(in, vtmp1, "BoxBlur", templateNodeInit<BoxBlurData>, boxBlurGetframe, templateNodeFree<BoxBlurData>, fmParallel, 0, new BoxBlurData{ node, vradius, vpasses, { process[0], process[1], process[2] } }, core);
			vtmp2 = vsapi->invoke(stdplugin, "Transpose", vtmp1);
			vsapi->freeMap(vtmp1);
			node = vsapi->propGetNode(vtmp2, "clip", 0, nullptr);
			vsapi->freeMap(vtmp2);
		}

		vsapi->propSetNode(out, "clip", node, paAppend);
		vsapi->freeNode(node);

	} catch (std::string &e) {
		vsapi->freeNode(node);
		RETERROR(("BoxBlur: " + e).c_str());
	}
}

//////////////////////////////////////////
// Init

void VS_CC boxBlurInitialize(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
	//configFunc("com.vapoursynth.std", "std", "VapourSynth Core Functions", VAPOURSYNTH_API_VERSION, 1, plugin);
	registerFunc("BoxBlur", "clip:clip;planes:int[]:opt;hradius:int:opt;hpasses:int:opt;vradius:int:opt;vpasses:int:opt;", boxBlurCreate, 0, plugin);
}
