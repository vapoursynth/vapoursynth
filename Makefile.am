warningflags = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
commoncflags = -O3 $(warningflags) -I$(srcdir)/include $(DEBUGCFLAGS) $(MFLAGS) $(UNICODECFLAGS)
AM_CXXFLAGS = -std=c++11 $(commoncflags)
AM_CFLAGS = -std=c99 $(commoncflags)



yasm_verbose = $(yasm_verbose_$(V))
yasm_verbose_ = $(yasm_verbose_$(AM_DEFAULT_VERBOSITY))
yasm_verbose_0 = @echo "  YASM    " $@;


cython_verbose = $(cython_verbose_$(V))
cython_verbose_ = $(cython_verbose_$(AM_DEFAULT_VERBOSITY))
cython_verbose_0 = @echo "  CYTHON  " $@;


.asm.lo:
	$(yasm_verbose)$(LIBTOOL) $(AM_V_lt) --mode=compile --tag=CC $(AS) $(ASFLAGS) -o $@ $< -prefer-non-pic

.pyx.c:
	$(cython_verbose)$(CYTHON) -o $@ $<


commonlibtoolflags = --tag=disable-static


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = 

EXTRA_DIST = pc/vapoursynth.pc.in \
			 pc/vapoursynth-script.pc.in



lib_LTLIBRARIES =


if VSCORE
lib_LTLIBRARIES += libvapoursynth.la

libvapoursynth_la_SOURCES = src/core/cachefilter.cpp \
							src/core/cachefilter.h \
							src/core/cpufeatures.c \
							src/core/cpufeatures.h \
							src/core/exprfilter.cpp \
							src/core/exprfilter.h \
							src/core/filtershared.h \
							src/core/genericfilters.cpp \
							src/core/genericfilters.h \
							src/core/lutfilters.c \
							src/core/lutfilters.h \
							src/core/mergefilters.c \
							src/core/mergefilters.h \
							src/core/reorderfilters.c \
							src/core/reorderfilters.h \
							src/core/settings.cpp \
							src/core/settings.h \
							src/core/simplefilters.c \
							src/core/simplefilters.h \
							src/core/ter-116n.h \
							src/core/textfilter.cpp \
							src/core/textfilter.h \
							src/core/version.h \
							src/core/vsapi.cpp \
							src/core/vscore.cpp \
							src/core/vscore.h \
							src/core/vslog.cpp \
							src/core/vslog.h \
							src/core/vsresize.c \
							src/core/vsresize.h \
							src/core/vsthreadpool.cpp \
							src/core/x86utils.h

if X86ASM
libvapoursynth_la_SOURCES += src/core/asm/x86/check.asm \
							 src/core/asm/x86/cpu.asm \
							 src/core/asm/x86/expr.asm \
							 src/core/asm/x86/merge.asm \
							 src/core/asm/x86/transpose.asm
endif # X86ASM

pkginclude_HEADERS = include/VapourSynth.h \
					 include/VSHelper.h \
					 include/VSScript.h

noinst_HEADERS = include/asm/x86/x86inc.asm

pkgconfig_DATA += pc/vapoursynth.pc

libvapoursynth_la_LDFLAGS = -no-undefined -avoid-version
libvapoursynth_la_CPPFLAGS = $(AVCODEC_CFLAGS) $(AVUTIL_CFLAGS) $(SWSCALE_CFLAGS) -DVS_PATH_PLUGINDIR='"$(PLUGINDIR)"'
libvapoursynth_la_LIBADD = $(AVUTIL_LIBS) $(SWSCALE_LIBS) $(DLOPENLIB)


if PYTHONMODULE
pyexec_LTLIBRARIES = vapoursynth.la

vapoursynth_la_SOURCES = src/cython/vapoursynth.pyx
vapoursynth_la_CPPFLAGS = $(PYTHON3_CFLAGS)
vapoursynth_la_LIBADD = $(PYTHON3_LIBS) libvapoursynth.la
vapoursynth_la_LDFLAGS = -no-undefined -avoid-version -module
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

if ASSVAPOUR
pkglib_LTLIBRARIES += libassvapour.la

libassvapour_la_SOURCES = src/filters/assvapour/assvapour.c

libassvapour_la_LDFLAGS = $(commonpluginldflags)
libassvapour_la_LIBTOOLFLAGS = $(commonlibtoolflags)
libassvapour_la_CPPFLAGS = $(LIBASS_CFLAGS)
libassvapour_la_LIBADD = $(LIBASS_LIBS)
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
