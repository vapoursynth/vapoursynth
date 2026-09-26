#!/bin/sh
# The standing pass from section 2 of ../linux_tests.md, plus the probes. Nothing here provokes a
# GPU reset; reset_next_submit is deliberately left out and run by hand, because it can take the
# display down with it.
#
#   ./run_all.sh /path/to/inst [python3]
#
# Set VS_VULKAN_FORCE_STAGING=1 in the environment to run the whole thing on the staging transfer
# paths, which is half of test L2: run it once each way on a device with unified memory and
# compare.
set -u

PREFIX="$1"
if [ -z "$PREFIX" ]; then
    echo "usage: $0 <install-prefix> [python3]" >&2
    exit 2
fi
PY="${2:-python3}"
HERE="$(cd "$(dirname "$0")" && pwd)"
SRCROOT="$(cd "$HERE/.." && pwd)"
BIN="$HERE/bin"

# meson puts both the libraries and the python package under the install prefix's python
# directory, which is neither <prefix>/lib nor .../site-packages on a Debian layout.
VSLIB="$(find "$PREFIX" -name libvapoursynth.so -print 2>/dev/null | head -1)"
if [ -n "$VSLIB" ]; then
    export LD_LIBRARY_PATH="$(dirname "$VSLIB"):${LD_LIBRARY_PATH:-}"
else
    export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
fi
SITE="$(ls -d "$PREFIX"/lib/python3*/site-packages "$PREFIX"/lib/python3*/dist-packages 2>/dev/null | head -1)"
[ -n "$SITE" ] && export PYTHONPATH="$SITE:${PYTHONPATH:-}"

fails=0
run() {
    name="$1"
    shift
    echo
    echo "=== $name ==="
    if "$@"; then
        echo "--- $name: ok"
    else
        echo "--- $name: FAILED (exit $?)"
        fails=$((fails + 1))
    fi
}

echo "prefix:        $PREFIX"
echo "python:        $PY"
echo "forced staging: ${VS_VULKAN_FORCE_STAGING:-0}"

run "unit tests (expect 230, OK with 3 expected failures)" \
    "$PY" -m unittest discover -s "$SRCROOT/test" -p "*_test.py"

run "unit tests (expect 51, OK)" \
    "$PY" -m unittest discover -s "$SRCROOT/test" -p "test*.py"

# Three sections, three steps. Running them as one "all" reports a gpu-section failure under an
# audit label, because the exit code covers all three while the label names only the last. Split so
# a red step names the section that actually failed.
run "gpu resize refcheck (the reference against the scalar path)" \
    sh -c "cd '$SRCROOT/test' && '$PY' gpuresize_test.py refcheck"

# EXPECTED TO FAIL on hardware other than the 6900 XT the floors were pinned on. See L14 in
# ../linux_tests.md: 17 of 103 checks land ~27 dB low, in one specific geometry class, and it is
# an open finding rather than a threshold to relax.
run "gpu resize gpu (differential against the reference)" \
    sh -c "cd '$SRCROOT/test' && '$PY' gpuresize_test.py gpu"

run "gpu resize audit (declines and error paths)" \
    sh -c "cd '$SRCROOT/test' && '$PY' gpuresize_test.py audit"

# api_fuzz runs 30 s with a time-based seed; a failure prints the seed that reproduces it.
# The Python-side fuzzer: 30 s, time-based seed, printed on failure to reproduce.
run "python fuzzer (expect: ALL PASS, no teardown leak warning)" \
    sh -c "cd '$SRCROOT/fuzzers' && '$PY' py_fuzz.py 30"

for p in lo_coverage queueorder_probe timelinewait_probe gpu_stress freecore_probe api_fuzz; do
    if [ -x "$BIN/$p" ]; then
        run "probe: $p" "$BIN/$p"
    else
        echo
        echo "=== probe: $p === not built, run build_probes.sh first"
        fails=$((fails + 1))
    fi
done

echo
if [ "$fails" -eq 0 ]; then
    echo "ALL GREEN"
else
    echo "$fails step(s) failed"
fi

echo
echo "not run here, both need a hand on the machine:"
echo "  bin/p16_deviceloss        resets the GPU (test L7)"
echo "  bin/reset_next_submit N   resets the GPU (test L1, the open question)"
exit "$fails"
