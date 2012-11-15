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

#include "../stdafx.h"

//#include "gui.h"
#include "AudioSource.h"
#include "AVIReadHandler.h"

//extern HWND g_hWnd;		// TODO: Remove in 1.5.0

#pragma warning(disable: 4018)    // signed/unsigned mismatch


AudioSourceWAV::AudioSourceWAV(wchar_t *szFile, LONG inputBufferSize) {
	MMIOINFO mmi;

	memset(&mmi,0,sizeof mmi);
	mmi.cchBuffer	= inputBufferSize;
	hmmioFile		= mmioOpen(szFile, &mmi, MMIO_READ | MMIO_ALLOCBUF);
}

AudioSourceWAV::~AudioSourceWAV() {
	mmioClose(hmmioFile, 0);
}

BOOL AudioSourceWAV::init() {
	if (!hmmioFile) return FALSE;

	chunkRIFF.fccType = mmioFOURCC('W','A','V','E');
	if (MMSYSERR_NOERROR != mmioDescend(hmmioFile, &chunkRIFF, NULL, MMIO_FINDRIFF))
		return FALSE;

	chunkDATA.ckid = mmioFOURCC('f','m','t',' ');
	if (MMSYSERR_NOERROR != mmioDescend(hmmioFile, &chunkDATA, &chunkRIFF, MMIO_FINDCHUNK))
		return FALSE;

	if (!allocFormat(chunkDATA.cksize)) return FALSE;
	if (chunkDATA.cksize != mmioRead(hmmioFile, (char *)getWaveFormat(), chunkDATA.cksize))
		return FALSE;

	if (MMSYSERR_NOERROR != mmioAscend(hmmioFile, &chunkDATA, 0))
		return FALSE;

	chunkDATA.ckid = mmioFOURCC('d','a','t','a');
	if (MMSYSERR_NOERROR != mmioDescend(hmmioFile, &chunkDATA, &chunkRIFF, MMIO_FINDCHUNK))
		return FALSE;

	bytesPerSample	= getWaveFormat()->nBlockAlign; //getWaveFormat()->nAvgBytesPerSec / getWaveFormat()->nSamplesPerSec;
	lSampleFirst	= 0;
	lSampleLast		= chunkDATA.cksize / bytesPerSample;
	lCurrentSample	= 0;

	streamInfo.fccType					= streamtypeAUDIO;
	streamInfo.fccHandler				= 0;
	streamInfo.dwFlags					= 0;
	streamInfo.wPriority				= 0;
	streamInfo.wLanguage				= 0;
	streamInfo.dwInitialFrames			= 0;
	streamInfo.dwScale					= bytesPerSample;
	streamInfo.dwRate					= getWaveFormat()->nAvgBytesPerSec;
	streamInfo.dwStart					= 0;
	streamInfo.dwLength					= chunkDATA.cksize / bytesPerSample;
	streamInfo.dwSuggestedBufferSize	= 0;
	streamInfo.dwQuality				= 0xffffffff;
	streamInfo.dwSampleSize				= bytesPerSample;

	return TRUE;
}

int AudioSourceWAV::_read(LONG lStart, LONG lCount, LPVOID buffer, LONG cbBuffer, LONG *lBytesRead, LONG *lSamplesRead) {
	LONG lBytes = lCount * bytesPerSample;
	
	if (buffer) {
		if (lStart != lCurrentSample)
			if (-1 == mmioSeek(hmmioFile, chunkDATA.dwDataOffset + bytesPerSample*lStart, SEEK_SET))
				return AVIERR_FILEREAD;

		if (lBytes != mmioRead(hmmioFile, (char *)buffer, lBytes))
			return AVIERR_FILEREAD;

		lCurrentSample = lStart + lCount;
	}

	*lSamplesRead = lCount;
	*lBytesRead = lBytes;

	return AVIERR_OK;
}

///////////////////////////

AudioSourceAVI::AudioSourceAVI(IAVIReadHandler *pAVI, bool bAutomated) {
	pAVIFile	= pAVI;
	pAVIStream	= NULL;
	bQuiet = bAutomated;	// ugh, this needs to go... V1.5.0.
}

AudioSourceAVI::~AudioSourceAVI() {
	if (pAVIStream)
		delete pAVIStream;
}

BOOL AudioSourceAVI::init() {
	LONG format_len;

	pAVIStream = pAVIFile->GetStream(streamtypeAUDIO, 0);
	if (!pAVIStream) return FALSE;

	if (pAVIStream->Info(&streamInfo))
		return FALSE;

	pAVIStream->FormatSize(0, &format_len);

	if (!allocFormat(format_len)) return FALSE;

	if (pAVIStream->ReadFormat(0, getFormat(), &format_len))
		return FALSE;

	lSampleFirst = pAVIStream->Start();
	lSampleLast = pAVIStream->End();

	if (!bQuiet) {
		double mean, stddev, maxdev;

		if (pAVIStream->getVBRInfo(mean, stddev, maxdev)) {
/*			guiMessageBoxF(g_hWnd, "VBR audio stream detected", MB_OK|MB_TASKMODAL|MB_SETFOREGROUND,
				"VirtualDub has detected an improper VBR audio encoding in the source AVI file and will rewrite the audio header "
				"with standard CBR values during processing for better compatibility. This may introduce up to %.0lf ms of skew "
				"from the video stream. If this is unacceptable, decompress the *entire* audio stream to an uncompressed WAV file "
				"and recompress with a constant bitrate encoder. "
				"(bitrate: %.1lf ± %.1lf kbps)"
				,maxdev*1000.0,mean*0.001, stddev*0.001);
 */
		}
	}

	return TRUE;
}

void AudioSourceAVI::Reinit() {
	pAVIStream->Info(&streamInfo);
	lSampleFirst = pAVIStream->Start();
	lSampleLast = pAVIStream->End();
}

bool AudioSourceAVI::isStreaming() {
	return pAVIStream->isStreaming();
}

void AudioSourceAVI::streamBegin(bool fRealTime) {
	pAVIStream->BeginStreaming(lSampleFirst, lSampleLast, fRealTime ? 1000 : 2000);
}

void AudioSourceAVI::streamEnd() {
	pAVIStream->EndStreaming();

}

BOOL AudioSourceAVI::_isKey(LONG lSample) {
	return pAVIStream->IsKeyFrame(lSample);
}
int AudioSourceAVI::_read(LONG lStart, LONG lCount, LPVOID lpBuffer, LONG cbBuffer, LONG *lpBytesRead, LONG *lpSamplesRead) {
	int err;
	long lBytes, lSamples;

	// There are some video clips roaming around with truncated audio streams
	// (audio streams that state their length as being longer than they
	// really are).  We use a kludge here to get around the problem.

	err = pAVIStream->Read(lStart, lCount, lpBuffer, cbBuffer, lpBytesRead, lpSamplesRead);

	if (err != AVIERR_FILEREAD)
		return err;

	// Suspect a truncated stream.
	//
	// AVISTREAMREAD_CONVENIENT will tell us if we're actually encountering a
	// true read error or not.  At least for the AVI handler, it returns
	// AVIERR_ERROR if we've broached the end.  

	*lpBytesRead = *lpSamplesRead = 0;

	while(lCount > 0) {
		err = pAVIStream->Read(lStart, AVISTREAMREAD_CONVENIENT, NULL, 0, &lBytes, &lSamples);

		if (err)
			return 0;

		if (!lSamples) return AVIERR_OK;

		if (lSamples > lCount) lSamples = lCount;

		err = pAVIStream->Read(lStart, lSamples, lpBuffer, cbBuffer, &lBytes, &lSamples);

		if (err)
			return err;

		lpBuffer = (LPVOID)((char *)lpBuffer + lBytes);
		cbBuffer -= lBytes;
		lCount -= lSamples;

		*lpBytesRead += lBytes;
		*lpSamplesRead += lSamples;
	}

	return AVIERR_OK;
}
