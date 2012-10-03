
// Null ptr and buffer overflow safe C string primitives.

inline const wchar_t* ssfix(const wchar_t* s) { return s?s:L""; }
inline const char* ssfix(const char* s) { return s?s:""; }
inline size_t sslen(const wchar_t* s) { return s?wcslen(s):0; }
inline size_t sslen(const char* s) { return s?strlen(s):0; }
inline size_t sssize(const wchar_t* s) { return sslen(s)*sizeof(s[0]); }
inline size_t sssize(const char* s) { return sslen(s)*sizeof(s[0]); }
void sscpy(wchar_t* dest,size_t maxDestChars,const wchar_t* source);
inline void ssfree(wchar_t* d) { if(d) free(d); }
inline void ssfree(char* d) { if(d) free(d); }
wchar_t* ssalloc(size_t count);
wchar_t* ssdup(const wchar_t* s);
int sscmpi(const wchar_t* s1,const wchar_t* s2);
wchar_t* ssrchr(const wchar_t* s,wchar_t c);
wchar_t* ssconvalloc(const char* s);
char* ssconvalloc(const wchar_t* s);
void ssvformat(wchar_t* dest,size_t maxDestChars,const wchar_t* format,va_list args);
void ssformat(wchar_t* dest,size_t maxDestChars,const wchar_t* format,...);
wchar_t* ssvformatalloc(const wchar_t* format,va_list args);
wchar_t* ssformatalloc(const wchar_t* format,...);
