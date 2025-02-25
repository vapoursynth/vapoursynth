warningflags = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-ignored-attributes
commoncflags = -O3 -fvisibility=hidden $(warningflags) -I$(srcdir)/include $(DEBUGCFLAGS) $(MFLAGS) $(UNICODECFLAGS) $(STACKREALIGN)
AM_CXXFLAGS = -std=c++17 $(commoncflags)
AM_CFLAGS = -std=c99 $(commoncflags)
ACLOCAL_AMFLAGS = -I m4
commonarmflags = $(SSE2NEON_CFLAGS)

VSCURRENTRELEASE := $(shell cat VAPOURSYNTH_VERSION | cut -d ' ' -f3  | cut -d '-' -f1)


cython_verbose = $(cython_verbose_$(V))
cython_verbose_ = $(cython_verbose_$(AM_DEFAULT_VERBOSITY))
cython_verbose_0 = @echo "  CYTHON  " $@;


.pyx.c:
	$(cython_verbose)$(CYTHON) -o $@ $<


commonlibtoolflags = --tag=disable-static


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA =

EXTRA_DIST = pc/vapoursynth.pc.in \
			 pc/vapoursynth-script.pc.in



lib_LTLIBRARIES =


if VSCORE
noinst_LTLIBRARIES =
lib_LTLIBRARIES += libvapoursynth.la

libvapoursynth_la_CFLAGS = $(AM_CFLAGS)
libvapoursynth_la_CXXFLAGS = $(AM_CXXFLAGS)

libvapoursynth_la_SOURCES = src/core/audiofilters.cpp \
							src/core/averageframesfilter.cpp \
							src/core/boxblurfilter.cpp \
							src/core/cpufeatures.cpp \
							src/core/cpufeatures.h \
							src/core/expr/expr.cpp \
							src/core/expr/expr.h \
							src/core/expr/jitcompiler.cpp \
							src/core/expr/jitcompiler.h \
							src/core/exprfilter.cpp \
							src/core/filtershared.h \
							src/core/genericfilters.cpp \
							src/core/internalfilters.h \
							src/core/kernel/average.cpp \
							src/core/kernel/average.h \
							src/core/kernel/cpulevel.cpp \
							src/core/kernel/cpulevel.h \
							src/core/kernel/generic.cpp \
							src/core/kernel/generic.h \
							src/core/kernel/merge.c \
							src/core/kernel/merge.h \
							src/core/kernel/planestats.c \
							src/core/kernel/planestats.h \
							src/core/kernel/transpose.c \
							src/core/kernel/transpose.h \
							src/core/lutfilters.cpp \
							src/core/memoryuse.cpp \
							src/core/memoryuse.h \
							src/core/mergefilters.cpp \
							src/core/reorderfilters.cpp \
							src/core/settings.cpp \
							src/core/settings.h \
							src/core/simplefilters.cpp \
							src/core/ter-116n.h \
							src/core/textfilter.cpp \
							src/core/version.h \
							src/core/vsapi.cpp \
							src/core/vscore.cpp \
							src/core/vscore.h \
							src/core/vslog.cpp \
							src/core/vslog.h \
							src/core/vsresize.cpp \
							src/core/vsthreadpool.cpp \
							src/core/x86utils.h

pkginclude_HEADERS = include/VapourSynth.h \
					 include/VapourSynth4.h \
					 include/VSConstants4.h \
					 include/VSHelper.h \
					 include/VSHelper4.h \
					 include/VSScript.h \
					 include/VSScript4.h

pkgconfig_DATA += pc/vapoursynth.pc

libvapoursynth_la_LDFLAGS = -no-undefined -avoid-version $(UNDEFINEDLDFLAGS)
libvapoursynth_la_CPPFLAGS = $(PTHREAD_CFLAGS) $(ZIMG_CFLAGS) -DVS_PATH_PLUGINDIR='"$(PLUGINDIR)"' -DVS_CURRENT_RELEASE='$(VSCURRENTRELEASE)'
libvapoursynth_la_LIBADD = $(PTHREAD_LIBS) $(ZIMG_LIBS) $(DLOPENLIB)


if X86ASM
noinst_LTLIBRARIES += libvapoursynth_avx2.la

libvapoursynth_avx2_la_SOURCES = src/core/kernel/x86/convolution_avx2.cpp \
								 src/core/kernel/x86/generic_avx2.cpp \
								 src/core/kernel/x86/merge_avx2.c \
								 src/core/kernel/x86/planestats_avx2.c
libvapoursynth_avx2_la_CFLAGS = $(AM_CFLAGS) $(AVX2FLAGS)
libvapoursynth_avx2_la_CXXFLAGS = $(AM_CXXFLAGS) $(AVX2FLAGS)

libvapoursynth_la_SOURCES += src/core/expr/jitasm.h \
							 src/core/expr/jitcompiler_x86.cpp \
							 src/core/kernel/x86/average_sse2.c \
							 src/core/kernel/x86/convolution_sse2.cpp \
							 src/core/kernel/x86/generic_sse2.cpp \
							 src/core/kernel/x86/merge_sse2.c \
							 src/core/kernel/x86/planestats_sse2.c \
							 src/core/kernel/x86/transpose_sse2.c

libvapoursynth_la_LIBADD += libvapoursynth_avx2.la
endif # X86ASM

if ARMASM
libvapoursynth_la_SOURCES += src/core/kernel/arm_neon/average_neon.c \
							 src/core/kernel/arm_neon/merge_neon.c \
							 src/core/kernel/arm_neon/planestats_neon.c \
							 src/core/kernel/arm_neon/transpose_neon.c \
							 src/core/kernel/arm_neon/convolution_neon.cpp \
							 src/core/kernel/arm_neon/generic_neon.cpp

libvapoursynth_la_CFLAGS += $(commonarmflags)
libvapoursynth_la_CXXFLAGS += $(commonarmflags)
endif # ARMASM

if PYTHONMODULE
pyexec_LTLIBRARIES = vapoursynth.la

vapoursynth_la_SOURCES = src/cython/vapoursynth.pyx
vapoursynth_la_CFLAGS= $(AM_CFLAGS) -fvisibility=default -Wno-implicit-fallthrough
vapoursynth_la_CPPFLAGS = $(PYTHON3_CFLAGS) -DVS_CURRENT_RELEASE='$(VSCURRENTRELEASE)'
vapoursynth_la_LIBADD = $(MAYBE_PYTHON3_LIBS) libvapoursynth.la
vapoursynth_la_LDFLAGS = $(PYTHON_MODULE_UNDEFINED) -avoid-version -module
vapoursynth_la_LIBTOOLFLAGS = $(commonlibtoolflags)

MOSTLYCLEANFILES = src/cython/vapoursynth.c \
				   src/cython/vapoursynth.h \
				   src/cython/vapoursynth_api.h
endif # PYTHONMODULE
endif # VSCORE



if VSSCRIPT
lib_LTLIBRARIES += libvapoursynth-script.la

pkgconfig_DATA += pc/vapoursynth-script.pc

libvapoursynth_script_la_SOURCES = src/vsscript/vsscript.cpp
libvapoursynth_script_la_LDFLAGS = -no-undefined -version-info 0 $(UNDEFINEDLDFLAGS)
libvapoursynth_script_la_CPPFLAGS = $(PYTHON3_CFLAGS) -DVS_CURRENT_RELEASE='$(VSCURRENTRELEASE)'
libvapoursynth_script_la_LIBADD = $(PYTHON3_LIBS)


if VSPIPE
bin_PROGRAMS = vspipe

vspipe_SOURCES = src/vspipe/vspipe.cpp \
				 src/vspipe/printgraph.cpp \
				 src/vspipe/vsjson.cpp \
				 src/common/wave.cpp

vspipe_LDADD = libvapoursynth-script.la
vspipe_LDFLAGS = $(UNICODELDFLAGS)
vspipe_CPPFLAGS = $(PYTHON3_CFLAGS) -DVS_CURRENT_RELEASE='$(VSCURRENTRELEASE)'
endif # VSPIPE
endif # VSSCRIPT



pkglib_LTLIBRARIES =

commonpluginldflags = -no-undefined -avoid-version $(PLUGINLDFLAGS) $(UNDEFINEDLDFLAGS)
