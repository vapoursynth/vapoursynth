//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2001 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"

#include "DubSource.h"

DubSource::DubSource() {
	format = NULL;
}

DubSource::~DubSource() {
	if (format) delete format;
}

BOOL DubSource::init() {
	return TRUE;
}

void *DubSource::allocFormat(int format_len) {
	if (format) delete format;

	return format = (void *)new char[this->format_len = format_len];
}

bool DubSource::isStreaming() {
	return false;
}

int DubSource::read(LONG lStart, LONG lCount, LPVOID lpBuffer, LONG cbBuffer, LONG *lBytesRead, LONG *lSamplesRead) {
	if (lStart < lSampleFirst) return AVIERR_BADPARAM;
	if (lStart >= lSampleLast) {
		if (lSamplesRead)
			*lSamplesRead = 0;
		if (lBytesRead)
			*lBytesRead = 0;
		return 0;
	}

	if (lCount>0 && lCount > lSampleLast - lStart) lCount = lSampleLast - lStart;

	return _read(lStart, lCount, lpBuffer, cbBuffer, lBytesRead, lSamplesRead);
}

BOOL DubSource::isKey(LONG lSample) {
	if (lSample<lSampleFirst || lSample>=lSampleLast) return TRUE;

	return _isKey(lSample);
}

BOOL DubSource::_isKey(LONG lSample) {
	return TRUE;
}

LONG DubSource::nearestKey(LONG lSample) {
	return lSample;
}

LONG DubSource::prevKey(LONG lSample) {
	return lSample <= lSampleFirst ? -1 : lSample-1;
}

LONG DubSource::nextKey(LONG lSample) {
	return lSample+1 >= lSampleFirst ? -1 : lSample+1;
}

void DubSource::streamBegin(bool fRealTime) {
}

void DubSource::streamEnd() {
}

