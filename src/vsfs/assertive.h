#ifdef __cplusplus
extern "C" {
#endif

// Disable some near useless warnings that make W4 difficult to use.
#pragma warning(disable: 4100) // unreferenced parameter.
#pragma warning(disable: 4512) // assignment operator could not be generated
#pragma warning(disable: 4505) // unreferenced local function has been removed
#pragma warning(disable: 4514) // unreferenced inline function has been removed
#pragma warning(disable: 4291) // no matching operator delete
#ifndef NDEBUG
#pragma warning(disable: 4127) // conditional expression is constant
#endif
#pragma warning(disable: 4201) // nameless struct/union
#pragma warning(disable: 4710) // function not expanded

#ifdef _M_IX86
#define TRAP_ __asm nop __asm int 3 __asm nop __asm { }
#else
#define TRAP_ __debugbreak()
#endif

void AssertHandler(const char* file,int line,const char* expr);
#define ASSERT_(e) do { if(!(e)) { AssertHandler(__FILE__,__LINE__,#e); } } while(0)

#undef ASSERT
#undef VERIFY
#ifdef NDEBUG
#define TRAP
#define TRAPIF(e)
#define ASSERT(e)
#define VERIFY(e) e
#define VERIFYNOT(e) e
#define VERIFYIS(e,v) e
#define VERIFYISNOT(e,v) e
#else
#define TRAP TRAP_
#define TRAPIF(e) do { if(e) TRAP_; } while(0)
#define ASSERT(e) ASSERT_(e)
#define VERIFY(e) ASSERT_(e)
#define VERIFYNOT(e) ASSERT_(!(e))
#define VERIFYIS(e,v) ASSERT_((e) == (v))
#define VERIFYISNOT(e,v) ASSERT_((e) != (v))
#endif

#define offsetofend(s,m) (offsetof(s,m)+sizeof(((s*)0)->m))
#define offsetof2(s,m) ((size_t)((char*)(&((s)->m))-(char*)(s)))

#ifdef __cplusplus
#define SCAST(t,e) static_cast<t>(e)
#define CCAST(t,e) const_cast<t>(e)
#define RCAST(t,e) reinterpret_cast<t>(e)
#else
#define SCAST(t,e) ((t)(e))
#define CCAST(t,e) ((t)(e))
#define RCAST(t,e) ((t)(e))
#endif

#define MAKETAGINT16(c0,c1)                    SCAST(int16_t,(c0)|((c1)<<8))
#define MAKETAGINT32(c0,c1,c2,c3)              SCAST(int32_t,(c0)|((c1)<<8)|((c2)<<16)|((c3)<<24))
#define MAKETAGINT64(c0,c1,c2,c3,c4,c5,c6,c7)  SCAST(int64_t,(c0)|((c1)<<8)|((c2)<<16)|((c3)<<24)|(SCAST(int64_t,(c4))<<32)|(SCAST(int64_t,(c5))<<40)|(SCAST(int64_t,(c6))<<48)|(SCAST(int64_t,(c7))<<56))
#define MAKETAGUINT16(c0,c1)                   SCAST(uint16_t,(c0)|((c1)<<8))
#define MAKETAGUINT32(c0,c1,c2,c3)             SCAST(uint32_t,(c0)|((c1)<<8)|((c2)<<16)|((c3)<<24))
#define MAKETAGUINT64(c0,c1,c2,c3,c4,c5,c6,c7) SCAST(uint64_t,(c0)|((c1)<<8)|((c2)<<16)|((c3)<<24)|(SCAST(uint64_t,(c4))<<32)|(SCAST(uint64_t,(c5))<<40)|(SCAST(uint64_t,(c6))<<48)|(SCAST(uint64_t,(c7))<<56))
#define MAKETAGINT(c0,c1,c2,c3) MAKETAGINT32(c0,c1,c2,c3)
#define MAKETAGUINT(c0,c1,c2,c3) MAKETAGUINT32(c0,c1,c2,c3)

#ifdef __cplusplus
}
#endif
