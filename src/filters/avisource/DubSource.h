//    VirtualDub - Video processing and capture application
//    Copyright (C) 1998-2001 Avery Lee
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_DUBSOURCE_H
#define f_DUBSOURCE_H

#include <vd2/Riza/avi.h>

class InputFile;

class DubSource {
private:
    void *    format;
    int        format_len;

protected:
    void *allocFormat(int format_len);
    virtual BOOL _isKey(LONG lSample);

public:
    VDPosition lSampleFirst, lSampleLast;
    VDAVIStreamInfo    streamInfo;

    DubSource();
    virtual ~DubSource();

    virtual BOOL init();
    int read(LONG lStart, LONG lCount, LPVOID lpBuffer, LONG cbBuffer, LONG *lBytesRead, LONG *lSamplesRead);
    virtual int _read(LONG lStart, LONG lCount, LPVOID lpBuffer, LONG cbBuffer, LONG *lBytesRead, LONG *lSamplesRead) = 0;

    void *getFormat() const { return format; }
    int getFormatLen() const { return format_len; }

    virtual bool isStreaming();

    BOOL isKey(LONG lSample);
    virtual LONG nearestKey(LONG lSample);
    virtual LONG prevKey(LONG lSample);
    virtual LONG nextKey(LONG lSample);

    virtual void streamBegin( bool fRealTime);
    virtual void streamEnd();

    LONG msToSamples(LONG lMs) const {
        return (LONG)(((__int64)lMs * streamInfo.dwRate + (__int64)500 * streamInfo.dwScale) / ((__int64)1000 * streamInfo.dwScale));
    }
    LONG samplesToMs(LONG lSamples) const {
        return (LONG)(
                (((__int64)lSamples * streamInfo.dwScale) * 1000 + streamInfo.dwRate/2) / streamInfo.dwRate
            );
    }

    // This is more accurate than AVIStreamSampleToSample(), which does a conversion to
    // milliseconds and back.

    static LONG samplesToSamples(const VDAVIStreamInfo *dest, const VDAVIStreamInfo *source, LONG lSamples) {
        __int64 divisor = (__int64)source->dwRate * dest->dwScale;

        return (LONG)((((__int64)lSamples * source->dwScale) * dest->dwRate + divisor/2)
                / divisor);
    }

    LONG samplesToSamples(const DubSource *source, LONG lSamples) const {
        return samplesToSamples(&streamInfo, &source->streamInfo, lSamples);
    }
};

#endif
