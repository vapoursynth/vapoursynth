AC_INIT([vapoursynth], [25], [https://github.com/vapoursynth/vapoursynth/issues], [vapoursynth], [http://www.vapoursynth.com/])

: ${CFLAGS=""}
: ${CXXFLAGS=""}

AM_INIT_AUTOMAKE([foreign no-dist-gzip dist-xz subdir-objects no-define])
AM_SILENT_RULES([yes])

LT_INIT([win32-dll])

AC_PROG_CC
AC_PROG_CXX

dnl TODO: maybe add libdl check, maybe add <mutex> check


AC_ARG_ENABLE([debug], AS_HELP_STRING([--enable-debug], [Enable compilation options required for debugging. (default=no)]))
AS_IF(
      [test "x$enable_debug" = "xyes"],
      [
       AC_DEFINE([VS_CORE_DEBUG])
       AC_SUBST([DEBUG_CFLAGS], ["-O0 -g3 -ggdb -ftrapv"])
      ],
      [AC_SUBST([DEBUG_CFLAGS], [""])]
)



AC_ARG_WITH([plugindir], AS_HELP_STRING([--with-plugindir], [Folder where the included plugins will be installed. (default=LIBDIR/vapoursynth)]), [PLUGINDIR=$with_plugindir], [PLUGINDIR=$libdir/$PACKAGE_NAME])
dnl Can't do it with AC_DEFINE because $libdir wouldn't be expanded fully.
AC_SUBST([PLUGINDIR])



AC_CANONICAL_HOST



AC_ARG_ENABLE([core], AS_HELP_STRING([--enable-core], [Build the VapourSynth core library. (default=yes)]))
AS_IF(
      [test "x$enable_core" != "xno"],
      [
       AC_DEFINE([VS_CORE_EXPORTS])

       PKG_CHECK_MODULES([AVCODEC], [libavcodec >= 54.34.1])
       PKG_CHECK_MODULES([AVUTIL], [libavutil >= 52.3.0])
       PKG_CHECK_MODULES([SWSCALE], [libswscale >= 2.0.0])

       AC_CONFIG_FILES([pc/vapoursynth.pc:pc/autotools-vapoursynth.pc.in])

       ASFLAGS="$ASFLAGS -I$srcdir/include -w -Worphan-labels -Wunrecognized-char -Dprivate_prefix=vs"
       
       X86="false"
       PPC="false"
       ARM="false"
       
       AS_CASE(
               [$host_cpu],
               [i?86],   [BITS="32" ASFLAGS="$ASFLAGS -DARCH_X86_64=0" X86="true"],
               [x86_64], [BITS="64" ASFLAGS="$ASFLAGS -DARCH_X86_64=1 -DPIC -m amd64" X86="true"],
               [powerpc*], [PPC="true"],
               [arm*], [ARM="true"] # Maybe doesn't work for all arm systems?
       )
       
       AS_IF(
             [test "x$X86" = "xtrue"],
             [
              AC_ARG_WITH([yasm], AS_HELP_STRING([--with-yasm], [Override the automatic detection of the yasm executable.]), [], [with_yasm=check])
       
              AC_ARG_ENABLE([x86-asm], AS_HELP_STRING([--enable-x86-asm], [Enable assembler code for x86 CPUs. Requires yasm. (default=yes)]))
              AS_IF(
                    [test "x$enable_x86_asm" != "xno"],
                    [
                     AS_IF(
                           [test "x$with_yasm" = "xcheck"],
                           [AC_CHECK_PROGS([YASM], [yasm])],
                           [YASM="$with_yasm"]
                     )
       
                     AS_IF([test "x$YASM" = "x"],
                           [AC_MSG_ERROR([yasm required but not found.])],
                           [AS="$YASM"])
                     AC_DEFINE([VS_TARGET_CPU_X86])
                    ]
              )
             ]
       )

       AS_IF(
             [test "x$PPC" = "xtrue"],
             [AC_DEFINE([VS_TARGET_CPU_POWERPC])]
       )

       AS_IF(
             [test "x$ARM" = "xtrue"],
             [AC_DEFINE([VS_TARGET_CPU_ARM])]
       )

       AS_CASE(
          [$host_os],
          [darwin*],
          [
             ASFLAGS="$ASFLAGS -f macho$BITS -DPREFIX"
             AC_DEFINE([VS_TARGET_OS_DARWIN])
             DEBUG_FORMAT="null"
          ],
          [*linux*|gnu*|dragonfly*|*bsd*], # The BSDs are close enough, right?
          [
             ASFLAGS="$ASFLAGS -f elf"
             AC_DEFINE([VS_TARGET_OS_LINUX])
             DEBUG_FORMAT="dwarf2"
          ],
          [cygwin*|mingw*],
          [
             ASFLAGS="$ASFLAGS -f win32"
             AS_IF(
                   [test "x$BITS" = "x32"],
                   [ASFLAGS="$ASFLAGS -DPREFIX"]
             )
             AC_DEFINE([VS_TARGET_OS_WINDOWS])
             DEBUG_FORMAT="null"
          ],
          [AC_MSG_ERROR([Unknown host OS])]
       )
       
       AS_IF(
             [test "x$enable_debug" = "xyes"],
             [ASFLAGS="$ASFLAGS -g$DEBUG_FORMAT"]
       )
       
       AC_SUBST([ASFLAGS])

      ]
)
AM_CONDITIONAL([VSCORE], [test "x$enable_core" != "xno"])
AM_CONDITIONAL([X86ASM], [test "x$X86" = "xtrue" -a "x$enable_x86_asm" != "xno"])



AC_ARG_ENABLE([vsscript], AS_HELP_STRING([--enable-vsscript], [Enable VSScript. Requires Python 3. (default=yes)]))
AS_IF(
      [test "x$enable_vsscript" != "xno"],
      [
       PKG_CHECK_MODULES([PYTHON3], [python3], [], [PKG_CHECK_MODULES([PYTHON3], [python >= 3])])

       AC_CONFIG_FILES([pc/vapoursynth-script.pc:pc/autotools-vapoursynth-script.pc.in])
      ]
)
AM_CONDITIONAL([VSSCRIPT], [test "x$enable_vsscript" != "xno"])



AC_ARG_ENABLE([vspipe], AS_HELP_STRING([--enable-vspipe], [Build vspipe. Requires VSScript. (default=yes)]))
AS_IF(
      [test "x$enable_vsscript" = "xno"],
      [
       AS_IF(
             [test "x$enable_vspipe" = "xyes"],
             [AC_MSG_ERROR([Cannot build vspipe when VSScript is disabled.])],
             [enable_vspipe="no"]
       )
      ]
)
AM_CONDITIONAL([VSPIPE], [test "x$enable_vspipe" != "xno"])



AC_ARG_ENABLE([python-module], AS_HELP_STRING([--enable-python-module], [Build the Python module. Requires Cython, Python, and the core. (default=yes)]))
AS_IF(
      [test "x$enable_core" = "xno"],
      [
       AS_IF(
             [test "x$enable_python_module" = "xyes"],
             [AC_MSG_ERROR([Cannot build the Python module when the core is disabled.])],
             [enable_python_module="no"]
       )
      ],
      [
       AC_ARG_WITH([cython], AS_HELP_STRING([--with-cython], [Override the automatic detection of the Cython executable.]), [], [with_cython=check])
       AS_IF(
             [test "x$with_cython" = "xcheck"],
             [AC_CHECK_PROGS([CYTHON], [cython])],
             [CYTHON="$with_cython"]
       )

       AS_IF(
             [test "x$CYTHON" = "x"],
             [AC_MSG_ERROR([Cython required but not found.])]
       )
       AC_SUBST([CYTHON])

       AS_IF(
             [test -z "$PYTHON3_LIBS"],
             [PKG_CHECK_MODULES([PYTHON3], [python3], [], [PKG_CHECK_MODULES([PYTHON3], [python >= 3])])]
       )

       AM_PATH_PYTHON([3])
      ]
)
AM_CONDITIONAL([PYTHONMODULE], [test "x$enable_python_module" != "xno"])



AC_ARG_ENABLE([plugins], AS_HELP_STRING([--enable-plugins], [Enable all included plugins. Each individual plugin has a switch which overrides this. (default=)]))

AC_ARG_ENABLE([assvapour], AS_HELP_STRING([--enable-assvapour], [Enable the ASS rendering plugin. Requires libass. (default=yes)]))
AC_ARG_ENABLE([eedi3], AS_HELP_STRING([--enable-eedi3], [Enable the eedi3 plugin. (default=yes)]))
AC_ARG_ENABLE([imwri], AS_HELP_STRING([--enable-imwri], [Enable the image reader/writer plugin. Requires ImageMagick. (default=no)]))
AC_ARG_ENABLE([morpho], AS_HELP_STRING([--enable-morpho], [Enable the morpho plugin. (default=yes)]))
AC_ARG_ENABLE([ocr], AS_HELP_STRING([--enable-ocr], [Enable the ocr plugin. Requires tesseract. (default=yes)]))
AC_ARG_ENABLE([removegrain], AS_HELP_STRING([--enable-removegrain], [Enable the removegrain plugin. (default=yes)]))
AC_ARG_ENABLE([vinverse], AS_HELP_STRING([--enable-vinverse], [Enable the vinverse plugin. (default=yes)]))
AC_ARG_ENABLE([vivtc], AS_HELP_STRING([--enable-vivtc], [Enable the vivtc plugin. (default=yes)]))

assvapour=""
eedi3=""
imwri=""
morpho=""
ocr=""
removegrain=""
vinverse=""
vivtc=""

AS_CASE(
        [$enable_plugins],
        [yes], [
                AS_IF([test "x$enable_assvapour" != "xno"], [assvapour="yes"])
                AS_IF([test "x$enable_eedi3" != "xno"], [eedi3="yes"])
                AS_IF([test "x$enable_imwri" != "xno"], [imwri="yes"])
                AS_IF([test "x$enable_morpho" != "xno"], [morpho="yes"])
                AS_IF([test "x$enable_ocr" != "xno"], [ocr="yes"])
                AS_IF([test "x$enable_removegrain" != "xno"], [removegrain="yes"])
                AS_IF([test "x$enable_vinverse" != "xno"], [vinverse="yes"])
                AS_IF([test "x$enable_vivtc" != "xno"], [vivtc="yes"])
               ],
        [no], [
                AS_IF([test "x$enable_assvapour" = "xyes"], [assvapour="yes"])
                AS_IF([test "x$enable_eedi3" = "xyes"], [eedi3="yes"])
                AS_IF([test "x$enable_imwri" = "xyes"], [imwri="yes"])
                AS_IF([test "x$enable_morpho" = "xyes"], [morpho="yes"])
                AS_IF([test "x$enable_ocr" = "xyes"], [ocr="yes"])
                AS_IF([test "x$enable_removegrain" = "xyes"], [removegrain="yes"])
                AS_IF([test "x$enable_vinverse" = "xyes"], [vinverse="yes"])
                AS_IF([test "x$enable_vivtc" = "xyes"], [vivtc="yes"])
              ],
        [
         AS_IF([test "x$enable_assvapour" != "xno"], [assvapour="yes"])
         AS_IF([test "x$enable_eedi3" != "xno"], [eedi3="yes"])
         AS_IF([test "x$enable_imwri" = "xyes"], [imwri="yes"])
         AS_IF([test "x$enable_morpho" != "xno"], [morpho="yes"])
         AS_IF([test "x$enable_ocr" != "xno"], [ocr="yes"])
         AS_IF([test "x$enable_removegrain" != "xno"], [removegrain="yes"])
         AS_IF([test "x$enable_vinverse" != "xno"], [vinverse="yes"])
         AS_IF([test "x$enable_vivtc" != "xno"], [vivtc="yes"])
        ]
)

AS_IF(
      [test "$assvapour"],
      [PKG_CHECK_MODULES([LIBASS], [libass])]
)

AS_IF(
      [test "$imwri"],
      [PKG_CHECK_MODULES([IMAGEMAGICK], [ImageMagick++]) ]
)

AS_IF(
      [test "$ocr"],
      [PKG_CHECK_MODULES([TESSERACT], [tesseract])]
)

AM_CONDITIONAL([ASSVAPOUR], [test "$assvapour"])
AM_CONDITIONAL([EEDI3], [test "$eedi3"])
AM_CONDITIONAL([IMWRI], [test "$imwri"])
AM_CONDITIONAL([MORPHO], [test "$morpho"])
AM_CONDITIONAL([OCR], [test "$ocr"])
AM_CONDITIONAL([REMOVEGRAIN], [test "$removegrain"])
AM_CONDITIONAL([VINVERSE], [test "$vinverse"])
AM_CONDITIONAL([VIVTC], [test "$vivtc"])



dnl Workaround for a bug in libtool
dnl The windows libtool uses a file magic checking method that only accepts
dnl dynamic libraries. Change it for libtool's alternative checking method.

dnl Workaround found in configure.ac from ffms2.

dnl Reproducing the error message below, for search engines and people
dnl looking for a solution...

dnl *** Warning: linker path does not have real file for library -lfftw3f.
dnl *** I have the capability to make that library automatically link in when
dnl *** you link to this library.  But I can only do this if you have a
dnl *** shared version of the library, which you do not appear to have
dnl *** because I did check the linker path looking for a file starting
dnl *** with libfftw3f and none of the candidates passed a file format test
dnl *** using a file magic. Last file checked: /home/asdf/mingw-w64/i686/mingw/lib/libfftw3f.a
dnl *** The inter-library dependencies that have been dropped here will be
dnl *** automatically added whenever a program is linked with this library
dnl *** or is declared to -dlopen it.
dnl *
dnl *** Since this library must not contain undefined symbols,
dnl *** because either the platform does not support them or
dnl *** it was explicitly requested with -no-undefined,
dnl *** libtool will only create a static version of it.

if test "$lt_cv_file_magic_cmd" = "func_win32_libid" ; then
    deplibs_check_method='file_magic file format pei*-(i386|x86-64)|(.*architecture: i386)?'
    file_magic_cmd='$OBJDUMP -f'
fi



AC_CONFIG_FILES([Makefile])
AC_OUTPUT
