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

#include <cfloat>
#include <cstring>

#include "eedi3.h"

void bitblt(uint8_t *dstp, int dst_stride, const uint8_t *srcp, int src_stride, int row_size, int height) {
    if (src_stride == dst_stride && dst_stride == row_size) {
        memcpy(dstp, srcp, row_size * height);
    } else {
        int i;

        for (i = 0; i < height; i++) {
            memcpy(dstp, srcp, row_size);
            dstp += dst_stride;
            srcp += src_stride;
        }
    }
}

void VS_CC eedi3::eedi3Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
	eedi3 *d = (eedi3 *)*instanceData;
	vsapi->setVideoInfo(&d->vi, node);
}

void VS_CC eedi3::eedi3Free(void *instanceData, VSCore *core, const VSAPI *vsapi) {
	eedi3 *d = (eedi3 *)instanceData;
	vsapi->freeNode(d->child);
	vsapi->freeNode(d->sclip);
	delete d;
}

eedi3::eedi3(const VSNodeRef *_child, int _field, bool _dh, int _planes, float _alpha,
	float _beta, float _gamma, int _nrad, int _mdis, bool _hp, bool _ucubic, bool _cost3, 
	int _vcheck, float _vthresh0,  float _vthresh1, float _vthresh2, const VSNodeRef *_sclip, const VSAPI *vsapi) : 
	child(_child), field(_field), dh(_dh), planes(_planes),
	alpha(_alpha), beta(_beta), gamma(_gamma), nrad(_nrad), mdis(_mdis), hp(_hp), 
	ucubic(_ucubic), cost3(_cost3), vcheck(_vcheck), vthresh0(_vthresh0), vthresh1(_vthresh1), 
	vthresh2(_vthresh2), sclip(_sclip)
{
	vi = *vsapi->getVideoInfo(child);
	if (field < 0 || field > 3)
		throw std::runtime_error(std::string("eedi3:  field must be set to 0, 1, 2, or 3!"));
	if (dh && (field < -1 || field > 1))
		throw std::runtime_error(std::string("eedi3:  field must be set to -1, 0, or 1 when dh=true!"));
	if (alpha < 0.0f || alpha > 1.0f)
		throw std::runtime_error(std::string("eedi3:  0 <= alpha <= 1!"));
	if (beta < 0.0f || beta > 1.0f)
		throw std::runtime_error(std::string("eedi3:  0 <= beta <= 1!"));
	if (alpha+beta > 1.0f)
		throw std::runtime_error(std::string("eedi3:  0 <= alpha+beta <= 1!"));
	if (gamma < 0.0f)
		throw std::runtime_error(std::string("eedi3:  0 <= gamma!"));
	if (nrad < 0 || nrad > 3)
		throw std::runtime_error(std::string("eedi3:  0 <= nrad <= 3!"));
	if (mdis < 1 || mdis > 40)
		throw std::runtime_error(std::string("eedi3:  1 <= mdis <= 40!"));
	if (vcheck < 0 || vcheck > 3)
		throw std::runtime_error(std::string("eedi3:  0 <= vcheck <= 3!"));
	if (vcheck > 0 && (vthresh0 <= 0.0f || vthresh1 <= 0.0f || vthresh2 <= 0.0f))
		throw std::runtime_error(std::string("eedi3:  0 < vthresh0 , 0 < vthresh1 , 0 < vthresh2!"));
	if (field > 1)
	{
		vi.numFrames *= 2;
		vi.fpsNum *= 2;
	}
	if (dh)
		vi.height *= 2;

	if (vcheck > 0 && sclip)
	{
		const VSVideoInfo *vi2 = vsapi->getVideoInfo(sclip);
		if (vi.height != vi2->height ||
			vi.width != vi2->width ||
			vi.numFrames != vi2->numFrames ||
			vi.format != vi2->format)
			throw std::runtime_error(std::string("eedi3:  sclip doesn't match!\n"));
	}
}

void interpLineFP(const unsigned char *srcp, const int width, const int pitch, 
	const float alpha, const float beta, const float gamma, const int nrad, 
	const int mdis, float *temp, unsigned char *dstp, int *dmap, const bool ucubic,
	const bool cost3)
{
	const unsigned char *src3p = srcp-3*pitch;
	const unsigned char *src1p = srcp-1*pitch;
	const unsigned char *src1n = srcp+1*pitch;
	const unsigned char *src3n = srcp+3*pitch;
	const int tpitch = mdis*2+1;
	float *ccosts = temp;
	float *pcosts = ccosts+width*tpitch;
	int *pbackt = (int*)(pcosts+width*tpitch);
	int *fpath = pbackt+width*tpitch;
	// calculate all connection costs
	if (!cost3)
	{
		for (int x=0; x<width; ++x)
		{
			const int umax = std::min(std::min(x,width-1-x),mdis);
			for (int u=-umax; u<=umax; ++u)
			{
				int s = 0;
				for (int k=-nrad; k<=nrad; ++k)
					s += 
						abs(src3p[x+u+k]-src1p[x-u+k])+
						abs(src1p[x+u+k]-src1n[x-u+k])+
						abs(src1n[x+u+k]-src3n[x-u+k]);
				const int ip = (src1p[x+u]+src1n[x-u]+1)>>1; // should use cubic if ucubic=true
				const int v = abs(src1p[x]-ip)+abs(src1n[x]-ip);
				ccosts[x*tpitch+mdis+u] = alpha*s+beta*abs(u)+(1.0f-alpha-beta)*v;
			}
		}
	}
	else
	{
		for (int x=0; x<width; ++x)
		{
			const int umax = std::min(std::min(x,width-1-x),mdis);
			for (int u=-umax; u<=umax; ++u)
			{
				int s0 = 0, s1 = -1, s2 = -1;
				for (int k=-nrad; k<=nrad; ++k)
					s0 += 
						abs(src3p[x+u+k]-src1p[x-u+k])+
						abs(src1p[x+u+k]-src1n[x-u+k])+
						abs(src1n[x+u+k]-src3n[x-u+k]);
				if ((u >= 0 && x >= u*2) || (u <= 0 && x < width+u*2))
				{
					s1 = 0;
					for (int k=-nrad; k<=nrad; ++k)
						s1 += 
							abs(src3p[x+k]-src1p[x-u*2+k])+
							abs(src1p[x+k]-src1n[x-u*2+k])+
							abs(src1n[x+k]-src3n[x-u*2+k]);
				}
				if ((u <= 0 && x >= u*2) || (u >= 0 && x < width+u*2))
				{
					s2 = 0;
					for (int k=-nrad; k<=nrad; ++k)
						s2 += 
							abs(src3p[x+u*2+k]-src1p[x+k])+
							abs(src1p[x+u*2+k]-src1n[x+k])+
							abs(src1n[x+u*2+k]-src3n[x+k]);
				}
				s1 = s1 >= 0 ? s1 : (s2 >= 0 ? s2 : s0);
				s2 = s2 >= 0 ? s2 : (s1 >= 0 ? s1 : s0);
				const int ip = (src1p[x+u]+src1n[x-u]+1)>>1; // should use cubic if ucubic=true
				const int v = abs(src1p[x]-ip)+abs(src1n[x]-ip);
				ccosts[x*tpitch+mdis+u] = alpha*(s0+s1+s2)*0.333333f+beta*abs(u)+(1.0f-alpha-beta)*v;
			}
		}
	}
	// calculate path costs
	pcosts[mdis] = ccosts[mdis];
	for (int x=1; x<width; ++x)
	{
		float *tT = ccosts+x*tpitch;
		float *ppT = pcosts+(x-1)*tpitch;
		float *pT = pcosts+x*tpitch;
		int *piT = pbackt+(x-1)*tpitch;
		const int umax = std::min(std::min(x,width-1-x),mdis);
		for (int u=-umax; u<=umax; ++u)
		{
			int idx;
			float bval = FLT_MAX;
			const int umax2 = std::min(std::min(x-1,width-x),mdis);
			for (int v=std::max(-umax2,u-1); v<=std::min(umax2,u+1); ++v)
			{
				const double y = ppT[mdis+v]+gamma*abs(u-v);
				const float ccost = (float)std::min(y,FLT_MAX*0.9);
				if (ccost < bval)
				{
					bval = ccost;
					idx = v;
				}
			}
			const double y = bval+tT[mdis+u];
			pT[mdis+u] = (float)std::min(y,FLT_MAX*0.9);
			piT[mdis+u] = idx;
		}
	}
	// backtrack
	fpath[width-1] = 0;
	for (int x=width-2; x>=0; --x)
		fpath[x] = pbackt[x*tpitch+mdis+fpath[x+1]];
	// interpolate
	for (int x=0; x<width; ++x)
	{
		const int dir = fpath[x];
		dmap[x] = dir;
		const int ad = abs(dir);
		if (ucubic && x >= ad*3 && x <= width-1-ad*3)
			dstp[x] = std::min(std::max((36*(src1p[x+dir]+src1n[x-dir])-
				4*(src3p[x+dir*3]+src3n[x-dir*3])+32)>>6,0),255);
		else
			dstp[x] = (src1p[x+dir]+src1n[x-dir]+1)>>1;
	}
}

void interpLineHP(const unsigned char *srcp, const int width, const int pitch, 
	const float alpha, const float beta, const float gamma, const int nrad, 
	const int mdis, float *temp, unsigned char *dstp, int *dmap, const bool ucubic,
	const bool cost3)
{
	const unsigned char *src3p = srcp-3*pitch;
	const unsigned char *src1p = srcp-1*pitch;
	const unsigned char *src1n = srcp+1*pitch;
	const unsigned char *src3n = srcp+3*pitch;
	const int tpitch = mdis*4+1;
	float *ccosts = temp;
	float *pcosts = ccosts+width*tpitch;
	int *pbackt = (int*)(pcosts+width*tpitch);
	int *fpath = pbackt+width*tpitch;
	// calculate half pel values
	unsigned char *hp3p = (unsigned char*)fpath;
	unsigned char *hp1p = hp3p+width;
	unsigned char *hp1n = hp1p+width;
	unsigned char *hp3n = hp1n+width;
	for (int x=0; x<width-1; ++x)
	{
		if (!ucubic || (x == 0 || x == width-2))
		{
			hp3p[x] = (src3p[x]+src3p[x+1]+1)>>1;
			hp1p[x] = (src1p[x]+src1p[x+1]+1)>>1;
			hp1n[x] = (src1n[x]+src1n[x+1]+1)>>1;
			hp3n[x] = (src3n[x]+src3n[x+1]+1)>>1;
		}
		else
		{
			hp3p[x] = std::min(std::max((36*(src3p[x]+src3p[x+1])-4*(src3p[x-1]+src3p[x+2])+32)>>6,0),255);
			hp1p[x] = std::min(std::max((36*(src1p[x]+src1p[x+1])-4*(src1p[x-1]+src1p[x+2])+32)>>6,0),255);
			hp1n[x] = std::min(std::max((36*(src1n[x]+src1n[x+1])-4*(src1n[x-1]+src1n[x+2])+32)>>6,0),255);
			hp3n[x] = std::min(std::max((36*(src3n[x]+src3n[x+1])-4*(src3n[x-1]+src3n[x+2])+32)>>6,0),255);
		}
	}
	// calculate all connection costs
	if (!cost3)
	{
		for (int x=0; x<width; ++x)
		{
			const int umax = std::min(std::min(x,width-1-x),mdis);
			for (int u=-umax*2; u<=umax*2; ++u)
			{
				int s = 0, ip;
				const int u2 = u>>1;
				if (!(u&1))
				{
					for (int k=-nrad; k<=nrad; ++k)
						s += 
							abs(src3p[x+u2+k]-src1p[x-u2+k])+
							abs(src1p[x+u2+k]-src1n[x-u2+k])+
							abs(src1n[x+u2+k]-src3n[x-u2+k]);
					ip = (src1p[x+u2]+src1n[x-u2]+1)>>1; // should use cubic if ucubic=true
				}
				else
				{
					for (int k=-nrad; k<=nrad; ++k)
						s += 
							abs(hp3p[x+u2+k]-hp1p[x-u2-1+k])+
							abs(hp1p[x+u2+k]-hp1n[x-u2-1+k])+
							abs(hp1n[x+u2+k]-hp3n[x-u2-1+k]);
					ip = (hp1p[x+u2]+hp1n[x-u2-1]+1)>>1; // should use cubic if ucubic=true
				}
				const int v = abs(src1p[x]-ip)+abs(src1n[x]-ip);
				ccosts[x*tpitch+mdis*2+u] = alpha*s+beta*abs(u)*0.5f+(1.0f-alpha-beta)*v;
			}
		}
	}
	else
	{
		for (int x=0; x<width; ++x)
		{
			const int umax = std::min(std::min(x,width-1-x),mdis);
			for (int u=-umax*2; u<=umax*2; ++u)
			{
				int s0 = 0, s1 = -1, s2 = -1, ip;
				const int u2 = u>>1;
				if (!(u&1))
				{
					for (int k=-nrad; k<=nrad; ++k)
						s0 += 
							abs(src3p[x+u2+k]-src1p[x-u2+k])+
							abs(src1p[x+u2+k]-src1n[x-u2+k])+
							abs(src1n[x+u2+k]-src3n[x-u2+k]);
					ip = (src1p[x+u2]+src1n[x-u2]+1)>>1; // should use cubic if ucubic=true
				}
				else
				{
					for (int k=-nrad; k<=nrad; ++k)
						s0 += 
							abs(hp3p[x+u2+k]-hp1p[x-u2-1+k])+
							abs(hp1p[x+u2+k]-hp1n[x-u2-1+k])+
							abs(hp1n[x+u2+k]-hp3n[x-u2-1+k]);
					ip = (hp1p[x+u2]+hp1n[x-u2-1]+1)>>1; // should use cubic if ucubic=true
				}
				if ((u >= 0 && x >= u) || (u <= 0 && x < width+u))
				{
					s1 = 0;
					for (int k=-nrad; k<=nrad; ++k)
						s1 += 
							abs(src3p[x+k]-src1p[x-u+k])+
							abs(src1p[x+k]-src1n[x-u+k])+
							abs(src1n[x+k]-src3n[x-u+k]);
				}
				if ((u <= 0 && x >= u) || (u >= 0 && x < width+u))
				{
					s2 = 0;
					for (int k=-nrad; k<=nrad; ++k)
						s2 += 
							abs(src3p[x+u+k]-src1p[x+k])+
							abs(src1p[x+u+k]-src1n[x+k])+
							abs(src1n[x+u+k]-src3n[x+k]);
				}
				s1 = s1 >= 0 ? s1 : (s2 >= 0 ? s2 : s0);
				s2 = s2 >= 0 ? s2 : (s1 >= 0 ? s1 : s0);
				const int v = abs(src1p[x]-ip)+abs(src1n[x]-ip);
				ccosts[x*tpitch+mdis*2+u] = alpha*(s0+s1+s2)*0.333333f+beta*abs(u)*0.5f+(1.0f-alpha-beta)*v;
			}
		}
	}
	// calculate path costs
	pcosts[mdis*2] = ccosts[mdis*2];
	for (int x=1; x<width; ++x)
	{
		float *tT = ccosts+x*tpitch;
		float *ppT = pcosts+(x-1)*tpitch;
		float *pT = pcosts+x*tpitch;
		int *piT = pbackt+(x-1)*tpitch;
		const int umax = std::min(std::min(x,width-1-x),mdis);
		for (int u=-umax*2; u<=umax*2; ++u)
		{
			int idx;
			float bval = FLT_MAX;
			const int umax2 = std::min(std::min(x-1,width-x),mdis);
			for (int v=std::max(-umax2*2,u-2); v<=std::min(umax2*2,u+2); ++v)
			{
				const double y = ppT[mdis*2+v]+gamma*abs(u-v)*0.5f;
				const float ccost = (float)std::min(y,FLT_MAX*0.9);
				if (ccost < bval)
				{
					bval = ccost;
					idx = v;
				}
			}
			const double y = bval+tT[mdis*2+u];
			pT[mdis*2+u] = (float)std::min(y,FLT_MAX*0.9);
			piT[mdis*2+u] = idx;
		}
	}
	// backtrack
	fpath[width-1] = 0;
	for (int x=width-2; x>=0; --x)
		fpath[x] = pbackt[x*tpitch+mdis*2+fpath[x+1]];
	// interpolate
	for (int x=0; x<width; ++x)
	{
		const int dir = fpath[x];
		dmap[x] = dir;
		if (!(dir&1))
		{
			const int d2 = dir>>1;
			const int ad = abs(d2);
			if (ucubic && x >= ad*3 && x <= width-1-ad*3)
				dstp[x] = std::min(std::max((36*(src1p[x+d2]+src1n[x-d2])-
					4*(src3p[x+d2*3]+src3n[x-d2*3])+32)>>6,0),255);
			else
				dstp[x] = (src1p[x+d2]+src1n[x-d2]+1)>>1;
		}
		else
		{
			const int d20 = dir>>1;
			const int d21 = (dir+1)>>1;
			const int d30 = (dir*3)>>1;
			const int d31 = (dir*3+1)>>1;
			const int ad = std::max(abs(d30),abs(d31));
			if (ucubic && x >= ad && x <= width-1-ad)
			{
				const int c0 = src3p[x+d30]+src3p[x+d31];
				const int c1 = src1p[x+d20]+src1p[x+d21]; // should use cubic if ucubic=true
				const int c2 = src1n[x-d20]+src1n[x-d21]; // should use cubic if ucubic=true
				const int c3 = src3n[x-d30]+src3n[x-d31];
				dstp[x] = std::min(std::max((36*(c1+c2)-4*(c0+c3)+64)>>7,0),255);
			}
			else
				dstp[x] = (src1p[x+d20]+src1p[x+d21]+src1n[x-d20]+src1n[x-d21]+2)>>2;
		}
	}
}

const VSFrameRef *VS_CC eedi3::eedi3GetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
	eedi3 *d = (eedi3 *)*instanceData;
	return d->getFrame(n, activationReason, frameCtx, core, vsapi);
}

const VSFrameRef *eedi3::getFrame(int n, int activationReason, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
	if (activationReason == arInitial) {
		vsapi->requestFrameFilter(field>1?(n>>1):n, child, frameCtx);
		if (sclip)
			vsapi->requestFrameFilter(n, sclip, frameCtx);
	} else if (activationReason == arAllFramesReady) {

		int field_n;
		if (field > 1)
		{
			if (n&1) field_n = field == 3 ? 0 : 1;
			else field_n = field == 3 ? 1 : 0;
		}
		else field_n = field;
		const VSFrameRef *src = vsapi->getFrameFilter(field>1?(n>>1):n, child, frameCtx);
		VSFrameRef *srcPF = copyPad(src,field_n,frameCtx,core,vsapi);

		const VSFrameRef *scpPF;
		if (vcheck > 0 && sclip)
			scpPF = vsapi->getFrameFilter(n, sclip, frameCtx);
		else scpPF = NULL;

		// fixme,  adjust duration
		VSFrameRef *dst = vsapi->newVideoFrame(vi.format, vi.width, vi.height, src, core);
		float *workspace = vs_aligned_malloc<float>(vi.width*std::max(mdis*4+1,16)*4*sizeof(float),16);
		int *dmapa = vs_aligned_malloc<int>(vsapi->getStride(dst, 0)*vsapi->getFrameHeight(dst, 0)*sizeof(int),16);
		vsapi->freeFrame(src);

		for (int b=0; b<vi.format->numPlanes; ++b)
		{
			if (!(planes & (1 << b)))
				continue;
			const unsigned char *srcp = vsapi->getReadPtr(srcPF, b);
			const int spitch = vsapi->getStride(srcPF, b);
			const int width = vsapi->getFrameWidth(dst, b) + 24;
			const int height = vsapi->getFrameHeight(dst, b) + 8;
			unsigned char *dstp = vsapi->getWritePtr(dst, b);
			const int dpitch = vsapi->getStride(dst, b);
			bitblt(dstp+(1-field_n)*dpitch,
				dpitch*2,srcp+(4+1-field_n)*spitch+12,
				spitch*2,width-24,(height-8)>>1);
			srcp += (4+field_n)*spitch;
			dstp += field_n*dpitch;
			// ~99% of the processing time is spent in this loop
			for (int y=4+field_n; y<height-4; y+=2)
			{
				const int off = (y-4-field_n)>>1;
				if (hp)
					interpLineHP(srcp+12+off*2*spitch,width-24,spitch,alpha,beta,
						gamma,nrad,mdis,workspace,dstp+off*2*dpitch,
						dmapa+off*dpitch,ucubic,cost3);
				else
					interpLineFP(srcp+12+off*2*spitch,width-24,spitch,alpha,beta,
						gamma,nrad,mdis,workspace,dstp+off*2*dpitch,
						dmapa+off*dpitch,ucubic,cost3);
			}
			if (vcheck > 0)
			{
				int *dstpd = dmapa;
				const unsigned char *scpp = NULL;
				int scpitch;
				if (sclip)
				{
					scpitch = vsapi->getStride(scpPF, b);
					scpp = vsapi->getReadPtr(scpPF, b)+field_n*scpitch;
				}
				for (int y=4+field_n; y<height-4; y+=2)
				{
					if (y >= 6 && y < height-6)
					{
						const unsigned char *dst3p = srcp-3*spitch+12;
						const unsigned char *dst2p = dstp-2*dpitch;
						const unsigned char *dst1p = dstp-1*dpitch;
						const unsigned char *dst1n = dstp+1*dpitch;
						const unsigned char *dst2n = dstp+2*dpitch;
						const unsigned char *dst3n = srcp+3*spitch+12;
						unsigned char *tline = (unsigned char*)workspace;
						for (int x=0; x<width-24; ++x)
						{
							const int dirc = dstpd[x];
							const int cint = scpp ? scpp[x] : 
								std::min(std::max((36*(dst1p[x]+dst1n[x])-4*(dst3p[x]+dst3n[x])+32)>>6,0),255);
							if (dirc == 0)
							{
								tline[x] = cint;
								continue;
							}
							const int dirt = dstpd[x-dpitch];
							const int dirb = dstpd[x+dpitch];
							if (std::max(dirc*dirt,dirc*dirb) < 0 || (dirt == dirb && dirt == 0))
							{
								tline[x] = cint;
								continue;
							}
							int it, ib, vt, vb,vc;
							vc = abs(dstp[x]-dst1p[x])+abs(dstp[x]-dst1n[x]);
							if (hp)
							{
								if (!(dirc&1))
								{
									const int d2 = dirc>>1;
									it = (dst2p[x+d2]+dstp[x-d2]+1)>>1;
									vt = abs(dst2p[x+d2]-dst1p[x+d2])+abs(dstp[x+d2]-dst1p[x+d2]);
									ib = (dstp[x+d2]+dst2n[x-d2]+1)>>1;
									vb = abs(dst2n[x-d2]-dst1n[x-d2])+abs(dstp[x-d2]-dst1n[x-d2]);
								}
								else
								{
									const int d20 = dirc>>1;
									const int d21 = (dirc+1)>>1;
									const int pa2p = dst2p[x+d20]+dst2p[x+d21]+1;
									const int pa1p = dst1p[x+d20]+dst1p[x+d21]+1;
									const int ps0 = dstp[x-d20]+dstp[x-d21]+1;
									const int pa0 = dstp[x+d20]+dstp[x+d21]+1;
									const int ps1n = dst1n[x-d20]+dst1n[x-d21]+1;
									const int ps2n = dst2n[x-d20]+dst2n[x-d21]+1;
									it = (pa2p+ps0)>>2;
									vt = (abs(pa2p-pa1p)+abs(pa0-pa1p))>>1;
									ib = (pa0+ps2n)>>2;
									vb = (abs(ps2n-ps1n)+abs(ps0-ps1n))>>1;
								}
							}
							else
							{
								it = (dst2p[x+dirc]+dstp[x-dirc]+1)>>1;
								vt = abs(dst2p[x+dirc]-dst1p[x+dirc])+abs(dstp[x+dirc]-dst1p[x+dirc]);
								ib = (dstp[x+dirc]+dst2n[x-dirc]+1)>>1;
								vb = abs(dst2n[x-dirc]-dst1n[x-dirc])+abs(dstp[x-dirc]-dst1n[x-dirc]);
							}
							const int d0 = abs(it-dst1p[x]);
							const int d1 = abs(ib-dst1n[x]);
							const int d2 = abs(vt-vc);
							const int d3 = abs(vb-vc);
							const int mdiff0 = vcheck == 1 ? std::min(d0,d1) : vcheck == 2 ? ((d0+d1+1)>>1) : std::max(d0,d1);
							const int mdiff1 = vcheck == 1 ? std::min(d2,d3) : vcheck == 2 ? ((d2+d3+1)>>1) : std::max(d2,d3);
							const float a0 = mdiff0/vthresh0;
							const float a1 = mdiff1/vthresh1;
							const int dircv = hp ? (abs(dirc)>>1) : abs(dirc);
							const float a2 = std::max((vthresh2-dircv)/vthresh2,0.0f);
							const float a = std::min(std::max(std::max(a0,a1),a2),1.0f);
							tline[x] = (int)((1.0-a)*dstp[x]+a*cint);
						}
						memcpy(dstp,tline,width-24);
					}
					srcp += 2*spitch;
					dstp += 2*dpitch;
					if (scpp)
						scpp += 2*scpitch;
					dstpd += dpitch;
				}
			}
		}

		vs_aligned_free(dmapa);
		vs_aligned_free(workspace);
		vsapi->freeFrame(srcPF);
		vsapi->freeFrame(scpPF);
		return dst;
	}

	return NULL;
}

VSFrameRef *eedi3::copyPad(const VSFrameRef *src, int fn, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi)
{
	const int off = 1-fn;
	VSFrameRef *srcPF = vsapi->newVideoFrame(vi.format, vi.width + 24 * (1 << vi.format->subSamplingW), vi.height + 8 * (1 << vi.format->subSamplingH), NULL, core);

	if (!dh)
	{
		for (int b=0; b<vi.format->numPlanes; ++b)
			bitblt(vsapi->getWritePtr(srcPF, b)+vsapi->getStride(srcPF, b)*(4+off)+12,
				vsapi->getStride(srcPF, b)*2,
				vsapi->getReadPtr(src, b)+vsapi->getStride(src, b)*off,
				vsapi->getStride(src, b)*2,
				vsapi->getFrameWidth(src, b) * vi.format->bytesPerSample,
				vsapi->getFrameHeight(src, b)>>1);
	}
	else
	{
		for (int b=0; b<vi.format->numPlanes; ++b)
			bitblt(vsapi->getWritePtr(srcPF, b)+vsapi->getStride(srcPF, b)*(4+off)+12,
				vsapi->getStride(srcPF, b)*2,
				vsapi->getReadPtr(src, b),
				vsapi->getStride(src, b),
				vsapi->getFrameWidth(src, b) * vi.format->bytesPerSample,
				vsapi->getFrameHeight(src, b));
	}
	for (int b=0; b<vi.format->numPlanes; ++b)
	{
		// fixme, probably pads a bit too much with subsampled formats
		unsigned char *dstp = vsapi->getWritePtr(srcPF, b);
		const int dst_pitch = vsapi->getStride(srcPF, b);
		const int height = vsapi->getFrameHeight(src, b) + 8;
		const int width = vsapi->getFrameWidth(src, b) + 24;
		dstp += (4+off)*dst_pitch;
		for (int y=4+off; y<height-4; y+=2)
		{
			for (int x=0; x<12; ++x)
				dstp[x] = dstp[24-x];
			int c = 2;
			for (int x=width-12; x<width; ++x, c+=2)
				dstp[x] = dstp[x-c]; 
			dstp += dst_pitch*2;
		}
		dstp = vsapi->getWritePtr(srcPF, b);
		for (int y=off; y<4; y+=2)
			bitblt(dstp+y*dst_pitch,dst_pitch,
				dstp+(8-y)*dst_pitch,dst_pitch,width,1);
		int c = 2+2*off;
		for (int y=height-4+off; y<height; y+=2, c+=4)
			bitblt(dstp+y*dst_pitch,dst_pitch,
				dstp+(y-c)*dst_pitch,dst_pitch,width,1);
	}

	return srcPF;
}

void VS_CC Create_eedi3(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi)
{
	int err;
	const VSNodeRef *child = vsapi->propGetNode(in, "clip", 0, NULL);
	const VSVideoInfo *vi = vsapi->getVideoInfo(child);
	int field = vsapi->propGetInt(in, "field", 0, NULL);
	bool dh = !!vsapi->propGetInt(in, "dh", 0, &err);
	double alpha = vsapi->propGetFloat(in, "alpha", 0, &err);
	if (err)
		alpha = 0.2;
	double beta = vsapi->propGetFloat(in, "beta", 0, &err);
	if (err)
		beta = 0.25;
	double gamma = vsapi->propGetFloat(in, "gamma", 0, &err);
	if (err)
		gamma = 20;
	int nrad = vsapi->propGetInt(in, "nrad", 0, &err);
	if (err)
		nrad = 2;
	int mdis = vsapi->propGetInt(in, "mdis", 0, &err);
	if (err)
		mdis = 20;
	bool hp = !!vsapi->propGetInt(in, "hp", 0, &err);
	bool ucubic = !!vsapi->propGetInt(in, "ucubic", 0, &err);
	if (err)
		ucubic = true;
	bool cost3 = !!vsapi->propGetInt(in, "cost3", 0, &err);
	if (err)
		cost3 = true;
	int vcheck = vsapi->propGetInt(in, "vcheck", 0, &err);
	if (err)
		vcheck = 2;
	double vthresh0 = vsapi->propGetFloat(in, "vthresh0", 0, &err);
	if (err)
		vthresh0 = 32;
	double vthresh1 = vsapi->propGetFloat(in, "vthresh1", 0, &err);
	if (err)
		vthresh1 = 64;
	double vthresh2 = vsapi->propGetFloat(in, "vthresh2", 0, &err);
	if (err)
		vthresh2 = 4;
	const VSNodeRef *sclip = vsapi->propGetNode(in, "sclip", 0, &err);

	int planes = 0;
	int nump = vsapi->propNumElements(in, "planes");
	if (nump <= 0)
		planes = -1;
	else
		for (int i = 0; i < nump; i++)
			planes |= 1 << vsapi->propGetInt(in, "planes", i, NULL);

	try {
		if (vi->format->bytesPerSample != 1)
			throw std::runtime_error(std::string("eedi3:  only 8bits per sample input supported"));

		if ((vi->height&1) && !dh)
			throw std::runtime_error(std::string("eedi3:  height must be mod 2 when dh=false!"));
		eedi3 *instance = new eedi3(child,field,dh,
			planes,
			alpha,beta,gamma,
			nrad,mdis,hp,ucubic,
			cost3,vcheck, vthresh0,
			vthresh1,vthresh2,sclip,
			vsapi);
		const VSNodeRef *cref = vsapi->createFilter(in, out, "eedi3", eedi3::eedi3Init, eedi3::eedi3GetFrame, eedi3::eedi3Free, fmParallel, 0, instance, core);
		vsapi->propSetNode(out, "clip", cref, 0);
		vsapi->freeNode(cref);
	} catch (const std::exception &e) {
		vsapi->freeNode(sclip);
		vsapi->freeNode(child);
		vsapi->setError(out, e.what());
	}
}

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin)
{
	configFunc("com.vapoursynth.eedi3", "eedi3", "EEDI3", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("eedi3", "clip:clip;field:int;dh:int:opt;planes:int[]:opt;alpha:float:opt;beta:float:opt;gamma:float:opt;nrad:int:opt;mdis:int:opt;" \
		"hp:int:opt;ucubic:int:opt;cost3:int:opt;vcheck:int:opt;vthresh0:float:opt;vthresh1:float:opt;vthresh2:float:opt;sclip:clip:opt;", 
		Create_eedi3, NULL, plugin);
}
