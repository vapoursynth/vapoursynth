// avfs.cpp : Avisynth Virtual File System
//
// Avisynth v2.5.  Copyright 2008 Ben Rudiak-Gould et al.
// http://www.avisynth.org

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
//
// Linking Avisynth statically or dynamically with other modules is making a
// combined work based on Avisynth.  Thus, the terms and conditions of the GNU
// General Public License cover the whole combination.
//
// As a special exception, the copyright holders of Avisynth give you
// permission to link Avisynth with independent modules that communicate with
// Avisynth solely through the interfaces defined in avisynth.h, regardless of the license
// terms of these independent modules, and to copy and distribute the
// resulting combined work under terms of your choice, provided that
// every copy of the combined work is accompanied by a complete copy of
// the source code of Avisynth (the version of Avisynth used to produce the
// combined work), being distributed under the terms of the GNU General
// Public License plus this exception.  An independent module is a module
// which is not derived from or based on Avisynth, such as 3rd-party filters,
// import and export plugins, or graphical user interfaces.


#include "avfsincludes.h"

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#define NOMINMAX
#include <windows.h>

const AVS_Linkage *AVS_linkage = nullptr;

/*---------------------------------------------------------
---------------------------------------------------------*/

typedef IScriptEnvironment* (__stdcall *ICreateScriptEnvironment)(int version);

class Avisynther final:
   public Avisynther_
{
  int references;

  // Function pointer
  ICreateScriptEnvironment CreateScriptEnvironment;

  // Avisynth.dll
  HMODULE hlib;

  IScriptEnvironment* env;

  PClip clip; // This is a smart pointer, make sure it is released before env gets zapped!

  std::wstring errText;

  VideoInfo vi;

  std::string lastStringValue;

  // Frame read ahead.
  HANDLE fraThread;
  CRITICAL_SECTION fraMutex;
  HANDLE fraResumeEvent;
  HANDLE fraSuspendedEvent;
  int fraPosition;
  int fraEndPosition;
  int fraSuspendCount;
  enum { fraDefaultFrameCount = 0 };
  enum { fraMaxFrameCount = 100 };
  int fraFrameCount;
  enum { fraMaxResumeDelay = 1000 };
  enum { fraDefaultResumeDelay = 10 };
  int fraResumeDelay;

  // Cache last accessed frame, to reduce interference with read-ahead.
  int lastPosition;
  PVideoFrame lastFrame;

  // Exception protected take a copy of the current error message
  void setError(const char *text, const wchar_t *alt = 0);

  // Retrieve the current avisynth error message
  const wchar_t* getError();

  // Exception protected refresh the IScriptEnvironment
  int/*error*/ newEnv();

  // Exception protected IScriptEnvironment->Invoke()
  AVSValue Invoke(const char* name, const AVSValue args);

  // (Re)Open the Script File
  int/*error*/ Import(const wchar_t* szScriptName);

  // Print the VideoInfo contents to the log file.
  void reportFormat(AvfsLog_* log);

  // Thread to read frames in background to better utilize multi core
  // systems.
  void FraThreadMain();
  static DWORD __stdcall FraThreadMainThunk(void* param);

public:

  // Exception protected clip->GetAudio()
  bool/*success*/ GetAudio(AvfsLog_* log, void* buf, __int64 start, unsigned count);

  // Exception protected PVideoFrame->GetFrame()
  PVideoFrame GetFrame(AvfsLog_* log, int n, bool *success=0);

  // Readonly reference to VideoInfo
  VideoInfoAdapter GetVideoInfo();

  // Read value of string variable from script. Returned pointer
  // is valid until next call, so copy if you need it long term.
  const char* GetVarAsString(const char* varName, const char* defVal);
  bool GetVarAsBool(const char* varName, bool defVal);
  int GetVarAsInt(const char* varName, int defVal);

  // Suspend/resume frame read ahead as necessary for
  // performance optimization.
  void FraSuspend();
  void FraResume();

  Avisynther(void);
  ~Avisynther(void);
  int/*error*/ Init(AvfsLog_* log,AvfsVolume_* volume);
  void AddRef(void);
  void Release(void);
};

/*---------------------------------------------------------
---------------------------------------------------------*/

// (Re)Open the Script File
int/*error*/ Avisynther::Import(const wchar_t* wszScriptName)
{
  int error = ERROR_OUTOFMEMORY;
  char* szScriptName = ssconvalloc(wszScriptName);
  if(szScriptName)
  {
    // Get a fresh IScriptEnvironment
    error = newEnv();

    if(!error)
    {
      // Load the script
      AVSValue var = Invoke("Import", szScriptName);

      if (var.Defined()) {
        if (var.IsClip()) {
          // Add a Cache to the graph
          var = Invoke("Cache", var);

          clip = var.AsClip();

          if (clip) {
            vi = clip->GetVideoInfo();
          }
        }
        else {
          setError("The script's return value was not a video clip.");
          error = ERROR_ACCESS_DENIED;
        }
      }
      else {
        error = ERROR_ACCESS_DENIED;
      }

    }
    free(szScriptName);
  }
  return error;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void Avisynther::reportFormat(AvfsLog_* log)
{
  if (vi.HasVideo()) {
    log->Print(L"Video stream :-\n");

	int msLen = (int)(1000.0 * vi.num_frames * vi.fps_denominator / vi.fps_numerator);
    log->Printf(L"  Duration: %8d frames, %02d:%02d:%02d.%03d\n", vi.num_frames,
                          (msLen/(60*60*1000)), (msLen/(60*1000))%60 ,(msLen/1000)%60, msLen%1000); 
    const char* c_space = "";
    if      (vi.IsYV12())  c_space = "YV12";
    else if (vi.IsYV16())  c_space = "YV16";
    else if (vi.IsYV24())  c_space = "YV24";
    else if (vi.IsYV411()) c_space = "YV411";
    else if (vi.IsY8())    c_space = "Y8";
    else if (vi.IsYUY2())  c_space = "YUY2";
    else if (vi.IsRGB32()) c_space = "RGB32";
    else if (vi.IsRGB24()) c_space = "RGB24";

    log->Printf(L"  ColorSpace: %hs\n", c_space);

    log->Printf(L"  Width:%4d pixels, Height:%4d pixels.\n", vi.width, vi.height);

    log->Printf(L"  Frames per second: %7.4f (%u/%u)\n", (double)vi.fps_numerator/vi.fps_denominator,
                                                        vi.fps_numerator, vi.fps_denominator);
    log->Printf(L"  FieldBased (Separated) Video: %hs\n", vi.IsFieldBased() ? "Yes" : "No");

    try {
      log->Printf(L"  Parity: %hs field first.\n", (clip->GetParity(0) ? "Top" : "Bottom"));
    }
    catch (...) { }

    const char* s_parity;
    if (vi.IsTFF() && vi.IsBFF())
      s_parity="Invalid";
    else if (vi.IsTFF())
      s_parity="Assumed Top Field First";
    else if (vi.IsBFF())
      s_parity="Assumed Bottom Field First";
    else
      s_parity="Unspecified";
    log->Printf(L"  Field order: %hs\n", s_parity);
  }
  else
    log->Print(L"No video stream.\n");

  if (vi.HasAudio()) {
    log->Print(L"Audio stream :-\n");

    int msLen = (int)(1000.0 * vi.num_audio_samples / vi.audio_samples_per_second);
    log->Printf(L"  Audio length: %I64u samples. %02d:%02d:%02d.%03d\n", vi.num_audio_samples,
  	                      (msLen/(60*60*1000)), (msLen/(60*1000))%60, (msLen/1000)%60, msLen%1000);
    log->Printf(L"  Samples Per Second: %5d\n", vi.audio_samples_per_second);
    log->Printf(L"  Audio Channels: %-8d\n", vi.AudioChannels());

    const char* s_type = "";
    if      (vi.SampleType()==SAMPLE_INT8)  s_type = "Integer 8 bit";
    else if (vi.SampleType()==SAMPLE_INT16) s_type = "Integer 16 bit";
    else if (vi.SampleType()==SAMPLE_INT24) s_type = "Integer 24 bit";
    else if (vi.SampleType()==SAMPLE_INT32) s_type = "Integer 32 bit";
    else if (vi.SampleType()==SAMPLE_FLOAT) s_type = "Float 32 bit";
    log->Printf(L"  Sample Type: %hs\n", s_type);
  }
  else
    log->Print(L"No audio stream.\n");

}

/*---------------------------------------------------------
---------------------------------------------------------*/
void Avisynther::FraThreadMain()
{
  int position;

  EnterCriticalSection(&fraMutex);
  // Destructor logic sets max suspend count to signal
  // thread to exit.
  while (fraSuspendCount != INT_MAX) {

    if (fraSuspendCount > 0 || fraPosition == fraEndPosition) {
      ResetEvent(fraResumeEvent);
      // Signal any waiting thread that the ra thread is
      // suspended, so OK to use env.
      SetEvent(fraSuspendedEvent);
      LeaveCriticalSection(&fraMutex);
      // Wait until more ra is necessary.
      WaitForSingleObject(fraResumeEvent,INFINITE);
      // Delay resuming read ahead a bit, to avoid slowing
      // down sequential reads.
      Sleep(fraResumeDelay);
      EnterCriticalSection(&fraMutex);
    }
    else {
      ResetEvent(fraSuspendedEvent);
      position = fraPosition;
      fraPosition ++;
      LeaveCriticalSection(&fraMutex);

      // Read the next frame and release it. Might be better
      // to hold the reference, but the MRU caching in avisynth
      // is enough for reasonable read ahead depths.
      try {
        PVideoFrame frame = clip->GetFrame(position, env);
      }
      catch (...) { }

      EnterCriticalSection(&fraMutex);
    }
  }
  LeaveCriticalSection(&fraMutex);
}

DWORD __stdcall Avisynther::FraThreadMainThunk(void* param)
{
  static_cast<Avisynther*>(param)->FraThreadMain();
  return 0;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void Avisynther::FraSuspend()
{
  if (fraThread) {
    EnterCriticalSection(&fraMutex);
    fraSuspendCount ++;
    LeaveCriticalSection(&fraMutex);
    WaitForSingleObject(fraSuspendedEvent,INFINITE);
  }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void Avisynther::FraResume()
{
  if (fraThread) {
    EnterCriticalSection(&fraMutex);
    ASSERT(fraSuspendCount);
    fraSuspendCount --;
    if (fraSuspendCount == 0 && fraEndPosition > fraPosition) {
      SetEvent(fraResumeEvent);
    }
    LeaveCriticalSection(&fraMutex);
  }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Exception protected clip->GetAudio()
bool/*success*/ Avisynther::GetAudio(AvfsLog_* log, void* buf, __int64 start, unsigned count) {

  FraSuspend();

  bool success = false;
  if (clip) {
    if (vi.HasAudio()) {
      try {
        clip->GetAudio(buf, start, (__int64)count, env);
        success = true;
      }
      catch(AvisynthError err) {
        setError(err.msg, L"GetAudio: AvisynthError.msg corrupted.");
      }
      catch (...) {
        setError("GetAudio: Unknown exception.");
      }
    }
  }
  if(!success)
  {
    log->Line(getError());
  }

  FraResume();
  return success;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Exception protected PVideoFrame->GetFrame()
PVideoFrame Avisynther::GetFrame(AvfsLog_* log, int n, bool *_success) {

  PVideoFrame frame = 0;
  bool success = false;

  if (n == lastPosition) {
    frame = lastFrame;
    success = true;
  }
  else {

    FraSuspend();

    lastPosition = -1;
    lastFrame = 0;

    if (clip) {
      if (vi.HasVideo()) {
        try {
          frame = clip->GetFrame(n, env);
          success = true;
        }
        catch(AvisynthError err) {
          setError(err.msg, L"GetFrame: AvisynthError.msg corrupted.");
        }
        catch (...) {
          setError("GetFrame: Unknown exception.");
        }
      }
    }
    if (!success) {
      log->Line(getError());
    }
    else {
      lastPosition = n;
      lastFrame = frame;
      if(fraThread) {
        // Have read ahead thread continue reading subsequent
        // frames to allow better multi-core utilization.
        if (n > fraEndPosition || n+fraFrameCount*2 < fraPosition) {
          fraPosition = n+1;
        }
        fraEndPosition = n+1+fraFrameCount;
      }
    }

    FraResume();
  }

  if (_success) *_success = success;

  return frame;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Readonly reference to VideoInfo
VideoInfoAdapter Avisynther::GetVideoInfo() {

  return &vi;

}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Read value of string variable from script. Returned pointer
// is valid until next call, so copy if you need it long term.
const char* Avisynther::GetVarAsString(
  const char* varName, const char* defVal)
{
  FraSuspend();

  const char *string = defVal;
  try {
    AVSValue value = env->GetVar(varName);
    if (value.IsString()) {
      string = value.AsString(defVal);
    }
  }
  catch (...) {
  }
  lastStringValue = string ? string : "";

  FraResume();
  return lastStringValue.c_str();
}


/*---------------------------------------------------------
---------------------------------------------------------*/

bool Avisynther::GetVarAsBool(const char* varName, bool defVal)
{
  FraSuspend();

  bool result = defVal;
  const char* strVal = 0;
  try {
    AVSValue value = env->GetVar(varName);
    if (value.IsString()) {
      strVal = value.AsString(0);
      result = stricmp(strVal, "true") == 0 ||
               stricmp(strVal, "yes") == 0 ||
               stricmp(strVal, "on") == 0 ||
               stricmp(strVal, "enable") == 0;
    }
    else {
      result = value.AsBool(defVal);
    }
  }
  catch (...) {
  }

  FraResume();
  return result;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int Avisynther::GetVarAsInt(const char* varName,int defVal)
{
  FraSuspend();

  int result = defVal;
  try {
    AVSValue value = env->GetVar(varName);
    result = value.AsInt(defVal);
  }
  catch (...) {
  }

  FraResume();
  return result;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Take a copy of the current error message
void Avisynther::setError(const char *_text, const wchar_t *alt) {
  std::wstring_convert<std::codecvt<wchar_t, char, std::mbstate_t>, wchar_t> conversion;
  errText.clear();

  if (_text)
      errText = conversion.from_bytes(_text);
  else if (alt)
      errText = alt;
  else
      errText = L"AvisynthError.msg corrupted.";
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Retrieve the current avisynth error message
const wchar_t* Avisynther::getError() {
  return errText.c_str();
}

/*---------------------------------------------------------
---------------------------------------------------------*/
// Exception protected refresh the IScriptEnvironment
int/*error*/ Avisynther::newEnv()
{
  int error = ERROR_OUTOFMEMORY;

  ASSERT(CreateScriptEnvironment);

  // Purge any old IScriptEnvironment
  if (env) {
    clip = 0; // Must release smartpointer before zapping env!
    try {
      delete env;
    }
    catch (...) { }
    env = 0;
  }

  // Make a new IScriptEnvironment
  try {
    env = CreateScriptEnvironment(AVISYNTH_INTERFACE_VERSION);
    if(env)
    {
      AVS_linkage = env->GetAVSLinkage();
      error = 0;
    }
  }
  catch(AvisynthError err) {
    setError(err.msg, L"CreateScriptEnvironment: AvisynthError.msg corrupted.");
  }
  catch (...) {
    setError("CreateScriptEnvironment: Unknown exception.");
  }

  ASSERT(!error == !!env);
  return error;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Exception protected IScriptEnvironment->Invoke()
AVSValue Avisynther::Invoke(const char* name, const AVSValue args) {

  if (env) {
    try {
      return env->Invoke(name, args);
    }
    catch(IScriptEnvironment::NotFound) {
      setError("Invoke: Function NotFound.");
    }
    catch(AvisynthError err) {
      setError(err.msg, L"Invoke: AvisynthError.msg corrupted.");
    }
    catch (...) {
      setError("Invoke: Unknown exception.");
    }
  }
  return AVSValue();

}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Constructor
Avisynther::Avisynther(void) :
  references(1),
  CreateScriptEnvironment(nullptr),
  hlib(0),
  env(0),
  clip(0),
  fraThread(0),
  fraSuspendCount(0),
  fraPosition(0),
  fraEndPosition(0),
  fraFrameCount(0),
  fraResumeDelay(0),
  lastPosition(-1),
  lastFrame(0)
{
  vi = {};

  InitializeCriticalSection(&fraMutex);
  fraResumeEvent = CreateEvent(0,1,0,0);
  fraSuspendedEvent = CreateEvent(0,1,0,0);
}

/*---------------------------------------------------------
---------------------------------------------------------*/

// Destructor
Avisynther::~Avisynther(void)
{
  ASSERT(!references);

  if (fraThread) {
    VERIFY(CloseHandle(fraThread));
  }
  if (fraResumeEvent) {
    VERIFY(CloseHandle(fraResumeEvent));
  }
  if (fraSuspendedEvent) {
    VERIFY(CloseHandle(fraSuspendedEvent));
  }
  DeleteCriticalSection(&fraMutex);

  if (env) {
    lastFrame = 0;
    clip = 0; // Must release smartpointer before zapping env!
    try {
      delete env;
    }
    catch (...) { }
    env = 0;
  }

  if (hlib) {
    ASSERT(FreeLibrary(hlib));
    hlib = NULL;
    CreateScriptEnvironment = nullptr;
  }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

int/*error*/ Avisynther::Init(
  AvfsLog_* log,
  AvfsVolume_* volume)
{
  int error = 0;
  // Load Avisynth.dll
  hlib = LoadLibrary(L"avisynth.dll");
  // hlib = LoadLibraryEx("c:\\code\\avisynth\\src\\debug\\avisynth.dll",0,LOAD_WITH_ALTERED_SEARCH_PATH);
  if (hlib) {
    // Get the CreateScriptEnvironment entry point
    CreateScriptEnvironment = (ICreateScriptEnvironment)GetProcAddress(hlib, "CreateScriptEnvironment");
    if (!CreateScriptEnvironment) {
      error = GetLastError();
      setError("Cannot find \"CreateScriptEnvironment\" entry point.");
    }
    else {
      error = Import(volume->GetScriptFileName());
    }
  }
  else {
    error = GetLastError();
    setError("Cannot load \"avisynth.dll\".");
  }

  if (!error) {
    // Initialize frame read-ahead logic.
    fraFrameCount = GetVarAsInt("AVFS_ReadAheadFrameCount",
      fraDefaultFrameCount);
    if (fraFrameCount < 0) {
      fraFrameCount = 0;
    }
    if (fraFrameCount > fraMaxFrameCount) {
      fraFrameCount = fraMaxFrameCount;
    }
    fraResumeDelay = GetVarAsInt("AVFS_ReadAheadDelayMsecs",
      fraDefaultResumeDelay);
    if (fraResumeDelay > fraMaxResumeDelay) {
      fraResumeDelay = fraMaxResumeDelay;
    }
    if (fraFrameCount && fraResumeEvent && fraSuspendedEvent) {
      ResetEvent(fraResumeEvent);
      SetEvent(fraSuspendedEvent);
      DWORD unusedThreadId;
      fraThread = CreateThread(0,0,FraThreadMainThunk,this,0,&unusedThreadId);
    }
  }

  if (error) {
    log->Line(getError());
  }
  else {
    reportFormat(log);
  }
  return error;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void Avisynther::AddRef(void)
{
  ASSERT(references);
  references ++;
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void Avisynther::Release(void)
{
  ASSERT(references);
  if (!--references)
  {
    if (fraThread) {
      // Kill the read-ahead thread before entering
      // destructor, to make sure it is not partially
      // torn down while thread running.
      FraSuspend();
      EnterCriticalSection(&fraMutex);
      fraSuspendCount = INT_MAX;
      SetEvent(fraResumeEvent);
      LeaveCriticalSection(&fraMutex);
      WaitForSingleObject(fraThread,INFINITE);
    }
    delete this;
  }
}

/*---------------------------------------------------------
---------------------------------------------------------*/

void AvfsProcessScript(
  AvfsLog_* log,
  AvfsVolume_* volume)
{
  // Construct an implementation of the media interface and
  // initialize the script.
  Avisynther* avs = new(std::nothrow) Avisynther();
  if (avs && avs->Init(log,volume) != 0)
  {
     avs->Release();
     avs = 0;
  }
  if (avs)
  {
    AvfsWavMediaInit(log,avs,volume);
    AvfsAviMediaInit(log,avs,volume);
    avs->Release();
  }
}
