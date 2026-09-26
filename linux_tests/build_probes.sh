#!/bin/sh
# Builds every probe in this directory against a VapourSynth install prefix.
#
#   ./build_probes.sh /path/to/inst [extra compiler flags...]
#
# The prefix is whatever `meson setup --prefix=... && ninja install` produced. Pass sanitizer
# flags as extra arguments, for example:
#
#   ./build_probes.sh "$PWD/../inst-tsan"  -fsanitize=thread -g
#   ./build_probes.sh "$PWD/../inst-asan"  -fsanitize=address -g
#   ./build_probes.sh "$PWD/../inst-ubsan" -fsanitize=undefined -g
#
# Binaries land in ./bin. Run them with the matching prefix on LD_LIBRARY_PATH; run_all.sh does
# that for you.
set -e

PREFIX="$1"
if [ -z "$PREFIX" ]; then
    echo "usage: $0 <install-prefix> [extra compiler flags...]" >&2
    exit 2
fi
shift

# Default to the toolchain meson used, which on POSIX is cc/c++. This matters for the sanitizer
# builds: a probe compiled by clang pulls in libclang_rt.asan while a gcc-built libvapoursynth
# pulls in libasan.so, and two ASan runtimes in one process abort with "Your application is
# linked against incompatible ASan runtimes." Override CC/CXX if meson used something else.
CC="${CC:-cc}"
CXX="${CXX:-c++}"
SRCROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$(cd "$(dirname "$0")" && pwd)/bin"
mkdir -p "$OUT"

# The headers come from the source tree, the library from the prefix. VS_USE_LATEST_API is what
# exposes the API 4.3 entry points the probes use.
INCLUDES="-I$SRCROOT/include -I$(dirname "$0")"
DEFINES="-DVS_USE_LATEST_API -D_GNU_SOURCE"
# meson installs the libraries into the python package directory rather than <prefix>/lib, so
# find libvapoursynth.so instead of assuming where it landed.
VSLIBDIR="$PREFIX/lib"
if [ ! -f "$VSLIBDIR/libvapoursynth.so" ]; then
    found="$(find "$PREFIX" -name libvapoursynth.so -print 2>/dev/null | head -1)"
    if [ -n "$found" ]; then
        VSLIBDIR="$(dirname "$found")"
    fi
fi
LIBS="-L$VSLIBDIR -Wl,-rpath,$VSLIBDIR -lvapoursynth -lpthread"

# Vulkan headers: the system ones if present, otherwise the bundled fallback the meson build uses.
if [ -f /usr/include/vulkan/vulkan_core.h ]; then
    :
elif [ -n "$VULKAN_SDK" ]; then
    INCLUDES="$INCLUDES -I$VULKAN_SDK/include"
else
    for d in "$SRCROOT"/subprojects/Vulkan-Headers-*/include "$SRCROOT"/subprojects/vulkan-headers/include; do
        [ -d "$d" ] && INCLUDES="$INCLUDES -I$d"
    done
fi

echo "prefix:  $PREFIX"
echo "libdir:  $VSLIBDIR"
echo "headers: $SRCROOT/include"
echo "flags:   $*"
echo

for src in gpu_stress.c lo_coverage.c queueorder_probe.c p16_deviceloss.c \
           timelinewait_probe.c reset_next_submit.c unsignalled_producer.c \
           reset_multithread.c release_reentry.c invariant_guards.c \
           nested_sweep.c release_frees_unsignalled.c \
           gate_reset.c free_during_reset.c transfer_contention.c gate_latch.c loader_icd_race.c \
           rtld_dlrace.c; do
    name="$(basename "$src" .c)"
    printf '  %-24s' "$name"
    # shellcheck disable=SC2086
    $CC -std=gnu11 -O2 -Wall $DEFINES $INCLUDES "$@" "$(dirname "$0")/$src" -o "$OUT/$name" $LIBS
    echo "ok"
done

for src in freecore_probe.cpp; do
    name="$(basename "$src" .cpp)"
    printf '  %-24s' "$name"
    # shellcheck disable=SC2086
    $CXX -std=gnu++20 -O2 -Wall $DEFINES $INCLUDES "$@" "$(dirname "$0")/$src" -o "$OUT/$name" $LIBS
    echo "ok"
done

# The fuzzers live in ../fuzzers; they share probe_compat.h and the prefix with the probes.
for src in "$SRCROOT"/fuzzers/*.c; do
    [ -f "$src" ] || continue
    name="$(basename "$src" .c)"
    printf '  %-24s' "$name"
    # shellcheck disable=SC2086
    $CC -std=gnu11 -O2 -Wall $DEFINES $INCLUDES "$@" "$src" -o "$OUT/$name" $LIBS
    echo "ok"
done

echo
echo "binaries in $OUT"
