#!/bin/sh
# Gate for the lock graph in section 2 of ../src/core/vsvulkanexec_protocol.md.
#
#   ./check_lock_order.sh <install-prefix-built-with-VS_LOCK_ORDER_CHECK> [python]
#
# Build the prefix with:
#   meson setup build-lockorder --prefix="$PWD/inst-lockorder" --buildtype=debugoptimized \
#       --force-fallback-for=zimg -Dcpp_args=-DVS_LOCK_ORDER_CHECK -Dc_args=-DVS_LOCK_ORDER_CHECK
#   ninja -C build-lockorder install
#
# Fails on an edge not in lock_order_expected.txt, on any pair seen in both orders, and on
# anything held at one of the four plugin boundaries. An expected edge that is not observed is
# reported but does not fail: coverage depends on the workload, and on a machine with no GPU
# only the cache and allocator edges can appear at all.
set -u

# Every workload is bounded. This runs unattended in CI, and the whole subject here is deadlock:
# a probe that hangs must fail the gate rather than hang the runner.
SUITE_TIMEOUT="${LOCK_ORDER_SUITE_TIMEOUT:-1800}"
PROBE_TIMEOUT="${LOCK_ORDER_PROBE_TIMEOUT:-900}"
# Which unittest modules to drive. The default is everything; narrowing it is for reproducing a
# specific edge quickly, and for checking that this gate can still fail.
TESTS="${LOCK_ORDER_TESTS:-*_test.py}"
timedout=0

PREFIX="${1:-}"
if [ -z "$PREFIX" ]; then
    echo "usage: $0 <instrumented-install-prefix> [python]" >&2
    exit 2
fi
PY="${2:-python3}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRCROOT="$(cd "$HERE/.." && pwd)"
EXPECTED="$HERE/lock_order_expected.txt"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

VSLIB="$(find "$PREFIX" -name libvapoursynth.so -print 2>/dev/null | head -1)"
[ -n "$VSLIB" ] || { echo "libvapoursynth.so not found under $PREFIX" >&2; exit 2; }
export LD_LIBRARY_PATH="$(dirname "$VSLIB"):${LD_LIBRARY_PATH:-}"
SITE="$(ls -d "$PREFIX"/lib/python3*/site-packages "$PREFIX"/lib/python3*/dist-packages 2>/dev/null | head -1)"
[ -n "$SITE" ] && export PYTHONPATH="$SITE:${PYTHONPATH:-}"

# Refuse to pass on a build without the instrumentation. Without this the gate is green for the
# most boring reason there is: no reports at all means no unexpected edges.
"$PY" -c 'import vapoursynth; vapoursynth.core.std.BlankClip().get_frame(0)' > "$WORK/probe.txt" 2>&1
if ! grep -q "=== lock order" "$WORK/probe.txt"; then
    echo "FAIL: $PREFIX has no lock-order instrumentation -- rebuild with -DVS_LOCK_ORDER_CHECK" >&2
    exit 2
fi

echo "prefix: $PREFIX"
echo "python: $PY"
echo

for mode in default staging; do
    [ "$mode" = staging ] && FS=1 || FS=""
    env ${FS:+VS_VULKAN_FORCE_STAGING=1} \
        timeout "$SUITE_TIMEOUT" "$PY" -m unittest discover -s "$SRCROOT/test" -p "$TESTS" \
        > "$WORK/suite_$mode.log" 2>&1
    rc=$?
    echo "  suite/$mode exit=$rc"
    if [ "$rc" = 124 ]; then
        echo "  FAIL: suite/$mode timed out after ${SUITE_TIMEOUT}s"
        timedout=1
    fi
done
for p in lo_coverage queueorder_probe timelinewait_probe freecore_probe gpu_stress api_fuzz; do
    if [ -x "$HERE/bin/$p" ]; then
        timeout "$PROBE_TIMEOUT" "$HERE/bin/$p" > "$WORK/probe_$p.log" 2>&1
        rc=$?
        echo "  probe/$p exit=$rc"
        if [ "$rc" = 124 ]; then
            echo "  FAIL: probe/$p timed out after ${PROBE_TIMEOUT}s"
            timedout=1
        fi
    else
        echo "  probe/$p not built, skipped"
    fi
done

grep -hE "^  [A-Za-z]+ +-> " "$WORK"/*.log 2>/dev/null \
    | sed -E 's/^ +//; s/ +/ /g' | sort -u > "$WORK/observed.txt"
grep -vE '^\s*(#|$)' "$EXPECTED" | sed -E 's/^ +//; s/ +/ /g' | sort -u > "$WORK/expected.txt"

echo
echo "edges observed:"
sed 's/^/  /' "$WORK/observed.txt"

fails=0
[ "$timedout" = 1 ] && fails=$((fails + 1))

NEW="$(comm -23 "$WORK/observed.txt" "$WORK/expected.txt")"
if [ -n "$NEW" ]; then
    echo
    echo "FAIL: lock edge(s) not in lock_order_expected.txt:"
    printf '%s\n' "$NEW" | sed 's/^/  /'
    echo "  Each is a new edge in the graph. Justify it against section 2 of"
    echo "  vsvulkanexec_protocol.md before adding it to the expected list."
    fails=$((fails + 1))
fi

if grep -hq "INVERSION" "$WORK"/*.log 2>/dev/null; then
    echo
    echo "FAIL: a pair was taken in both orders:"
    grep -h "INVERSION" "$WORK"/*.log | sort -u | sed 's/^/  /'
    fails=$((fails + 1))
fi

if grep -hq "BOUNDARY VIOLATION" "$WORK"/*.log 2>/dev/null; then
    echo
    echo "FAIL: a core lock was held across a plugin boundary:"
    grep -h "BOUNDARY VIOLATION" "$WORK"/*.log | sort -u | sed 's/^/  /'
    fails=$((fails + 1))
fi

MISSING="$(comm -13 "$WORK/observed.txt" "$WORK/expected.txt")"
if [ -n "$MISSING" ]; then
    echo
    echo "note: expected edge(s) not reached by this run (coverage, not a failure):"
    printf '%s\n' "$MISSING" | sed 's/^/  /'
fi

echo
if [ "$fails" -eq 0 ]; then
    echo "LOCK ORDER OK"
else
    echo "$fails lock-order check(s) failed"
fi
exit "$fails"
