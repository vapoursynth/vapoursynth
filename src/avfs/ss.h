
// Null ptr and buffer overflow safe C string primitives.

inline const wchar_t* ssfix(const wchar_t* s) { return s?s:L""; }
inline const char* ssfix(const char* s) { return s?s:""; }
inline size_t sslen(const wchar_t* s) { return s?wcslen(s):0; }
inline size_t sslen(const char* s) { return s?strlen(s):0; }
wchar_t* ssalloc(size_t count);
wchar_t* ssdup(const wchar_t* s);
wchar_t* ssdupn(const wchar_t* s,size_t len);
int sscmpi(const wchar_t* s1,const wchar_t* s2);
wchar_t* ssrchr(const wchar_t* s,wchar_t c);
void ssvformat(wchar_t* dest,size_t maxDestChars,const wchar_t* format,va_list args);
void ssformat(wchar_t* dest,size_t maxDestChars,const wchar_t* format,...);
wchar_t* ssvformatalloc(const wchar_t* format,va_list args);
