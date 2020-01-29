warningflags = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-ignored-attributes
commoncflags = -O3 -fvisibility=hidden $(warningflags) -I$(srcdir)/include $(DEBUGCFLAGS) $(MFLAGS) $(UNICODECFLAGS) $(STACKREALIGN)
AM_CXXFLAGS = -std=c++11 $(commoncflags)
AM_CFLAGS = -std=c99 $(commoncflags)



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
noinst_LTLIBRARIES = libexprfilter.la

libexprfilter_la_SOURCES = src/core/exprfilter.cpp
libexprfilter_la_CPPFLAGS = $(AM_CXXFLAGS) -fno-strict-aliasing

lib_LTLIBRARIES += libvapoursynth.la

libvapoursynth_la_SOURCES = src/core/boxblurfilter.cpp \
							src/core/cachefilter.cpp \
							src/core/cachefilter.h \
							src/core/cpufeatures.cpp \
							src/core/cpufeatures.h \
							src/core/filtershared.h \
							src/core/genericfilters.cpp \
							src/core/internalfilters.h \
							src/core/jitasm.h \
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
							src/core/mergefilters.c \
							src/core/reorderfilters.c \
							src/core/settings.cpp \
							src/core/settings.h \
							src/core/simplefilters.c \
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
					 include/VSHelper.h \
					 include/VSScript.h

pkgconfig_DATA += pc/vapoursynth.pc

libvapoursynth_la_LDFLAGS = -no-undefined -avoid-version
libvapoursynth_la_CPPFLAGS = $(ZIMG_CFLAGS) -DVS_PATH_PLUGINDIR='"$(PLUGINDIR)"'
libvapoursynth_la_LIBADD = $(ZIMG_LIBS) $(DLOPENLIB) libexprfilter.la


if X86ASM
noinst_LTLIBRARIES += libvapoursynth_avx2.la

libvapoursynth_avx2_la_SOURCES = src/core/kernel/x86/generic_avx2.cpp \
								 src/core/kernel/x86/merge_avx2.c \
								 src/core/kernel/x86/planestats_avx2.c 
libvapoursynth_avx2_la_CFLAGS = $(AM_CFLAGS) $(AVX2FLAGS)
libvapoursynth_avx2_la_CXXFLAGS = $(AM_CXXFLAGS) $(AVX2FLAGS)

libvapoursynth_la_SOURCES += src/core/jitasm.h \
							 src/core/kernel/x86/generic_sse2.cpp \
							 src/core/kernel/x86/merge_sse2.c \
							 src/core/kernel/x86/planestats_sse2.c \
							 src/core/kernel/x86/transpose_sse2.c

libvapoursynth_la_LIBADD += libvapoursynth_avx2.la
endif # X86ASM

if PYTHONMODULE
pyexec_LTLIBRARIES = vapoursynth.la

vapoursynth_la_SOURCES = src/cython/vapoursynth.pyx
vapoursynth_la_CFLAGS= $(AM_CFLAGS) -fvisibility=default -Wno-implicit-fallthrough
vapoursynth_la_CPPFLAGS = $(PYTHON3_CFLAGS)
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
libvapoursynth_script_la_LDFLAGS = -no-undefined -version-info 0
libvapoursynth_script_la_CPPFLAGS = $(PYTHON3_CFLAGS)
libvapoursynth_script_la_LIBADD = $(PYTHON3_LIBS)


if VSPIPE
bin_PROGRAMS = vspipe

vspipe_SOURCES = src/vspipe/vspipe.cpp
vspipe_LDADD = libvapoursynth-script.la
vspipe_LDFLAGS = $(UNICODELDFLAGS)
endif # VSPIPE
endif # VSSCRIPT



pkglib_LTLIBRARIES =

commonpluginldflags = -no-undefined -avoid-version $(PLUGINLDFLAGS)

if SUBTEXT
pkglib_LTLIBRARIES += libsubtext.la

libsubtext_la_SOURCES = src/filters/subtext/text.c \
						src/filters/subtext/common.c \
						src/filters/subtext/common.h \
						src/filters/subtext/image.cpp \
						src/filters/subtext/toass.cpp \
						src/filters/subtext/toutf8.c

libsubtext_la_LDFLAGS = $(commonpluginldflags)
libsubtext_la_LIBTOOLFLAGS = $(commonlibtoolflags)
libsubtext_la_CPPFLAGS = $(LIBASS_CFLAGS) $(FFMPEG_CFLAGS)
libsubtext_la_LIBADD = $(LIBASS_LIBS) $(FFMPEG_LIBS)
endif



if EEDI3
pkglib_LTLIBRARIES += libeedi3.la

libeedi3_la_SOURCES = src/filters/eedi3/eedi3.c
libeedi3_la_LDFLAGS = $(commonpluginldflags)
libeedi3_la_LIBTOOLFLAGS = $(commonlibtoolflags)
endif



if IMWRI
pkglib_LTLIBRARIES += libimwri.la

libimwri_la_SOURCES = src/filters/imwri/imwri.cpp
libimwri_la_LDFLAGS = $(commonpluginldflags)
libimwri_la_LIBTOOLFLAGS = $(commonlibtoolflags)
libimwri_la_CPPFLAGS = $(IMAGEMAGICK_CFLAGS)
libimwri_la_LIBADD = $(IMAGEMAGICK_LIBS)
endif



if MISCFILTERS
pkglib_LTLIBRARIES += libmiscfilters.la

libmiscfilters_la_SOURCES = src/filters/misc/miscfilters.cpp
libmiscfilters_la_LDFLAGS = $(commonpluginldflags)
libmiscfilters_la_LIBTOOLFLAGS = $(commonlibtoolflags)
endif



if MORPHO
pkglib_LTLIBRARIES += libmorpho.la

libmorpho_la_SOURCES = src/filters/morpho/morpho.c \
					   src/filters/morpho/morpho_filters.c \
					   src/filters/morpho/morpho_filters.h \
					   src/filters/morpho/morpho.h \
					   src/filters/morpho/morpho_selems.c \
					   src/filters/morpho/morpho_selems.h
libmorpho_la_LDFLAGS = $(commonpluginldflags)
libmorpho_la_LIBTOOLFLAGS = $(commonlibtoolflags)
endif



if OCR
pkglib_LTLIBRARIES += libocr.la

libocr_la_SOURCES = src/filters/ocr/ocr.c
libocr_la_LDFLAGS = $(commonpluginldflags)
libocr_la_LIBTOOLFLAGS = $(commonlibtoolflags)
libocr_la_CPPFLAGS = $(TESSERACT_CFLAGS)
libocr_la_LIBADD = $(TESSERACT_LIBS)
endif



if REMOVEGRAIN
pkglib_LTLIBRARIES += libremovegrain.la

libremovegrain_la_SOURCES = src/filters/removegrain/clense.cpp \
							src/filters/removegrain/removegrainvs.cpp \
							src/filters/removegrain/repairvs.cpp \
							src/filters/removegrain/shared.cpp \
							src/filters/removegrain/shared.h \
							src/filters/removegrain/verticalcleaner.cpp
libremovegrain_la_LDFLAGS = $(commonpluginldflags)
libremovegrain_la_LIBTOOLFLAGS = $(commonlibtoolflags)
endif



if VINVERSE
pkglib_LTLIBRARIES += libvinverse.la

libvinverse_la_SOURCES = src/filters/vinverse/vinverse.c
libvinverse_la_LDFLAGS = $(commonpluginldflags)
libvinverse_la_LIBTOOLFLAGS = $(commonlibtoolflags)
endif



if VIVTC
pkglib_LTLIBRARIES += libvivtc.la

libvivtc_la_SOURCES = src/filters/vivtc/vivtc.c
libvivtc_la_LDFLAGS = $(commonpluginldflags)
libvivtc_la_LIBTOOLFLAGS = $(commonlibtoolflags)
endif
