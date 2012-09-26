/*
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

#include <stdexcept>
#include <algorithm>
#include "VapourSynth.h"
#include "vshelper.h"

class eedi3
{
private:
	const VSNodeRef *child;
	const VSNodeRef *sclip;

	bool dh, hp, ucubic, cost3;
	int planes;
	float alpha, beta, gamma,  vthresh0, vthresh1, vthresh2;
	int field, nrad, mdis, vcheck;

	VSVideoInfo vi;
	VSFrameRef *copyPad(const VSFrameRef *src, int fn, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);

public:
	eedi3(const VSNodeRef *child, int _field, bool _dh, int planes, 
		float _alpha, float _beta, float _gamma, int _nrad, int _mdis, bool _hp, 
		bool _ucubic, bool _cost3, int _vcheck, float _vthresh0, float _vthresh1, 
		float _vthresh2, const VSNodeRef *sclip, const VSAPI *vsapi);
	static void VS_CC eedi3Init(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi);
	static const VSFrameRef *VS_CC eedi3GetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
	static void VS_CC eedi3Free(void *instanceData, VSCore *core, const VSAPI *vsapi);
	const VSFrameRef *getFrame(int n, int activationReason, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi);
};
