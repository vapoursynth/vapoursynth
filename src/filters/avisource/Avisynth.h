// Avisynth v1.0 beta.  Copyright 2000 Ben Rudiak-Gould.
// http://www.math.berkeley.edu/~benrg/avisynth.html

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA, or visit
// http://www.gnu.org/copyleft/gpl.html .

#ifndef f_AVISYNTH_H
#define f_AVISYNTH_H

extern "C" const GUID CLSID_CAVIFileSynth   // {E6D6B700-124D-11D4-86F3-DB80AFD98778}
  = {0xe6d6b700, 0x124d, 0x11d4, {0x86, 0xf3, 0xdb, 0x80, 0xaf, 0xd9, 0x87, 0x78}};

extern "C" const GUID IID_IAvisynthClipInfo   // {E6D6B708-124D-11D4-86F3-DB80AFD98778}
  = {0xe6d6b708, 0x124d, 0x11d4, {0x86, 0xf3, 0xdb, 0x80, 0xaf, 0xd9, 0x87, 0x78}};


struct IAvisynthClipInfo : IUnknown {
  virtual int __stdcall GetError(const char** ppszMessage) = 0;
  virtual bool __stdcall GetParity(int n) = 0;
  virtual bool __stdcall IsFieldBased() = 0;
};

#endif
